#pragma once

#include <EditorP2P/core/Session.hpp>
#include <EditorP2P/core/Types.hpp>
#include <EditorP2P/protocol/Message.hpp>
#include <EditorP2P/protocol/MessageCodec.hpp>
#include <EditorP2P/util/Result.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace ep2p {

#ifdef EP2P_WINDOWS
    class WinTcpTransport;
#endif

    // Central controller. Owns the active Session and the networking back-end.
    // UI and editor hooks talk to SessionManager exclusively — never to net layer directly.
    //
    // Threading: all public methods (except postToMainThread) must be called on the main
    // (Cocos) thread. Network threads call postToMainThread to queue work.
    class SessionManager {
    public:
        static SessionManager& get();

        // Lifecycle
        Result<void> startHost(const SessionConfig& config);
        Result<void> joinAsPeer(const SessionConfig& config);
        void         disconnect(const std::string& reason = "");

        // State queries (main thread)
        SessionState  state()       const;
        SessionMode   mode()        const;
        bool          isConnected() const;
        bool          isHost()      const;
        bool          isPeer()      const;
        Role          myRole()      const;
        PlayerId      myPlayerId()  const;
        std::string   statusText() const;
        uint32_t      statusRevision() const;
        std::string   roomSummary() const;

        // Active session accessor — nullptr when Idle.
        Session* activeSession() { return m_session.get(); }
        const Session* activeSession() const { return m_session.get(); }

        // Called by editor hooks on the main thread.
        // objectKey : key passed to LevelEditorLayer::createObject (authoritative palette ID).
        // requestedX/Y : the position parameter from createObject — more reliable than
        //                reading object->getPosition() post-creation, which may differ
        //                due to per-object origin offsets GD applies internally.
        void onLocalObjectPlaced(void* gdObject, int objectKey, float requestedX, float requestedY);
        void onLocalObjectDiscovered(void* gdObject, int objectKey, float x, float y);
        void markLocalObjectKnown(void* gdObject);
        void onLocalObjectDeleted(uint32_t netId);
        void onLocalObjectSelected(uint32_t netId);
        void onLocalObjectEdited(uint32_t netId);
        void onLocalObjectDeselected(uint32_t netId);
        void onLocalSaveRequested();
        void onCursorMoved(float worldX, float worldY);
        bool setPeerRole(PlayerId peerId, Role role);

        // Trigger a full state resync.
        // Host: pushes current object state to the peer.
        // Peer: requests a snapshot from the host.
        void requestStateResync();

        // Called by Cocos scheduler every frame.
        void tick(float dt);

        // Thread-safe — called from net threads to schedule work on the main thread.
        void postToMainThread(std::function<void()> fn);

        // Permission check helpers (use myRole() for self, or check a peer by ID).
        bool hasPermission(PermissionFlags flag) const;  // for own role

    private:
        SessionManager() = default;
        ~SessionManager();

        void drainQueue();
        void setStatus(std::string text);

#ifdef EP2P_WINDOWS
        Result<void> openHostListener(const SessionConfig& config);
        void closeHostListener();
        void acceptLoop();
        void adoptAcceptedSocket(uintptr_t rawSocket);
        void configureTransportCallbacks();
        void handleTcpBytes(std::vector<uint8_t> bytes);
        void handleMessage(const Message& msg);
        void handleHostHello(const Message& msg);
        void handlePeerHelloAck(const Message& msg);
        void handleHeartbeat(const Message& msg);
        void handlePresenceUpdate(const Message& msg);
        void handlePlaceObject(const Message& msg);
        void handleDeleteObject(const Message& msg);
        void handleLockRequest(const Message& msg);
        void handleLockReply(const Message& msg, bool granted);
        void handleCommitEdit(const Message& msg);
        void handleUnlockObject(const Message& msg);
        void handlePermissionUpdate(const Message& msg);
        void handleSaveRequest(const Message& msg);
        void handleSaveCommand(const Message& msg);
        void handleTransportDisconnect(std::string reason);
        void handleStateResyncRequest(const Message& msg);
        void handleStateSnapshot(const Message& msg);
        void applyStateSnapshot();
        bool sendMessage(Message msg);
        bool sendHeartbeat(std::string payload);
        bool sendPresenceUpdate();
        bool sendPlaceObject(NetworkObjectId netId, const MessageCodec::PlaceObjectFields& fields);
        bool sendDeleteObject(NetworkObjectId netId);
        bool sendPermissionUpdate(PlayerId peerId, Role role);
        bool sendSaveRequest();
        bool sendSaveCommand(PlayerId peerId);
        bool sendLockMessage(MessageType type, NetworkObjectId netId, std::string extra = {});
        bool sendCommitEdit(NetworkObjectId netId);
        bool sendStateResyncRequest();
        bool sendStateSnapshot();
#endif

        std::unique_ptr<Session> m_session;

        // Main-thread dispatch queue
        std::mutex                        m_queueMutex;
        std::queue<std::function<void()>> m_queue;

        mutable std::mutex m_statusMutex;
        std::string        m_statusText = "Idle";
        uint32_t           m_statusRevision = 0;

        // Cursor send accumulator
        float    m_cursorAccum  = 0.f;
        float    m_heartbeatAccum = 0.f;
        float    m_cursorX = 0.f;
        float    m_cursorY = 0.f;
        uint32_t m_presenceSeq  = 0;
        TimestampMs m_lastHeartbeatMs = 0;
        bool     m_applyingRemote = false;  // guard against hook re-fire loops
        std::unordered_set<void*> m_knownLocalObjects;
        std::unordered_map<void*, std::string> m_baselineLocalObjects;

        // Snapshot reassembly (peer side)
        std::vector<MessageCodec::SnapshotObjectEntry> m_snapshotEntries;
        uint32_t m_snapshotTotalChunks    = 0;
        uint32_t m_snapshotChunksReceived = 0;

        // Pending placements: provisional tempId -> gdObject*
        uint32_t m_nextTempId = 1;

#ifdef EP2P_WINDOWS
        std::unique_ptr<WinTcpTransport> m_controlTransport;
        uintptr_t         m_listenSocket = 0;
        std::atomic<bool> m_acceptRunning = false;
        std::thread       m_acceptThread;
        uint32_t          m_controlSequence = 1;
        MessageCodec      m_controlCodec;
        std::unordered_map<NetworkObjectId, PlayerId> m_objectLocks;
        std::unordered_set<NetworkObjectId> m_heldLocks;
#endif
    };

    // Convenience alias so hooks can write SessionMgr::get() after including this header.
    using SessionMgr = SessionManager;

} // namespace ep2p
