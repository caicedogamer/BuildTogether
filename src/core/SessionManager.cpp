#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/core/ActivityLog.hpp>
#include <EditorP2P/core/EditorOperation.hpp>
#include <EditorP2P/editor/EditorBridge.hpp>
#include <EditorP2P/editor/ObjectRegistry.hpp>
#include <EditorP2P/net/WinSocketTransport.hpp>
#include <EditorP2P/protocol/GroupMetadata.hpp>
#include <EditorP2P/protocol/MessageCodec.hpp>
#include <EditorP2P/util/Clock.hpp>
#include <EditorP2P/util/StringUtil.hpp>
#include <EditorP2P/config/BuildConfig.hpp>

#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

namespace ep2p {

    namespace {
        bool peerHasPermission(const Session& session, PlayerId senderId,
                               bool PermissionFlags::*flag) {
            if (senderId == HOST_PLAYER_ID) {
                auto ownerPermissions = permissionsForRole(Role::Owner);
                return ownerPermissions.*flag;
            }

            auto* peer = session.findPeer(senderId);
            return peer && peer->connected && (peer->permissions.*flag);
        }

        PermissionFlags requirePermission(bool PermissionFlags::*flag) {
            PermissionFlags permissions;
            permissions.*flag = true;
            return permissions;
        }

        NetworkObjectId canonicalPeerObjectId(PlayerId peerId, NetworkObjectId localId) {
            if ((localId >> 24) == peerId) {
                return localId;
            }
            return (static_cast<NetworkObjectId>(peerId) << 24) | (localId & 0x00FFFFFF);
        }

        std::string objectFingerprint(GameObject* object) {
            if (!object) return {};
            if (auto* lel = LevelEditorLayer::get()) {
                return object->getSaveString(lel);
            }

            auto pos = object->getPosition();
            return std::to_string(object->m_objectID) + "@" +
                   std::to_string(pos.x) + "," + std::to_string(pos.y);
        }

        EditorOperation makeObjectOperation(
            EditorOperationType type,
            EditorOperationDirection direction,
            NetworkObjectId netId,
            const MessageCodec::PlaceObjectFields& fields,
            PlayerId playerId,
            uint32_t messageSequence,
            std::string origin
        ) {
            EditorOperation operation;
            operation.type = type;
            operation.direction = direction;
            operation.netId = netId;
            operation.gdObjectId = fields.gdObjectId;
            operation.x = fields.x;
            operation.y = fields.y;
            operation.rotation = fields.rotation;
            operation.scaleX = fields.scaleX;
            operation.scaleY = fields.scaleY;
            operation.playerId = playerId;
            operation.messageSequence = messageSequence;
            operation.origin = std::move(origin);
            operation.saveString = fields.saveString;
            operation.groups = GroupMetadataParser::parseSaveString(fields.saveString);
            return operation;
        }

        uint64_t recordObjectOperation(
            EditorOperationType type,
            EditorOperationDirection direction,
            NetworkObjectId netId,
            const MessageCodec::PlaceObjectFields& fields,
            PlayerId playerId,
            uint32_t messageSequence,
            const std::string& origin
        ) {
            return EditorOperationRecorder::get().record(
                makeObjectOperation(
                    type, direction, netId, fields, playerId, messageSequence, origin
                )
            );
        }

        Result<Message> decodeCompleteFrame(const std::vector<uint8_t>& bytes) {
            if (bytes.size() < sizeof(FrameHeader)) {
                return Result<Message>::err("Frame is shorter than header");
            }

            FrameHeader hdr;
            std::memcpy(&hdr, bytes.data(), sizeof(FrameHeader));
            if (hdr.magic != FRAME_MAGIC) {
                return Result<Message>::err("Bad frame magic");
            }
            if (hdr.payloadLen > MAX_FRAME_PAYLOAD) {
                return Result<Message>::err("Frame too large");
            }
            if (bytes.size() != sizeof(FrameHeader) + hdr.payloadLen) {
                return Result<Message>::err("Frame length mismatch");
            }

            std::string body(
                reinterpret_cast<const char*>(bytes.data() + sizeof(FrameHeader)),
                hdr.payloadLen
            );
            auto parts = StringUtil::split(body, '|');
            if (parts.size() < 3) {
                return Result<Message>::err("Frame body missing sender/sequence/payload");
            }

            auto senderId = StringUtil::parseInt(parts[0]);
            auto seq = StringUtil::parseInt(parts[1]);
            if (!senderId || !seq || *senderId < 0 || *seq < 0) {
                return Result<Message>::err("Frame body has invalid sender/sequence");
            }

            Message msg;
            msg.type = static_cast<MessageType>(hdr.type);
            msg.senderId = static_cast<uint32_t>(*senderId);
            msg.sequence = static_cast<uint32_t>(*seq);
            msg.payload = StringUtil::join(
                std::vector<std::string>(parts.begin() + 2, parts.end()), '|'
            );
            return Result<Message>::ok(std::move(msg));
        }
    }

    SessionManager& SessionManager::get() {
        static SessionManager instance;
        return instance;
    }

