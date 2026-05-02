#pragma once

#include <EditorP2P/core/Permissions.hpp>
#include <Geode/ui/Popup.hpp>

namespace cocos2d {
    class CCLabelBMFont;
    class CCObject;
}

namespace ep2p {

    // Host-only popup for managing the connected peer's role.
    // Lets the host promote/demote: Viewer / Builder / TrustedBuilder.
    class PermissionsLayer : public geode::Popup {
    public:
        static PermissionsLayer* create();

    protected:
        bool setup();

    private:
        void refresh();
        void applyRole(Role role);
        void onViewer(cocos2d::CCObject*);
        void onBuilder(cocos2d::CCObject*);
        void onTrusted(cocos2d::CCObject*);

        cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    };

} // namespace ep2p
