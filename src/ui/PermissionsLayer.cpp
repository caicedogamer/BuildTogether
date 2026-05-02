#include <EditorP2P/ui/PermissionsLayer.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace ep2p {

    PermissionsLayer* PermissionsLayer::create() {
        auto* obj = new PermissionsLayer();
        if (obj && obj->init(340.f, 220.f) && obj->setup()) {
            obj->autorelease();
            return obj;
        }
        CC_SAFE_DELETE(obj);
        return nullptr;
    }

    bool PermissionsLayer::setup() {
        this->setTitle("Permissions");

        auto size = this->m_mainLayer->getContentSize();
        m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_statusLabel->setScale(0.32f);
        m_statusLabel->setWidth(285.f);
        m_statusLabel->setAlignment(kCCTextAlignmentCenter);
        m_statusLabel->setPosition({size.width / 2.f, size.height / 2.f + 42.f});
        this->m_mainLayer->addChild(m_statusLabel);

        auto* menu = CCMenu::create();
        menu->setPosition({size.width / 2.f, size.height / 2.f - 24.f});
        this->m_mainLayer->addChild(menu);

        auto* viewer = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Viewer", 78, 0, 0.44f, true),
            this,
            menu_selector(PermissionsLayer::onViewer)
        );
        viewer->setPosition({-86.f, 0.f});
        menu->addChild(viewer);

        auto* builder = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Builder", 78, 0, 0.44f, true),
            this,
            menu_selector(PermissionsLayer::onBuilder)
        );
        builder->setPosition({0.f, 0.f});
        menu->addChild(builder);

        auto* trusted = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Trusted", 78, 0, 0.44f, true),
            this,
            menu_selector(PermissionsLayer::onTrusted)
        );
        trusted->setPosition({86.f, 0.f});
        menu->addChild(trusted);

        refresh();
        return true;
    }

    void PermissionsLayer::refresh() {
        if (!m_statusLabel) return;

        auto* session = SessionManager::get().activeSession();
        if (!session || !SessionManager::get().isHost()) {
            m_statusLabel->setString("Host a session to manage peer permissions.");
            return;
        }
        if (session->peers().empty()) {
            m_statusLabel->setString("No peer connected yet.");
            return;
        }

        const auto& peer = session->peers().front();
        auto text = peer.displayName + "\nCurrent role: " + roleName(peer.role);
        m_statusLabel->setString(text.c_str());
    }

    void PermissionsLayer::applyRole(Role role) {
        auto* session = SessionManager::get().activeSession();
        if (!session || session->peers().empty()) {
            refresh();
            return;
        }
        SessionManager::get().setPeerRole(session->peers().front().id, role);
        refresh();
    }

    void PermissionsLayer::onViewer(CCObject*) {
        applyRole(Role::Viewer);
    }

    void PermissionsLayer::onBuilder(CCObject*) {
        applyRole(Role::Builder);
    }

    void PermissionsLayer::onTrusted(CCObject*) {
        applyRole(Role::TrustedBuilder);
    }

} // namespace ep2p