    SessionManager::~SessionManager() {
        disconnect("SessionManager shutting down");
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    Result<void> SessionManager::startHost(const SessionConfig& config) {
        if (m_session) {
            return Result<void>::err("A session is already active. Disconnect first.");
        }

        m_session = std::make_unique<Session>(config);
        m_session->setState(SessionState::Hosting);

#ifdef EP2P_WINDOWS
        auto listener = openHostListener(config);
        if (!listener) {
            m_session.reset();
            setStatus("Host failed: " + listener.error());
            return listener;
        }
#else
        return Result<void>::err("EditorP2P networking is Windows-only in this prototype.");
#endif

        Clock::markSessionStart();
        ActivityLog::get().add(ActivityEventType::Generic, HOST_PLAYER_ID, 0, "Session started (hosting)");
        setStatus("Waiting for peer on port " + std::to_string(config.hostPort));

        return Result<void>::ok();
    }

    Result<void> SessionManager::joinAsPeer(const SessionConfig& config) {
        if (m_session) {
            return Result<void>::err("A session is already active. Disconnect first.");
        }

        m_session = std::make_unique<Session>(config);
        m_session->setState(SessionState::Joining);

#ifdef EP2P_WINDOWS
        setStatus("Connecting to " + config.remoteEndpoint.str());
        m_controlTransport = std::make_unique<WinTcpTransport>(config.remoteEndpoint, 5000);
        if (!m_controlTransport->isConnected()) {
            m_controlTransport.reset();
            m_session.reset();
            setStatus("Join failed: could not connect");
            return Result<void>::err("Could not connect to " + config.remoteEndpoint.str());
        }

        configureTransportCallbacks();
        if (!m_controlTransport->start()) {
            m_controlTransport.reset();
            m_session.reset();
            setStatus("Join failed: receive thread could not start");
            return Result<void>::err("Connected, but receive thread could not start.");
        }

        MessageCodec::HelloFields hello;
        hello.protocolVersion = PROTOCOL_VERSION;
        hello.sessionKey = config.sessionKey;
        hello.displayName = config.displayName;
        hello.requestedRole = static_cast<uint8_t>(Role::Builder);

        Message msg;
        msg.type = MessageType::Hello;
        // The peer does not have an assigned ID until HelloAck. Use a parseable
        // placeholder so the host can decode and approve the hello frame.
        msg.senderId = 0;
        msg.sequence = m_controlSequence++;
        msg.payload = MessageCodec::encodeHello(hello);

        if (!sendMessage(std::move(msg))) {
            disconnect("Could not send hello");
            return Result<void>::err("Connected, but could not send hello.");
        }

        setStatus("Connected to host. Waiting for approval.");
#else
        return Result<void>::err("EditorP2P networking is Windows-only in this prototype.");
#endif

        return Result<void>::ok();
    }

    void SessionManager::disconnect(const std::string& reason) {
        if (!m_session) return;

#ifdef EP2P_WINDOWS
        closeHostListener();
        m_controlTransport.reset();
        m_controlSequence = 1;
        m_controlCodec.reset();
        m_objectLocks.clear();
        m_heldLocks.clear();
#endif

        m_session->setState(SessionState::Idle);
        EditorBridge::get().onSessionDisconnected();
        ObjectRegistry::get().clear();
        m_session.reset();
        m_cursorAccum   = 0.f;
        m_heartbeatAccum = 0.f;
        m_cursorX = 0.f;
        m_cursorY = 0.f;
        m_presenceSeq   = 0;
        m_lastHeartbeatMs = 0;
        m_applyingRemote = false;
        m_knownLocalObjects.clear();
        m_baselineLocalObjects.clear();
        EditorOperationRecorder::get().clear();
        setStatus(reason.empty() ? "Disconnected" : "Disconnected: " + reason);
    }

    // -------------------------------------------------------------------------
    // State queries
    // -------------------------------------------------------------------------

    SessionState SessionManager::state() const {
        return m_session ? m_session->state() : SessionState::Idle;
    }

    SessionMode SessionManager::mode() const {
        return m_session ? m_session->mode() : SessionMode::None;
    }

    bool SessionManager::isConnected() const {
        return state() == SessionState::Connected;
    }

    bool SessionManager::isHost() const {
        return mode() == SessionMode::Host;
    }

    bool SessionManager::isPeer() const {
        return mode() == SessionMode::Peer;
    }

    Role SessionManager::myRole() const {
        return m_session ? m_session->myRole() : Role::Owner;
    }

    PlayerId SessionManager::myPlayerId() const {
        return m_session ? m_session->myPlayerId() : HOST_PLAYER_ID;
    }

    std::string SessionManager::statusText() const {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_statusText;
    }

    uint32_t SessionManager::statusRevision() const {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_statusRevision;
    }

    std::string SessionManager::roomSummary() const {
        auto summary = statusText();
        if (!m_session) return summary;

        if (m_session->isHost()) {
            summary += "\nYou: Host / Owner";
            const auto& peers = m_session->peers();
            if (peers.empty()) {
                summary += "\nPeer: none";
            } else {
                const auto& peer = peers.front();
                summary += "\nPeer: " + peer.displayName + " / " + roleName(peer.role);
            }
        } else {
            summary += "\nYou: Peer / " + std::string(roleName(m_session->myRole()));
            summary += "\nHost: " + m_session->config().remoteEndpoint.str();
        }

        if (m_session->isConnected()) {
            auto ageMs = Clock::elapsed(m_lastHeartbeatMs);
            summary += "\nHeartbeat: " + std::to_string(ageMs / 1000) + "s ago";
        }
        return summary;
    }

    // -------------------------------------------------------------------------
    // Editor hook callbacks (main thread)
    // -------------------------------------------------------------------------

    void SessionManager::onLocalObjectPlaced(void* gdObject, int objectKey,
                                              float requestedX, float requestedY) {
        if (m_applyingRemote || !isConnected()) return;
        if (!hasPermission(requirePermission(&PermissionFlags::canEdit))) return;
        auto* object = static_cast<GameObject*>(gdObject);
        if (!object) return;

        // Prefer the key/position that EditorUI passed to createObject; they are
        // the authoritative intended values before any GD-internal transforms.
        int idToSync = (objectKey > 0) ? objectKey : object->m_objectID;
        if (idToSync <= 0) return;

        // Diagnostic: warn when m_objectID or actual position differ from what was requested.
        if (idToSync != object->m_objectID) {
            geode::log::warn(
                "[EditorP2P] onLocalObjectPlaced: key={} m_objectID={} — using key",
                idToSync, object->m_objectID
            );
        }
        auto actualPos = object->getPosition();
        if (std::abs(actualPos.x - requestedX) > 0.5f || std::abs(actualPos.y - requestedY) > 0.5f) {
            geode::log::warn(
                "[EditorP2P] onLocalObjectPlaced pos mismatch: requested=({},{}) actual=({},{})",
                requestedX, requestedY, actualPos.x, actualPos.y
            );
        }
        geode::log::info(
            "[EditorP2P] onLocalObjectPlaced: id={} pos=({},{})",
            idToSync, requestedX, requestedY
        );

        MessageCodec::PlaceObjectFields fields;
        if (isHost()) {
            fields.tempClientId = ObjectRegistry::get().registerObject(gdObject);
        } else {
            fields.tempClientId = canonicalPeerObjectId(myPlayerId(), m_nextTempId++);
            ObjectRegistry::get().bindObject(fields.tempClientId, gdObject);
        }
        fields.gdObjectId = static_cast<uint16_t>(idToSync);
        // Send the final object transform. The remote side creates the object and
        // then overwrites the transform so pasted/duplicated objects sync exactly.
        fields.x        = actualPos.x;
        fields.y        = actualPos.y;
        fields.rotation = object->getRotation();
        fields.scaleX   = object->m_scaleX;
        fields.scaleY   = object->m_scaleY;
        if (auto* lel = LevelEditorLayer::get()) {
            fields.saveString = object->getSaveString(lel);
        }

#ifdef EP2P_WINDOWS
        if (sendPlaceObject(fields.tempClientId, fields)) {
            m_knownLocalObjects.insert(gdObject);
            m_baselineLocalObjects.erase(gdObject);
        } else {
            ObjectRegistry::get().removeByObject(gdObject);
            geode::log::warn(
                "[EditorP2P] Failed to send placement for netId={}, leaving object unregistered for retry",
                fields.tempClientId
            );
        }
#else
        m_knownLocalObjects.insert(gdObject);
#endif
    }

    void SessionManager::onLocalObjectDiscovered(void* gdObject, int objectKey, float x, float y) {
        if (!gdObject || !isConnected() || m_applyingRemote) return;
        if (ObjectRegistry::get().hasObject(gdObject)) {
            m_knownLocalObjects.insert(gdObject);
            m_baselineLocalObjects.erase(gdObject);
            return;
        }

        auto* object = static_cast<GameObject*>(gdObject);
        auto baseline = m_baselineLocalObjects.find(gdObject);
        if (baseline != m_baselineLocalObjects.end()) {
            auto current = objectFingerprint(object);
            if (current == baseline->second) {
                return;
            }

            geode::log::debug(
                "[EditorP2P] Baseline pointer changed content, allowing discovery obj={} id={}",
                gdObject, object ? object->m_objectID : 0
            );
            m_baselineLocalObjects.erase(baseline);
            m_knownLocalObjects.erase(gdObject);
        }

        geode::log::info(
            "[EditorP2P] Discovered unsynced local object id={} pos=({},{}), sending placement",
            objectKey, x, y
        );
        onLocalObjectPlaced(gdObject, objectKey, x, y);
    }

    void SessionManager::markLocalObjectKnown(void* gdObject) {
        if (gdObject) {
            m_knownLocalObjects.insert(gdObject);
            m_baselineLocalObjects[gdObject] = objectFingerprint(static_cast<GameObject*>(gdObject));
        }
    }

    void SessionManager::onLocalObjectDeleted(uint32_t netId) {
        if (m_applyingRemote || !isConnected()) return;
        if (netId == INVALID_OBJECT_ID) return;
        if (!hasPermission(requirePermission(&PermissionFlags::canDelete))) return;

#ifdef EP2P_WINDOWS
        sendDeleteObject(netId);
#endif
        auto* object = ObjectRegistry::get().findByNetworkId(netId);
        ObjectRegistry::get().removeByNetworkId(netId);
        m_knownLocalObjects.erase(object);
        m_baselineLocalObjects.erase(object);
    }

    void SessionManager::onLocalObjectSelected(uint32_t netId) {
        if (m_applyingRemote) return;
        if (!isConnected() || netId == INVALID_OBJECT_ID) return;
        if (!hasPermission(requirePermission(&PermissionFlags::canLock))) return;
        if (isHost()) {
            m_heldLocks.insert(netId);
            m_objectLocks[netId] = HOST_PLAYER_ID;
            return;
        }

#ifdef EP2P_WINDOWS
        sendLockMessage(MessageType::LockRequest, netId);
#endif
    }

    void SessionManager::onLocalObjectEdited(uint32_t netId) {
        if (m_applyingRemote) return;
        if (!isConnected() || netId == INVALID_OBJECT_ID) return;
        if (!hasPermission(requirePermission(&PermissionFlags::canEdit))) return;

#ifdef EP2P_WINDOWS
        bool lockHeld = m_heldLocks.count(netId) != 0;
        geode::log::info(
            "[EditorP2P] onLocalObjectEdited netId={} isHost={} lockHeld={} heldLocks.size={}",
            netId, isHost(), lockHeld, m_heldLocks.size()
        );
        if (!isHost() && !lockHeld) {
            geode::log::warn(
                "[EditorP2P] Sending edit commit without held lock netId={}", netId
            );
        }
        sendCommitEdit(netId);
#endif
    }

    void SessionManager::onLocalObjectDeselected(uint32_t netId) {
        if (m_applyingRemote) return;
        if (!isConnected() || netId == INVALID_OBJECT_ID) return;

#ifdef EP2P_WINDOWS
        if (isHost() || m_heldLocks.count(netId) != 0) {
            sendCommitEdit(netId);
            sendLockMessage(MessageType::UnlockObject, netId);
        }
#endif
        m_heldLocks.erase(netId);
        if (isHost()) {
            m_objectLocks.erase(netId);
        }
    }

    void SessionManager::onLocalSaveRequested() {
        if (!isConnected() || isHost()) return;
        if (!hasPermission(requirePermission(&PermissionFlags::canSave)) &&
            !hasPermission(requirePermission(&PermissionFlags::canRequestSave))) {
            setStatus("You do not have permission to save this session.");
            return;
        }

#ifdef EP2P_WINDOWS
        if (hasPermission(requirePermission(&PermissionFlags::canSave))) {
            sendSaveRequest();
            setStatus("Save command sent to host.");
        } else {
            sendSaveRequest();
            setStatus("Save requested from host.");
        }
#else
        setStatus("Save requests require the Windows networking build.");
#endif
    }

    bool SessionManager::setPeerRole(PlayerId peerId, Role role) {
        if (!m_session || !isHost()) return false;
        auto* peer = m_session->findPeer(peerId);
        if (!peer) {
            setStatus("No connected peer to update.");
            return false;
        }

        peer->role = role;
        peer->permissions = permissionsForRole(role);
        ActivityLog::get().add(
            ActivityEventType::PermissionChanged,
            HOST_PLAYER_ID,
            0,
            "Peer role changed to " + std::string(roleName(role))
        );
        setStatus("Peer role changed to " + std::string(roleName(role)));

#ifdef EP2P_WINDOWS
        return sendPermissionUpdate(peerId, role);
#else
        return true;
#endif
    }

    void SessionManager::requestStateResync() {
        if (!isConnected()) return;
#ifdef EP2P_WINDOWS
        if (isHost()) {
            if (sendStateSnapshot()) {
                setStatus("State pushed to peer.");
            } else {
                setStatus("State push failed.");
            }
        } else {
            if (sendStateResyncRequest()) {
                setStatus("State resync requested.");
            } else {
                setStatus("State resync request failed.");
            }
        }
#endif
    }

    void SessionManager::onCursorMoved(float worldX, float worldY) {
        // Stored here; the tick() accumulator decides when to actually send.
        // EditorBridge caches the position for EditorBridge::cursorWorldX/Y.
        m_cursorX = worldX;
        m_cursorY = worldY;
        EditorBridge::get().setCursorWorld(worldX, worldY);
    }

    // -------------------------------------------------------------------------
    // Per-frame tick (main thread, driven by Cocos scheduler)
    // -------------------------------------------------------------------------

    void SessionManager::tick(float dt) {
        drainQueue();

        if (!isConnected()) return;

        m_heartbeatAccum += dt;
        if (m_heartbeatAccum * 1000.f >= static_cast<float>(HEARTBEAT_INTERVAL_MS)) {
            m_heartbeatAccum = 0.f;
#ifdef EP2P_WINDOWS
            sendHeartbeat("ping");
#endif
        }

        if (m_lastHeartbeatMs != 0 && Clock::elapsed(m_lastHeartbeatMs) > HEARTBEAT_TIMEOUT_MS) {
            disconnect("Heartbeat timed out");
            return;
        }

        // Send presence updates at PRESENCE_RATE_HZ.
        m_cursorAccum += dt;
        const float presenceInterval = 1.f / static_cast<float>(PRESENCE_RATE_HZ);
        if (m_cursorAccum >= presenceInterval) {
            m_cursorAccum -= presenceInterval;
            ++m_presenceSeq;
#ifdef EP2P_WINDOWS
            sendPresenceUpdate();
#endif
        }
    }

    // -------------------------------------------------------------------------
    // Thread dispatch
    // -------------------------------------------------------------------------

    void SessionManager::postToMainThread(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push(std::move(fn));
    }

    void SessionManager::drainQueue() {
        // Swap to a local queue so net threads can keep posting while we drain.
        std::queue<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            std::swap(local, m_queue);
        }
        while (!local.empty()) {
            local.front()();
            local.pop();
        }
    }

