#include <EditorP2P/ui/HostLayer.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/config/RuntimeConfig.hpp>
#include <EditorP2P/net/JoinCode.hpp>
#include <EditorP2P/net/Discovery.hpp>
#include <EditorP2P/net/BoreRelay.hpp>
#include <EditorP2P/ui/ActivityLogLayer.hpp>
#include <EditorP2P/ui/PermissionsLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/Geode.hpp>
#include <Geode/utils/general.hpp>

#ifdef EP2P_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using namespace geode::prelude;
using namespace ep2p;

namespace ep2p {
    namespace {
        std::string getLocalJoinAddress() {
#ifdef EP2P_WINDOWS
            // Use the UDP connect trick: the OS picks the outbound interface
            // for a route to 8.8.8.8 without sending any traffic. This avoids
            // returning a VirtualBox or VPN adapter ahead of the real LAN IP.
            SOCKET probe = socket(AF_INET, SOCK_DGRAM, 0);
            if (probe != INVALID_SOCKET) {
                sockaddr_in dest{};
                dest.sin_family = AF_INET;
                dest.sin_port = htons(53);
                inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);
                if (connect(probe, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == 0) {
                    sockaddr_in local{};
                    int localLen = sizeof(local);
                    if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &localLen) == 0) {
                        char ip[INET_ADDRSTRLEN] = {};
                        if (inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip))) {
                            closesocket(probe);
                            return ip;
                        }
                    }
                }
                closesocket(probe);
            }

            return "127.0.0.1";
#else
            return "127.0.0.1";
