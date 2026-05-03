#pragma once

#include <Geode/ui/Popup.hpp>
#include <atomic>
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
        void refreshCodeDisplay();
        void startRelay();

        cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
        uint32_t m_seenStatusRevision = 0;
        std::string m_joinCodeText;
        std::string m_joinCodeValue;   // internet code (relay:PORT#key), or LAN key until resolved
        std::string m_sessionKey;
        std::string m_relayAddr;       // "relay:PORT" once tunnel is ready
        std::string m_lastRenderedStatus;
        std::atomic<bool> m_relayConnecting { false };
    };

} // namespace ep2p
