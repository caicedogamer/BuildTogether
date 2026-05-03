#include <EditorP2P/ui/JoinLayer.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/config/RuntimeConfig.hpp>
#include <EditorP2P/net/JoinCode.hpp>
#include <EditorP2P/net/Discovery.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <cctype>
#include <thread>

using namespace geode::prelude;
using namespace ep2p;

namespace ep2p {

    JoinLayer* JoinLayer::create() {
        auto* obj = new JoinLayer();
        if (obj && obj->init(380.f, 230.f) && obj->setup()) {
            obj->autorelease();
            return obj;
        }
        CC_SAFE_DELETE(obj);
        return nullptr;
    }

    bool JoinLayer::setup() {
        this->setTitle("Join Session");

        auto size = this->m_mainLayer->getContentSize();

        m_joinCodeInput = geode::TextInput::create(290.f, "XXXX-XXXX");
        m_joinCodeInput->setPosition({size.width / 2.f, size.height / 2.f + 38.f});
        m_joinCodeInput->setCommonFilter(geode::CommonFilter::Any);
        m_joinCodeInput->setMaxCharCount(80);
        this->m_mainLayer->addChild(m_joinCodeInput);

        m_statusLabel = CCLabelBMFont::create("Enter the host's join code, then press Join.", "bigFont.fnt");
        m_statusLabel->setScale(0.32f);
        m_statusLabel->setWidth(310.f);
        m_statusLabel->setAlignment(kCCTextAlignmentCenter);
        m_statusLabel->setPosition({size.width / 2.f, size.height / 2.f - 12.f});
        this->m_mainLayer->addChild(m_statusLabel);

        auto* menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height / 2.f - 62.f});
        this->m_mainLayer->addChild(menu);

        auto* joinSprite = ButtonSprite::create("Join", 72, 0, 0.44f, true);
        auto* joinBtn = CCMenuItemSpriteExtra::create(
            joinSprite,
            this,
            menu_selector(JoinLayer::onJoin)
        );
        joinBtn->setPosition({-46.f, 0.f});
        menu->addChild(joinBtn);

        auto* disconnectSprite = ButtonSprite::create("Disc.", 72, 0, 0.44f, true);
        auto* disconnectBtn = CCMenuItemSpriteExtra::create(
            disconnectSprite,
            this,
            menu_selector(JoinLayer::onDisconnect)
        );
        disconnectBtn->setPosition({46.f, 0.f});
        menu->addChild(disconnectBtn);

        auto* resyncSprite = ButtonSprite::create("Resync", 72, 0, 0.44f, true);
        auto* resyncBtn = CCMenuItemSpriteExtra::create(
            resyncSprite,
            this,
            menu_selector(JoinLayer::onResync)
        );
        resyncBtn->setPosition({-46.f, -26.f});
        menu->addChild(resyncBtn);

        auto* saveSprite = ButtonSprite::create("Save", 72, 0, 0.44f, true);
        auto* saveBtn = CCMenuItemSpriteExtra::create(
            saveSprite,
            this,
            menu_selector(JoinLayer::onSaveRequest)
        );
        saveBtn->setPosition({46.f, -26.f});
        menu->addChild(saveBtn);

        this->schedule(schedule_selector(JoinLayer::statusTick), 0.25f);

        return true;
    }

    void JoinLayer::connectWith(const JoinCode& code) {
        auto& runtime = RuntimeConfig::get();
        SessionConfig config;
        config.mode = SessionMode::Peer;
        config.displayName = runtime.displayName;
        config.remoteEndpoint = code.endpoint;
        config.sessionKey = code.sessionKey;

        auto result = SessionManager::get().joinAsPeer(config);
        if (!result) {
            setStatus("Could not join:\n" + result.error());
            return;
        }

        setStatus("Connecting to " + code.endpoint.str());
    }

    void JoinLayer::onJoin(CCObject*) {
        if (!m_joinCodeInput || m_scanning) return;

        std::string raw = m_joinCodeInput->getString();

        // Full format: IP:port#XXXX-XXXX — connect directly.
        if (auto parsed = JoinCode::parse(raw)) {
            connectWith(*parsed);
            return;
        }

        // Bare key: XXXX-XXXX — scan LAN to find the host IP.
        std::string key = raw;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        if (key.size() != 9 || key[4] != '-') {
            setStatus("Invalid code.\nEnter XXXX-XXXX or IP:port#XXXX-XXXX.");
            return;
        }

        setStatus("Scanning LAN for host...");
        m_scanning = true;
        this->retain();

        std::thread([this, key]() {
            LanScanner scanner;
            std::optional<DiscoveryInfo> found;
            scanner.scan(3000, [&](DiscoveryInfo info) {
                if (!found && info.sessionKey == key) {
                    found = info;
                }
            });

            geode::Loader::get()->queueInMainThread([this, found, key]() {
                m_scanning = false;
                if (!found) {
                    setStatus("No host found on LAN.\nMake sure the host is running.");
                    this->release();
                    return;
                }
                JoinCode code;
                code.endpoint   = found->endpoint;
                code.sessionKey = key;
                connectWith(code);
                this->release();
            });
        }).detach();
    }

    void JoinLayer::onDisconnect(CCObject*) {
        SessionManager::get().disconnect("Peer stopped from UI");
        setStatus("Disconnected. Ready to join again.");
    }

    void JoinLayer::onResync(CCObject*) {
        if (!SessionManager::get().isConnected()) {
            setStatus("Not connected. Join a session first.");
            return;
        }
        setStatus("Requesting state resync from host...");
        SessionManager::get().requestStateResync();
    }

    void JoinLayer::onSaveRequest(CCObject*) {
        if (!SessionManager::get().isConnected()) {
            setStatus("Not connected. Join a session first.");
            return;
        }
        SessionManager::get().onLocalSaveRequested();
        setStatus(SessionManager::get().statusText());
    }

    void JoinLayer::statusTick(float) {
        auto& manager = SessionManager::get();
        manager.tick(0.f);

        auto revision = manager.statusRevision();
        auto status = manager.roomSummary();
        if (revision == m_seenStatusRevision && status == m_lastRenderedStatus) return;

        m_seenStatusRevision = revision;
        m_lastRenderedStatus = status;
        setStatus(status);
    }

    void JoinLayer::setStatus(const std::string& text) {
        if (m_statusLabel) {
            m_statusLabel->setString(text.c_str());
        }
    }

} // namespace ep2p