    void SessionManager::setStatus(std::string text) {
        geode::log::info("[EditorP2P] {}", text);
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusText = std::move(text);
        ++m_statusRevision;
    }

#ifdef EP2P_WINDOWS

    Result<void> SessionManager::openHostListener(const SessionConfig& config) {
        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET) {
            return Result<void>::err("Could not create TCP listener.");
        }

        BOOL reuse = TRUE;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(config.hostPort);

        if (::bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(listenSock);
            return Result<void>::err("Could not bind TCP port " + std::to_string(config.hostPort));
        }

        if (::listen(listenSock, 1) == SOCKET_ERROR) {
            closesocket(listenSock);
            return Result<void>::err("Could not listen on TCP port " + std::to_string(config.hostPort));
        }

        m_listenSocket = static_cast<uintptr_t>(listenSock);
        m_acceptRunning = true;
        m_acceptThread = std::thread(&SessionManager::acceptLoop, this);
        return Result<void>::ok();
    }

    void SessionManager::closeHostListener() {
        m_acceptRunning = false;
        auto sock = static_cast<SOCKET>(m_listenSocket);
        if (sock != INVALID_SOCKET && sock != 0) {
            shutdown(sock, SD_BOTH);
            closesocket(sock);
            m_listenSocket = 0;
        }
        if (m_acceptThread.joinable()) {
            m_acceptThread.join();
        }
    }

    void SessionManager::acceptLoop() {
        auto listenSock = static_cast<SOCKET>(m_listenSocket);
        while (m_acceptRunning) {
            sockaddr_in from{};
            int fromLen = sizeof(from);
            SOCKET peerSock = accept(listenSock, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (peerSock == INVALID_SOCKET) {
                if (m_acceptRunning) {
                    postToMainThread([this] {
                        if (m_session && m_session->state() == SessionState::Hosting) {
                            setStatus("Listener stopped accepting peers.");
                        }
                    });
                }
                break;
            }

            postToMainThread([this, raw = static_cast<uintptr_t>(peerSock)] {
                adoptAcceptedSocket(raw);
            });
        }
    }

    void SessionManager::adoptAcceptedSocket(uintptr_t rawSocket) {
        auto sock = static_cast<SOCKET>(rawSocket);
        if (!m_session || !m_session->isHost()) {
            closesocket(sock);
            return;
        }
        if (m_controlTransport && m_controlTransport->isConnected()) {
            closesocket(sock);
            setStatus("Rejected extra peer: V1 supports one peer.");
            return;
        }

        m_controlTransport = std::make_unique<WinTcpTransport>(sock);
        configureTransportCallbacks();
        if (!m_controlTransport->start()) {
            m_controlTransport.reset();
            setStatus("Accepted peer, but receive thread failed.");
            return;
        }

        setStatus("Peer socket connected. Waiting for hello.");
    }

    void SessionManager::configureTransportCallbacks() {
        if (!m_controlTransport) return;

        m_controlTransport->onDataReceived = [this](const uint8_t* data, size_t len) {
            std::vector<uint8_t> bytes(data, data + len);
            postToMainThread([this, bytes = std::move(bytes)]() mutable {
                handleTcpBytes(std::move(bytes));
            });
        };

        m_controlTransport->onDisconnected = [this](std::string reason) {
            postToMainThread([this, reason = std::move(reason)] {
                handleTransportDisconnect(reason);
            });
        };
    }

    void SessionManager::handleTcpBytes(std::vector<uint8_t> bytes) {
        auto framed = decodeCompleteFrame(bytes);
        if (framed) {
            handleMessage(framed.value());
            return;
        }

        geode::log::info("[EditorP2P] TCP bytes received: {}", bytes.size());
        geode::log::warn("[EditorP2P] Direct TCP frame decode failed: {}", framed.error());
        m_controlCodec.feed(bytes.data(), bytes.size(), [this](Message msg) {
            handleMessage(msg);
        });
    }

    void SessionManager::handleMessage(const Message& msg) {
        if (!m_session) return;

        if (msg.type != MessageType::Heartbeat && msg.type != MessageType::PresenceUpdate) {
            geode::log::info(
                "[EditorP2P] TCP message received: type={} sender={} seq={}",
                static_cast<uint16_t>(msg.type),
                msg.senderId,
                msg.sequence
            );
        }

        if (isHost() && msg.type == MessageType::Hello) {
            handleHostHello(msg);
            return;
        }
        if (isPeer() && msg.type == MessageType::HelloAck) {
            handlePeerHelloAck(msg);
            return;
        }
        if (msg.type == MessageType::Heartbeat) {
            handleHeartbeat(msg);
            return;
        }
        if (msg.type == MessageType::PresenceUpdate) {
            handlePresenceUpdate(msg);
            return;
        }
        if (msg.type == MessageType::PlaceObject) {
            handlePlaceObject(msg);
            return;
        }
        if (msg.type == MessageType::DeleteObject) {
            handleDeleteObject(msg);
            return;
        }
        if (msg.type == MessageType::LockRequest) {
            handleLockRequest(msg);
            return;
        }
        if (msg.type == MessageType::LockGranted) {
            handleLockReply(msg, true);
            return;
        }
        if (msg.type == MessageType::LockDenied) {
            handleLockReply(msg, false);
            return;
        }
        if (msg.type == MessageType::CommitEdit) {
            handleCommitEdit(msg);
            return;
        }
        if (msg.type == MessageType::UnlockObject) {
            handleUnlockObject(msg);
            return;
        }
        if (msg.type == MessageType::PermissionUpdate) {
            handlePermissionUpdate(msg);
            return;
        }
        if (msg.type == MessageType::SaveRequest) {
            handleSaveRequest(msg);
            return;
        }
        if (msg.type == MessageType::SaveCommand) {
            handleSaveCommand(msg);
            return;
        }
        if (msg.type == MessageType::StateResyncRequest) {
            handleStateResyncRequest(msg);
            return;
        }
        if (msg.type == MessageType::StateSnapshot) {
            handleStateSnapshot(msg);
            return;
        }

        setStatus("Ignored unexpected message type " + std::to_string(static_cast<uint16_t>(msg.type)));
    }

    void SessionManager::handleHostHello(const Message& msg) {
        auto decoded = MessageCodec::decodeHello(msg.payload);

        MessageCodec::HelloAckFields ack;
        ack.accepted = false;
        ack.assignedPeerId = 1;
        ack.grantedRole = static_cast<uint8_t>(Role::Builder);
        ack.hostDisplayName = m_session->config().displayName;
        ack.roomName = m_session->config().roomName;

        if (!decoded) {
            ack.rejectReason = decoded.error();
        } else if (decoded.value().protocolVersion < MIN_COMPAT_VERSION ||
                   decoded.value().protocolVersion > PROTOCOL_VERSION) {
            ack.rejectReason = "Incompatible protocol version.";
        } else if (decoded.value().sessionKey != m_session->config().sessionKey) {
            ack.rejectReason = "Invalid session key.";
        } else {
            ack.accepted = true;
        }

        Message reply;
        reply.type = MessageType::HelloAck;
        reply.senderId = HOST_PLAYER_ID;
        reply.sequence = m_controlSequence++;
        reply.payload = MessageCodec::encodeHelloAck(ack);
        sendMessage(std::move(reply));

        if (!ack.accepted) {
            setStatus("Rejected peer: " + ack.rejectReason);
            m_controlTransport.reset();
            return;
        }

        PeerRecord peer;
        peer.id = ack.assignedPeerId;
        peer.displayName = decoded.value().displayName;
        peer.role = Role::Builder;
        peer.permissions = permissionsForRole(peer.role);
        peer.connected = true;
        m_session->addPeer(peer);
        m_session->setState(SessionState::Connected);
        m_lastHeartbeatMs = Clock::now();
        m_heartbeatAccum = 0.f;
        EditorBridge::get().onSessionConnected();
        setStatus("Peer connected: " + peer.displayName);
        ActivityLog::get().add(ActivityEventType::Generic, peer.id, 0, "Peer connected: " + peer.displayName);
    }

    void SessionManager::handlePeerHelloAck(const Message& msg) {
        auto decoded = MessageCodec::decodeHelloAck(msg.payload);
        if (!decoded) {
            disconnect("Invalid hello ack: " + decoded.error());
            return;
        }

        const auto& ack = decoded.value();
        if (!ack.accepted) {
            disconnect("Join rejected: " + ack.rejectReason);
            return;
        }

        m_session->setMyPlayerId(ack.assignedPeerId);
        m_session->setMyRole(static_cast<Role>(ack.grantedRole));
        m_session->setState(SessionState::Connected);
        m_lastHeartbeatMs = Clock::now();
        m_heartbeatAccum = 0.f;
        EditorBridge::get().onSessionConnected();
        setStatus("Connected to " + ack.roomName + " as " + roleName(m_session->myRole()));
        ActivityLog::get().add(ActivityEventType::Generic, ack.assignedPeerId, 0, "Joined room: " + ack.roomName);
    }

    void SessionManager::handleHeartbeat(const Message& msg) {
        if (!m_session || !m_session->isConnected()) return;

        m_lastHeartbeatMs = Clock::now();
        if (msg.payload == "ping") {
            sendHeartbeat("pong");
        }
    }

    void SessionManager::handlePresenceUpdate(const Message& msg) {
        if (!m_session || !m_session->isConnected()) return;

        auto decoded = MessageCodec::decodePresence(msg.payload);
        if (!decoded) {
            geode::log::warn("[EditorP2P] Invalid presence update: {}", decoded.error());
            return;
        }

        PresenceState state;
        state.playerId = msg.senderId;
        state.displayName = decoded.value().displayName;
        state.editorX = decoded.value().x;
        state.editorY = decoded.value().y;
        state.colorRgb = decoded.value().colorRgb;
        state.lastUpdate = Clock::now();
        state.sequence = msg.sequence;
        EditorBridge::get().applyRemotePresence(state);
    }

    void SessionManager::handlePlaceObject(const Message& msg) {
        if (!m_session || !m_session->isConnected()) return;
        if (!peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canEdit)) {
            geode::log::warn("[EditorP2P] Rejected place object from unauthorized sender {}", msg.senderId);
            return;
        }

        auto decoded = MessageCodec::decodePlaceObject(msg.payload);
        if (!decoded) {
            geode::log::warn("[EditorP2P] Invalid place object: {}", decoded.error());
            return;
        }

        auto fields = decoded.value();
        NetworkObjectId netId = fields.tempClientId;
        if (isHost()) {
            // Keep peer provisional IDs out of the host's own low ID range.
            netId = canonicalPeerObjectId(msg.senderId, fields.tempClientId);
        }

        auto opId = recordObjectOperation(
            EditorOperationType::Place,
            EditorOperationDirection::RemoteReceive,
            netId,
            fields,
            msg.senderId,
            msg.sequence,
            "tcp.place_object"
        );
        auto groups = GroupMetadataParser::parseSaveString(fields.saveString);
        geode::log::info(
            "[EditorP2P] handlePlaceObject op={} gdObjectId={} netId={} pos=({},{}) rot={} scale=({},{}) {}",
            opId,
            fields.gdObjectId, netId, fields.x, fields.y,
            fields.rotation, fields.scaleX, fields.scaleY,
            GroupMetadataParser::summarize(groups)
        );

        m_applyingRemote = true;
        void* created = EditorBridge::get().applyRemotePlacement(
            netId,
            fields.gdObjectId,
            fields.x,
            fields.y,
            fields.rotation,
            fields.scaleX,
            fields.scaleY,
            fields.saveString
        );
        m_applyingRemote = false;

        if (created) {
            ObjectRegistry::get().bindObject(netId, created);
            geode::log::info(
                "[EditorP2P] Applied remote object id={} obj={} from peer={}",
                netId,
                fields.gdObjectId,
                msg.senderId
            );
        }
    }

    void SessionManager::handleDeleteObject(const Message& msg) {
        if (!m_session || !m_session->isConnected()) return;
        if (!peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canDelete)) {
            geode::log::warn("[EditorP2P] Rejected delete object from unauthorized sender {}", msg.senderId);
            return;
        }

        auto parsed = StringUtil::parseInt(msg.payload);
        if (!parsed || *parsed <= 0) {
            geode::log::warn("[EditorP2P] Invalid delete object payload: {}", msg.payload);
            return;
        }

        NetworkObjectId netId = static_cast<NetworkObjectId>(*parsed);
        if (isHost() && msg.senderId != HOST_PLAYER_ID &&
            !ObjectRegistry::get().hasNetworkId(netId)) {
            netId = canonicalPeerObjectId(msg.senderId, netId);
        }

        EditorOperation operation;
        operation.type = EditorOperationType::Delete;
        operation.direction = EditorOperationDirection::RemoteReceive;
        operation.netId = netId;
        operation.playerId = msg.senderId;
        operation.messageSequence = msg.sequence;
        operation.origin = "tcp.delete_object";
        auto opId = EditorOperationRecorder::get().record(std::move(operation));

        m_applyingRemote = true;
        EditorBridge::get().applyRemoteDeletion(netId);
        m_applyingRemote = false;
        ObjectRegistry::get().removeByNetworkId(netId);
        m_objectLocks.erase(netId);
        m_heldLocks.erase(netId);

        geode::log::info(
            "[EditorP2P] Applied remote delete op={} id={} from peer={}",
            opId,
            netId,
            msg.senderId
        );
    }

    void SessionManager::handleLockRequest(const Message& msg) {
        if (!m_session || !m_session->isConnected() || !isHost()) return;
        if (!peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canLock)) {
            sendLockMessage(MessageType::LockDenied, INVALID_OBJECT_ID, "No lock permission");
            return;
        }

        auto parsed = StringUtil::parseInt(msg.payload);
        if (!parsed || *parsed <= 0) return;

        NetworkObjectId netId = static_cast<NetworkObjectId>(*parsed);
        if (!ObjectRegistry::get().hasNetworkId(netId)) {
            netId = canonicalPeerObjectId(msg.senderId, netId);
        }

        auto it = m_objectLocks.find(netId);
        if (it == m_objectLocks.end() || it->second == msg.senderId) {
            m_objectLocks[netId] = msg.senderId;
            sendLockMessage(MessageType::LockGranted, netId);
        } else {
            sendLockMessage(MessageType::LockDenied, netId, "Locked by another player");
        }
    }

    void SessionManager::handleLockReply(const Message& msg, bool granted) {
        if (!m_session || !m_session->isConnected() || !isPeer()) return;

        auto parts = StringUtil::split(msg.payload, '|');
        if (parts.empty()) return;
        auto parsed = StringUtil::parseInt(parts[0]);
        if (!parsed || *parsed <= 0) return;

        NetworkObjectId netId = static_cast<NetworkObjectId>(*parsed);

        NetworkObjectId localId = netId;
        if ((netId >> 24) == myPlayerId() && !ObjectRegistry::get().hasNetworkId(netId)) {
            localId = netId & 0x00FFFFFF;
        }

        if (granted) {
            m_heldLocks.insert(localId);
            geode::log::info(
                "[EditorP2P] Lock granted canonical={} localId={} heldLocks.size={}",
                netId, localId, m_heldLocks.size()
            );
        } else {
            m_heldLocks.erase(localId);
            setStatus(parts.size() > 1 ? "Lock denied: " + parts[1] : "Lock denied");
            geode::log::info("[EditorP2P] Lock denied canonical={} localId={}", netId, localId);
        }
    }

    void SessionManager::handleCommitEdit(const Message& msg) {
        if (!m_session || !m_session->isConnected()) return;
        if (!peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canEdit)) {
            geode::log::warn("[EditorP2P] Rejected edit commit from unauthorized sender {}", msg.senderId);
            return;
        }

        auto decoded = MessageCodec::decodePlaceObject(msg.payload);
        if (!decoded) {
            geode::log::warn("[EditorP2P] Invalid edit commit: {}", decoded.error());
            return;
        }

        auto fields = decoded.value();
        NetworkObjectId netId = fields.tempClientId;
        if (isHost() && msg.senderId != HOST_PLAYER_ID &&
            !ObjectRegistry::get().hasNetworkId(netId)) {
            netId = canonicalPeerObjectId(msg.senderId, netId);
        } else if (isPeer() && (netId >> 24) == myPlayerId() &&
                   !ObjectRegistry::get().hasNetworkId(netId)) {
            auto legacyLocalId = netId & 0x00FFFFFF;
            if (ObjectRegistry::get().hasNetworkId(legacyLocalId)) {
                netId = legacyLocalId;
            }
        }

        auto opId = recordObjectOperation(
            EditorOperationType::CommitEdit,
            EditorOperationDirection::RemoteReceive,
            netId,
            fields,
            msg.senderId,
            msg.sequence,
            "tcp.commit_edit"
        );
        m_applyingRemote = true;
        EditorBridge::get().applyRemoteEdit(
            netId,
            fields.x,
            fields.y,
            fields.rotation,
            fields.scaleX,
            fields.scaleY,
            fields.saveString,
            false  // explicit params are authoritative for live edit commits
        );
        m_applyingRemote = false;
        geode::log::info(
            "[EditorP2P] Applied remote edit op={} id={} pos=({}, {}) rot={} scale=({}, {}) from peer={} {}",
            opId,
            netId,
            fields.x,
            fields.y,
            fields.rotation,
            fields.scaleX,
            fields.scaleY,
            msg.senderId,
            GroupMetadataParser::summarize(GroupMetadataParser::parseSaveString(fields.saveString))
        );
    }

    void SessionManager::handleUnlockObject(const Message& msg) {
        if (!m_session || !m_session->isConnected()) return;

        auto parsed = StringUtil::parseInt(msg.payload);
        if (!parsed || *parsed <= 0) return;

        NetworkObjectId netId = static_cast<NetworkObjectId>(*parsed);
        if (isHost() && msg.senderId != HOST_PLAYER_ID &&
            !ObjectRegistry::get().hasNetworkId(netId)) {
            netId = canonicalPeerObjectId(msg.senderId, netId);
        }

        m_objectLocks.erase(netId);
        m_heldLocks.erase(netId);
    }

    void SessionManager::handlePermissionUpdate(const Message& msg) {
        if (!m_session || !isPeer()) return;

        auto parsed = StringUtil::parseInt(msg.payload);
        if (!parsed || *parsed < 0 || *parsed > static_cast<int>(Role::Owner)) {
            geode::log::warn("[EditorP2P] Invalid permission update payload: {}", msg.payload);
            return;
        }

        auto role = static_cast<Role>(*parsed);
        m_session->setMyRole(role);
        ActivityLog::get().add(
            ActivityEventType::PermissionChanged,
            HOST_PLAYER_ID,
            0,
            "Your role changed to " + std::string(roleName(role))
        );
        setStatus("Role updated: " + std::string(roleName(role)));
    }

    void SessionManager::handleSaveRequest(const Message& msg) {
        if (!m_session || !isHost()) return;
        if (!peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canRequestSave) &&
            !peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canSave)) {
            geode::log::warn("[EditorP2P] Rejected save request from unauthorized sender {}", msg.senderId);
            return;
        }

        auto* peer = m_session->findPeer(msg.senderId);
        auto name = peer ? peer->displayName : ("peer " + std::to_string(msg.senderId));
        ActivityLog::get().add(
            ActivityEventType::SaveRequested,
            msg.senderId,
            0,
            "Save requested by " + name
        );
        setStatus("Save requested by " + name);

        if (peerHasPermission(*m_session, msg.senderId, &PermissionFlags::canSave)) {
            sendSaveCommand(msg.senderId);
            ActivityLog::get().add(
                ActivityEventType::SaveExecuted,
                HOST_PLAYER_ID,
                0,
                "Trusted peer save request acknowledged"
            );
        }
    }

    void SessionManager::handleSaveCommand(const Message& msg) {
        if (!m_session || !isPeer()) return;
        ActivityLog::get().add(
            ActivityEventType::SaveExecuted,
            msg.senderId,
            0,
            "Host acknowledged save"
        );
        setStatus("Host acknowledged save.");
    }

    void SessionManager::handleTransportDisconnect(std::string reason) {
        if (!m_session) return;
        disconnect(reason);
    }

    bool SessionManager::sendMessage(Message msg) {
        if (!m_controlTransport || !m_controlTransport->isConnected()) return false;
        auto bytes = MessageCodec::encode(msg);
        bool sent = m_controlTransport->send(bytes.data(), bytes.size());
        if (msg.type != MessageType::Heartbeat && msg.type != MessageType::PresenceUpdate) {
            geode::log::info(
                "[EditorP2P] TCP send type={} bytes={} ok={}",
                static_cast<uint16_t>(msg.type),
                bytes.size(),
                sent
            );
        }
        return sent;
    }

    bool SessionManager::sendHeartbeat(std::string payload) {
        if (!m_session || !m_session->isConnected()) return false;

        Message msg;
        msg.type = MessageType::Heartbeat;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = std::move(payload);
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendPresenceUpdate() {
        if (!m_session || !m_session->isConnected()) return false;

        MessageCodec::PresenceFields presence;
        presence.x = m_cursorX;
        presence.y = m_cursorY;
        presence.colorRgb = isHost() ? 0x50FF80u : 0x50B4FFu;
        presence.displayName = m_session->config().displayName;

        Message msg;
        msg.type = MessageType::PresenceUpdate;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = MessageCodec::encodePresence(presence);
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendPlaceObject(NetworkObjectId netId, const MessageCodec::PlaceObjectFields& fields) {
        if (!m_session || !m_session->isConnected()) return false;

        auto payloadFields = fields;
        payloadFields.tempClientId = netId;

        Message msg;
        msg.type = MessageType::PlaceObject;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = MessageCodec::encodePlaceObject(payloadFields);
        auto opId = recordObjectOperation(
            EditorOperationType::Place,
            EditorOperationDirection::LocalSend,
            netId,
            payloadFields,
            msg.senderId,
            msg.sequence,
            "tcp.place_object"
        );
        geode::log::info(
            "[EditorP2P] Sending placement op={} netId={} gdObjectId={} pos=({}, {}) {}",
            opId,
            netId,
            payloadFields.gdObjectId,
            payloadFields.x,
            payloadFields.y,
            GroupMetadataParser::summarize(
                GroupMetadataParser::parseSaveString(payloadFields.saveString)
            )
        );
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendDeleteObject(NetworkObjectId netId) {
        if (!m_session || !m_session->isConnected()) return false;

        Message msg;
        msg.type = MessageType::DeleteObject;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = std::to_string(netId);
        EditorOperation operation;
        operation.type = EditorOperationType::Delete;
        operation.direction = EditorOperationDirection::LocalSend;
        operation.netId = netId;
        operation.playerId = msg.senderId;
        operation.messageSequence = msg.sequence;
        operation.origin = "tcp.delete_object";
        auto opId = EditorOperationRecorder::get().record(std::move(operation));
        geode::log::info("[EditorP2P] Sending delete op={} netId={}", opId, netId);
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendPermissionUpdate(PlayerId peerId, Role role) {
        if (!m_session || !m_session->isConnected() || !isHost()) return false;

        Message msg;
        msg.type = MessageType::PermissionUpdate;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = std::to_string(static_cast<int>(role));
        geode::log::info(
            "[EditorP2P] Sending permission update peer={} role={}",
            peerId,
            roleName(role)
        );
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendSaveRequest() {
        if (!m_session || !m_session->isConnected() || !isPeer()) return false;

        Message msg;
        msg.type = MessageType::SaveRequest;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = "request";
        ActivityLog::get().add(ActivityEventType::SaveRequested, myPlayerId(), 0, "Save requested");
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendSaveCommand(PlayerId peerId) {
        if (!m_session || !m_session->isConnected() || !isHost()) return false;

        Message msg;
        msg.type = MessageType::SaveCommand;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = std::to_string(peerId);
        geode::log::info("[EditorP2P] Sending save command peer={}", peerId);
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendLockMessage(MessageType type, NetworkObjectId netId, std::string extra) {
        if (!m_session || !m_session->isConnected()) return false;

        Message msg;
        msg.type = type;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = std::to_string(netId);
        if (!extra.empty()) {
            msg.payload += "|" + extra;
        }
        return sendMessage(std::move(msg));
    }

    void SessionManager::handleStateResyncRequest(const Message& msg) {
        if (!m_session || !isHost()) return;
        geode::log::info("[EditorP2P] State resync requested by peer {}", msg.senderId);
        sendStateSnapshot();
    }

    bool SessionManager::sendStateResyncRequest() {
        Message msg;
        msg.type     = MessageType::StateResyncRequest;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload  = "request";
        return sendMessage(std::move(msg));
    }

    bool SessionManager::sendStateSnapshot() {
        auto* lel = LevelEditorLayer::get();
        if (!lel) {
            geode::log::warn("[EditorP2P] sendStateSnapshot: no active editor");
            return false;
        }

        std::vector<MessageCodec::SnapshotObjectEntry> entries;
        std::unordered_set<void*> liveObjects;
        if (lel->m_objects) {
            const auto count = lel->m_objects->count();
            for (unsigned int i = 0; i < count; ++i) {
                auto* obj = static_cast<GameObject*>(lel->m_objects->objectAtIndex(i));
                if (obj) {
                    liveObjects.insert(obj);
                    ObjectRegistry::get().registerObject(obj);
                    m_knownLocalObjects.insert(obj);
                }
            }
        }

        std::vector<NetworkObjectId> staleIds;
        ObjectRegistry::get().forEach([&](NetworkObjectId netId, void* ptr) {
            if (!ptr || liveObjects.find(ptr) == liveObjects.end()) {
                staleIds.push_back(netId);
            }
        });
        for (auto netId : staleIds) {
            ObjectRegistry::get().removeByNetworkId(netId);
        }

        ObjectRegistry::get().forEach([&](NetworkObjectId netId, void* ptr) {
            auto* obj = static_cast<GameObject*>(ptr);
            if (!obj) return;
            MessageCodec::SnapshotObjectEntry entry;
            entry.netId      = netId;
            entry.saveString = obj->getSaveString(lel);
            entries.push_back(std::move(entry));
        });

        geode::log::info("[EditorP2P] Building snapshot: {} objects", entries.size());

        std::vector<std::vector<MessageCodec::SnapshotObjectEntry>> chunks;
        std::vector<MessageCodec::SnapshotObjectEntry> current;
        size_t currentBytes = 0;

        for (auto& entry : entries) {
            size_t recordBytes = std::to_string(entry.netId).size() + 1 +
                                 entry.saveString.size() + 1;
            if (!current.empty() &&
                currentBytes + recordBytes > MessageCodec::SNAPSHOT_CHUNK_BUDGET) {
                chunks.push_back(std::move(current));
                current.clear();
                currentBytes = 0;
            }
            currentBytes += recordBytes;
            current.push_back(std::move(entry));
        }
        chunks.push_back(std::move(current));

        auto totalChunks = static_cast<uint32_t>(chunks.size());
        bool ok = true;
        for (uint32_t i = 0; i < totalChunks; ++i) {
            MessageCodec::SnapshotChunkFields f;
            f.chunkIndex  = i;
            f.totalChunks = totalChunks;
            f.objects     = std::move(chunks[i]);

            Message msg;
            msg.type     = MessageType::StateSnapshot;
            msg.senderId = myPlayerId();
            msg.sequence = m_controlSequence++;
            msg.payload  = MessageCodec::encodeSnapshotChunk(f);
            if (!sendMessage(std::move(msg))) ok = false;
        }

        geode::log::info("[EditorP2P] Snapshot sent in {} chunk(s)", totalChunks);
        return ok;
    }

    void SessionManager::handleStateSnapshot(const Message& msg) {
        if (!m_session || !isConnected()) return;
        if (!isPeer()) {
            geode::log::warn("[EditorP2P] Ignoring state snapshot while not in peer mode");
            return;
        }

        auto decoded = MessageCodec::decodeSnapshotChunk(msg.payload);
        if (!decoded) {
            geode::log::warn("[EditorP2P] Bad snapshot chunk: {}", decoded.error());
            return;
        }

        auto& chunk = decoded.value();
        geode::log::info("[EditorP2P] Snapshot chunk {}/{}: {} objects",
            chunk.chunkIndex + 1, chunk.totalChunks, chunk.objects.size());

        if (chunk.chunkIndex == 0) {
            m_snapshotEntries.clear();
            m_snapshotTotalChunks    = chunk.totalChunks;
            m_snapshotChunksReceived = 0;
        }

        for (auto& entry : chunk.objects) {
            m_snapshotEntries.push_back(std::move(entry));
        }
        ++m_snapshotChunksReceived;

        if (m_snapshotChunksReceived == m_snapshotTotalChunks) {
            applyStateSnapshot();
        }
    }

    void SessionManager::applyStateSnapshot() {
        auto* lel = LevelEditorLayer::get();
        if (!lel) {
            geode::log::warn("[EditorP2P] applyStateSnapshot: no active editor");
            m_snapshotEntries.clear();
            return;
        }

        geode::log::info("[EditorP2P] Applying snapshot: {} objects", m_snapshotEntries.size());

        std::unordered_set<NetworkObjectId> snapshotIds;
        for (const auto& entry : m_snapshotEntries) {
            snapshotIds.insert(entry.netId);
        }

        m_applyingRemote = true;

        // Remove locally-tracked objects not present in the snapshot.
        std::vector<NetworkObjectId> toRemove;
        ObjectRegistry::get().forEach([&](NetworkObjectId netId, void*) {
            if (snapshotIds.find(netId) == snapshotIds.end()) {
                toRemove.push_back(netId);
            }
        });
        for (auto netId : toRemove) {
            EditorOperation operation;
            operation.type = EditorOperationType::SnapshotDelete;
            operation.direction = EditorOperationDirection::LocalApply;
            operation.netId = netId;
            operation.playerId = myPlayerId();
            operation.origin = "snapshot.apply";
            auto opId = EditorOperationRecorder::get().record(std::move(operation));
            geode::log::info("[EditorP2P] Snapshot delete op={} netId={}", opId, netId);
            EditorBridge::get().applyRemoteDeletion(netId);
            ObjectRegistry::get().removeByNetworkId(netId);
        }

        // A host push is authoritative. Objects that exist locally but are not
        // registered cannot be matched to any snapshot entry, so remove them to
        // avoid peer-side leftovers and duplicate state after resync.
        std::vector<GameObject*> untrackedLiveObjects;
        if (lel->m_objects) {
            const auto count = lel->m_objects->count();
            untrackedLiveObjects.reserve(count);
            for (unsigned int i = 0; i < count; ++i) {
                auto* obj = static_cast<GameObject*>(lel->m_objects->objectAtIndex(i));
                if (obj && ObjectRegistry::get().findNetworkId(obj) == INVALID_OBJECT_ID) {
                    untrackedLiveObjects.push_back(obj);
                }
            }
        }
        for (auto* obj : untrackedLiveObjects) {
            auto pos = obj->getPosition();
            geode::log::info(
                "[EditorP2P] Snapshot removing untracked local object obj={} gdObjectId={} pos=({}, {})",
                static_cast<void*>(obj),
                obj->m_objectID,
                pos.x,
                pos.y
            );
            lel->removeObject(obj, true);
            m_knownLocalObjects.erase(obj);
            m_baselineLocalObjects.erase(obj);
        }

        // Create missing objects and refresh existing tracked objects from the snapshot.
        int created = 0, updated = 0;
        for (const auto& entry : m_snapshotEntries) {
            auto netId = entry.netId;
            MessageCodec::PlaceObjectFields snapshotFields;
            snapshotFields.tempClientId = netId;
            snapshotFields.gdObjectId = 0;
            snapshotFields.x = 0.f;
            snapshotFields.y = 0.f;
            snapshotFields.rotation = 0.f;
            snapshotFields.scaleX = 1.f;
            snapshotFields.scaleY = 1.f;
            snapshotFields.saveString = entry.saveString;
            if (ObjectRegistry::get().hasNetworkId(netId)) {
                auto opId = recordObjectOperation(
                    EditorOperationType::SnapshotUpdate,
                    EditorOperationDirection::LocalApply,
                    netId,
                    snapshotFields,
                    myPlayerId(),
                    0,
                    "snapshot.apply"
                );
                geode::log::info(
                    "[EditorP2P] Snapshot update op={} netId={} {}",
                    opId,
                    netId,
                    GroupMetadataParser::summarize(
                        GroupMetadataParser::parseSaveString(snapshotFields.saveString)
                    )
                );
                EditorBridge::get().applyRemoteEdit(
                    netId,
                    0.f, 0.f, 0.f, 1.f, 1.f,
                    entry.saveString,
                    true  // snapshot: derive transform from saveString, not fake zeros
                );
                ++updated;
                continue;
            }
            auto opId = recordObjectOperation(
                EditorOperationType::SnapshotCreate,
                EditorOperationDirection::LocalApply,
                netId,
                snapshotFields,
                myPlayerId(),
                0,
                "snapshot.apply"
            );
            geode::log::info(
                "[EditorP2P] Snapshot create op={} netId={} {}",
                opId,
                netId,
                GroupMetadataParser::summarize(
                    GroupMetadataParser::parseSaveString(snapshotFields.saveString)
                )
            );
            void* obj = EditorBridge::get().applyRemotePlacement(
                netId,
                0,             // gdObjectId — save string takes precedence
                0.f, 0.f, 0.f, 1.f, 1.f,
                entry.saveString
            );
            if (obj) {
                ObjectRegistry::get().bindObject(netId, obj);
                ++created;
            }
        }

        m_applyingRemote = false;
        m_objectLocks.clear();
        m_heldLocks.clear();

        geode::log::info("[EditorP2P] Snapshot applied: {} created, {} updated, {} removed, {} untracked removed",
            created,
            updated,
            static_cast<int>(toRemove.size()),
            static_cast<int>(untrackedLiveObjects.size()));
        setStatus("State synced: " + std::to_string(created) + " objects received, " +
                  std::to_string(updated) + " updated.");

        m_snapshotEntries.clear();
        m_snapshotTotalChunks    = 0;
        m_snapshotChunksReceived = 0;
    }

    bool SessionManager::sendCommitEdit(NetworkObjectId netId) {
        if (!m_session || !m_session->isConnected()) return false;

        auto* object = static_cast<GameObject*>(ObjectRegistry::get().findByNetworkId(netId));
        if (!object) return false;

        MessageCodec::PlaceObjectFields fields;
        fields.tempClientId = netId;
        fields.gdObjectId = static_cast<uint16_t>(object->m_objectID);
        auto pos = object->getPosition();
        fields.x = pos.x;
        fields.y = pos.y;
        fields.rotation = object->getRotation();
        fields.scaleX = object->m_scaleX;
        fields.scaleY = object->m_scaleY;
        if (auto* lel = LevelEditorLayer::get()) {
            fields.saveString = object->getSaveString(lel);
        }

        Message msg;
        msg.type = MessageType::CommitEdit;
        msg.senderId = myPlayerId();
        msg.sequence = m_controlSequence++;
        msg.payload = MessageCodec::encodePlaceObject(fields);
        auto opId = recordObjectOperation(
            EditorOperationType::CommitEdit,
            EditorOperationDirection::LocalSend,
            netId,
            fields,
            msg.senderId,
            msg.sequence,
            "tcp.commit_edit"
        );

        geode::log::info(
            "[EditorP2P] Sending edit commit op={} id={} pos=({}, {}) rot={} "
            "m_scaleX/Y=({}, {}) m_customScaleX/Y=({}, {}) sent=({}, {}) {}",
            opId,
            netId,
            fields.x, fields.y,
            fields.rotation,
            object->m_scaleX, object->m_scaleY,
            object->m_customScaleX, object->m_customScaleY,
            fields.scaleX, fields.scaleY,
            GroupMetadataParser::summarize(
                GroupMetadataParser::parseSaveString(fields.saveString)
            )
        );
        return sendMessage(std::move(msg));
    }

#endif

    bool SessionManager::hasPermission(PermissionFlags flag) const {
        if (!m_session) return false;
        auto own = permissionsForRole(m_session->myRole());
        return
            (!flag.canEdit || own.canEdit) &&
            (!flag.canDelete || own.canDelete) &&
            (!flag.canLock || own.canLock) &&
            (!flag.canRequestSave || own.canRequestSave) &&
            (!flag.canSave || own.canSave) &&
            (!flag.canInvite || own.canInvite);
    }

} // namespace ep2p
