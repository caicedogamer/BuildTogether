#pragma once

#include <Geode/ui/Popup.hpp>
#include <string>

namespace geode {
    class TextInput;
}

namespace ep2p {

    // Popup shown when the player taps "Join" in the collab toolbar button menu.
    // Lets the peer enter a join code (IP:port#sessionKey) or scan LAN.
    class JoinLayer : public geode::Popup {
    public:
        static JoinLayer* create();

    protected:
        bool setup();

    private:
        void onJoin(cocos2d::CCObject*);
        void onDisconnect(cocos2d::CCObject*);
        void onResync(cocos2d::CCObject*);
        void onSaveRequest(cocos2d::CCObject*);
        void statusTick(float);
        void setStatus(const std::string& text);

        geode::TextInput* m_joinCodeInput = nullptr;
        cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
        uint32_t m_seenStatusRevision = 0;
        std::string m_lastRenderedStatus;
    };

} // namespace ep2p
