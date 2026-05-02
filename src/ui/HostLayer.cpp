#include <EditorP2P/ui/HostLayer.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/config/RuntimeConfig.hpp>
#include <EditorP2P/net/JoinCode.hpp>
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
            char hostname[256] = {};
            if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
                return "127.0.0.1";
            }

            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            addrinfo* results = nullptr;
            if (getaddrinfo(hostname, nullptr, &hints, &results) != 0 || !results) {
                return "127.0.0.1";
            }

            std::string fallback = "127.0.0.1";
            for (auto* entry = results; entry; entry = entry->ai_next) {
                auto* addr = reinterpret_cast<sockaddr_in*>(entry->ai_addr);
                char ip[INET_ADDRSTRLEN] = {};
                if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) continue;

                std::string candidate = ip;
                if (candidate.rfind("127.", 0) != 0) {
                    freeaddrinfo(results);
                    return candidate;
                }
                fallback = candidate;
            }

            freeaddrinfo(results);
            return fallback;
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

        JoinCode code;
        code.endpoint.host = getLocalJoinAddress();
        code.endpoint.port = runtime.hostPort;
        code.sessionKey = config.sessionKey;

        m_joinCodeValue = code.format();
        m_joinCodeText = "Join code: " + m_joinCodeValue;
        setStatus("Room created.\n" + m_joinCodeText + "\nWaiting for peer.");
    }

    void HostLayer::onDisconnect(CCObject*) {
        SessionManager::get().disconnect("Host stopped from UI");
        m_joinCodeText.clear();
        m_joinCodeValue.clear();
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
