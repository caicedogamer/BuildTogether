#include <EditorP2P/ui/JoinLayer.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/config/RuntimeConfig.hpp>
#include <EditorP2P/net/JoinCode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

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

        m_joinCodeInput = geode::TextInput::create(290.f, "127.0.0.1:43720#ABCD-1234");
        m_joinCodeInput->setPosition({size.width / 2.f, size.height / 2.f + 38.f});
        m_joinCodeInput->setCommonFilter(geode::CommonFilter::Any);
        m_joinCodeInput->setMaxCharCount(80);
        m_joinCodeInput->setString("127.0.0.1:43720#");
        this->m_mainLayer->addChild(m_joinCodeInput);

        m_statusLabel = CCLabelBMFont::create("Paste a host join code, then press Join.", "bigFont.fnt");
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

    void JoinLayer::onJoin(CCObject*) {
        if (!m_joinCodeInput) return;

        auto parsed = JoinCode::parse(m_joinCodeInput->getString());
        if (!parsed) {
            setStatus("Invalid join code.\nUse IP:port#XXXX-XXXX.");
            return;
        }

        auto& runtime = RuntimeConfig::get();
        SessionConfig config;
        config.mode = SessionMode::Peer;
        config.displayName = runtime.displayName;
        config.remoteEndpoint = parsed->endpoint;
        config.sessionKey = parsed->sessionKey;

        auto result = SessionManager::get().joinAsPeer(config);
        if (!result) {
            setStatus("Could not join:\n" + result.error());
            return;
        }

        setStatus("Connecting to " + parsed->endpoint.str());
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
