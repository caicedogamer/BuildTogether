#include <EditorP2P/ui/HostLayer.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/config/RuntimeConfig.hpp>
#include <EditorP2P/net/JoinCode.hpp>
#include <EditorP2P/net/Discovery.hpp>
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
#include <thread>

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

    void HostLayer::fetchPublicIP() {
        m_fetchingIP = true;
        this->retain();

        std::thread([this]() {
            std::string ip;
#ifdef EP2P_WINDOWS
            SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock != INVALID_SOCKET) {
                addrinfo hints{}, *res = nullptr;
                hints.ai_family   = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (getaddrinfo("api.ipify.org", "80", &hints, &res) == 0 && res) {
                    if (connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0) {
                        const char* req =
                            "GET / HTTP/1.0\r\n"
                            "Host: api.ipify.org\r\n"
                            "Connection: close\r\n\r\n";
                        send(sock, req, static_cast<int>(strlen(req)), 0);

                        char buf[256] = {};
                        std::string raw;
                        int n;
                        while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
                            buf[n] = '\0';
                            raw += buf;
                        }
                        auto pos = raw.find("\r\n\r\n");
                        if (pos != std::string::npos)
                            ip = raw.substr(pos + 4);
                        // trim whitespace
                        while (!ip.empty() && (ip.back() == '\r' || ip.back() == '\n' || ip.back() == ' '))
                            ip.pop_back();
                    }
                    freeaddrinfo(res);
                }
                closesocket(sock);
            }
#endif
            geode::Loader::get()->queueInMainThread([this, ip]() {
                m_fetchingIP = false;
                m_publicIP = ip;
                refreshCodeDisplay();
                this->release();
            });
        }).detach();
    }

    void HostLayer::refreshCodeDisplay() {
        if (m_sessionKey.empty()) return;
        auto& runtime = RuntimeConfig::get();

        std::string lanLine  = "LAN: " + m_sessionKey;
        std::string inetLine;
        if (m_fetchingIP) {
            inetLine = "Internet: fetching IP...";
        } else if (!m_publicIP.empty()) {
            std::string fullCode = m_publicIP + ":" + std::to_string(runtime.hostPort)
                                   + "#" + m_sessionKey;
            inetLine       = "Internet: " + fullCode;
            m_joinCodeValue = fullCode;
        } else {
            inetLine = "Internet: could not fetch IP";
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
        m_publicIP.clear();

        setStatus("Room created.\nLAN: " + m_sessionKey + "\nInternet: fetching IP...\nWaiting for peer.");
        fetchPublicIP();
    }

    void HostLayer::onDisconnect(CCObject*) {
        s_broadcaster.stop();
        SessionManager::get().disconnect("Host stopped from UI");
        m_joinCodeText.clear();
        m_joinCodeValue.clear();
        m_sessionKey.clear();
        m_publicIP.clear();
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
