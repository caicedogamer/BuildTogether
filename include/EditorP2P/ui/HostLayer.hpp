#pragma once

#include <Geode/ui/Popup.hpp>
#include <string>

namespace ep2p {

    // Popup shown when the player taps "Host" in the collab toolbar button menu.
    // Lets the host configure room name, port, session key, then start listening.
    class HostLayer : public geode::Popup {
    public:
        static HostLayer* create();

    protected:
        bool setup();

    private:
        void onStartHost(cocos2d::CCObject*);
        void onDisconnect(cocos2d::CCObject*);
        void onCopyCode(cocos2d::CCObject*);
        void onPushState(cocos2d::CCObject*);
        void onPermissions(cocos2d::CCObject*);
        void onActivityLog(cocos2d::CCObject*);
        void statusTick(float);
        void setStatus(const std::string& text);

        cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
        uint32_t m_seenStatusRevision = 0;
        std::string m_joinCodeText;
        std::string m_joinCodeValue;
        std::string m_lastRenderedStatus;
    };

} // namespace ep2p
