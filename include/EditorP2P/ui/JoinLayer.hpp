#pragma once

#include <Geode/ui/Popup.hpp>
#include <EditorP2P/net/JoinCode.hpp>
#include <atomic>
#include <string>

namespace geode {
    class TextInput;
}

namespace ep2p {

    // Popup shown when the player taps "Join" in the collab toolbar button menu.
    // Accepts a bare session key (XXXX-XXXX) and scans LAN to find the host IP,
    // or a full code (IP:port#XXXX-XXXX) for direct connection.
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
        void connectWith(const JoinCode& code);

        geode::TextInput* m_joinCodeInput = nullptr;
        cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
        uint32_t m_seenStatusRevision = 0;
        std::string m_lastRenderedStatus;
        std::atomic<bool> m_scanning { false };
    };

} // namespace ep2p