#endif
        }
    }

    HostLayer* HostLayer::create() {
        auto* obj = new HostLayer();
        if (obj && obj->init(340.f, 220.f) && obj->setup()) {
            obj->autorelease();
            return obj;
        }
        CC_SAFE_DELETE(obj);
        return nullptr;
    }

    bool HostLayer::setup() {
        this->setTitle("Host Session");

        auto size = this->m_mainLayer->getContentSize();

        m_statusLabel = CCLabelBMFont::create("Ready to host a local EditorP2P room.", "bigFont.fnt");
        m_statusLabel->setScale(0.34f);
        m_statusLabel->setWidth(285.f);
        m_statusLabel->setAlignment(kCCTextAlignmentCenter);
        m_statusLabel->setPosition({size.width / 2.f, size.height / 2.f + 34.f});
        this->m_mainLayer->addChild(m_statusLabel);

        auto* menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height / 2.f - 34.f});
        this->m_mainLayer->addChild(menu);

        auto* startSprite = ButtonSprite::create("Host", 72, 0, 0.44f, true);
        auto* startBtn = CCMenuItemSpriteExtra::create(
            startSprite,
            this,
            menu_selector(HostLayer::onStartHost)
        );
        startBtn->setPosition({-92.f, 0.f});
        menu->addChild(startBtn);

        auto* copySprite = ButtonSprite::create("Copy", 72, 0, 0.44f, true);
        auto* copyBtn = CCMenuItemSpriteExtra::create(
            copySprite,
            this,
            menu_selector(HostLayer::onCopyCode)
        );
        copyBtn->setPosition({0.f, 0.f});
        menu->addChild(copyBtn);

        auto* pushSprite = ButtonSprite::create("Push", 72, 0, 0.44f, true);
        auto* pushBtn = CCMenuItemSpriteExtra::create(
            pushSprite,
            this,
            menu_selector(HostLayer::onPushState)
        );
        pushBtn->setPosition({92.f, 0.f});
        menu->addChild(pushBtn);

        auto* stopSprite = ButtonSprite::create("Stop", 72, 0, 0.44f, true);
        auto* stopBtn = CCMenuItemSpriteExtra::create(
            stopSprite,
            this,
            menu_selector(HostLayer::onDisconnect)
        );
        stopBtn->setPosition({-92.f, -26.f});
        menu->addChild(stopBtn);

        auto* permissionsBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Roles", 72, 0, 0.44f, true),
            this,
            menu_selector(HostLayer::onPermissions)
        );
        permissionsBtn->setPosition({0.f, -26.f});
        menu->addChild(permissionsBtn);

        auto* logBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Log", 72, 0, 0.44f, true),
            this,
            menu_selector(HostLayer::onActivityLog)
        );
        logBtn->setPosition({92.f, -26.f});
        menu->addChild(logBtn);

        this->schedule(schedule_selector(HostLayer::statusTick), 0.25f);

        return true;
    }

    static LanBroadcaster s_broadcaster;
    static BoreRelay      s_relay;

    void HostLayer::startRelay() {
        m_relayConnecting = true;
        this->retain();

        s_relay.start(
            RuntimeConfig::get().hostPort,
            // onReady — called on main thread
            [this](std::string addr) {
                m_relayConnecting = false;
                m_relayAddr       = addr;
                refreshCodeDisplay();
                this->release();
            },
            // onStop — called on main thread
            [this](std::string reason) {
                m_relayConnecting = false;
                if (m_relayAddr.empty()) {
                    // Failed before ready — show error in status
                    setStatus("Relay failed: " + reason
                              + "\nLAN: " + m_sessionKey
                              + "\nWaiting for peer.");
                    this->release();
                }
                // If it was already ready, a mid-session drop is handled by
                // the existing disconnect flow; don't touch the UI here.
            }
        );
    }

    void HostLayer::refreshCodeDisplay() {
        if (m_sessionKey.empty()) return;

        std::string lanLine  = "LAN:      " + m_sessionKey;
        std::string inetLine;
        if (m_relayConnecting) {
            inetLine = "Internet: connecting to relay...";
        } else if (!m_relayAddr.empty()) {
            std::string fullCode = m_relayAddr + "#" + m_sessionKey;
            inetLine        = "Internet: " + fullCode;
            m_joinCodeValue = fullCode;
        } else {
            inetLine = "Internet: relay unavailable";
        }

        m_joinCodeText = lanLine + "\n" + inetLine;
        setStatus("Room created.\n" + m_joinCodeText + "\nWaiting for peer.");
    }

    void HostLayer::onStartHost(CCObject*) {
        auto& runtime = RuntimeConfig::get();

        SessionConfig config;
        config.mode = SessionMode::Host;
        config.displayName = runtime.displayName;
        config.roomName = runtime.displayName + "'s Room";
        config.hostPort = runtime.hostPort;
        config.sessionKey = JoinCode::generateKey();

        auto result = SessionManager::get().startHost(config);
        if (!result) {
            setStatus("Could not start hosting:\n" + result.error());
            return;
        }

        DiscoveryInfo info;
        info.roomName        = config.roomName;
        info.hostName        = config.displayName;
        info.sessionKey      = config.sessionKey;
        info.endpoint.host   = getLocalJoinAddress();
        info.endpoint.port   = runtime.hostPort;
        info.protocolVersion = 1;
        s_broadcaster.stop();
        s_broadcaster.start(info);

        m_sessionKey    = config.sessionKey;
        m_joinCodeValue = config.sessionKey;
        m_relayAddr.clear();

        setStatus("Room created.\nLAN: " + m_sessionKey + "\nInternet: connecting to relay...\nWaiting for peer.");
        startRelay();
    }

    void HostLayer::onDisconnect(CCObject*) {
        s_broadcaster.stop();
        s_relay.stop();
        SessionManager::get().disconnect("Host stopped from UI");
        m_joinCodeText.clear();
        m_joinCodeValue.clear();
        m_sessionKey.clear();
        m_relayAddr.clear();
        setStatus("Disconnected. Ready to host again.");
    }

    void HostLayer::onPushState(CCObject*) {
        if (!SessionManager::get().isConnected()) {
            setStatus("No peer connected. Start hosting first.");
            return;
        }
        setStatus("Pushing state to peer...");
        SessionManager::get().requestStateResync();
    }

    void HostLayer::onCopyCode(CCObject*) {
        if (m_joinCodeValue.empty()) {
            setStatus("Start hosting first, then copy the join code.");
            return;
        }

        if (geode::utils::clipboard::write(m_joinCodeValue)) {
            setStatus("Copied join code:\n" + m_joinCodeValue);
        } else {
            setStatus("Could not copy join code.\n" + m_joinCodeValue);
        }
    }

    void HostLayer::onPermissions(CCObject*) {
        if (auto* layer = PermissionsLayer::create()) {
            layer->show();
        }
    }

    void HostLayer::onActivityLog(CCObject*) {
        if (auto* layer = ActivityLogLayer::create()) {
            layer->show();
        }
    }

    void HostLayer::statusTick(float) {
        auto& manager = SessionManager::get();
        manager.tick(0.f);

        auto revision = manager.statusRevision();
        auto status = manager.roomSummary();
        if (revision == m_seenStatusRevision && status == m_lastRenderedStatus) return;
        m_seenStatusRevision = revision;
        if (!m_joinCodeText.empty()) {
            status = m_joinCodeText + "\n" + status;
        }
        m_lastRenderedStatus = status;
        setStatus(status);
    }

    void HostLayer::setStatus(const std::string& text) {
        if (m_statusLabel) {
            m_statusLabel->setString(text.c_str());
        }
    }

} // namespace ep2p
