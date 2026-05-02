#pragma once

#include <Geode/ui/Popup.hpp>
#include <string>

namespace cocos2d {
    class CCNode;
    class CCObject;
}

namespace ep2p {

    // Host-only popup showing a scrollable list of activity events.
    // Triggered from the collab toolbar button while hosting.
    class ActivityLogLayer : public geode::Popup {
    public:
        static ActivityLogLayer* create();

    protected:
        bool setup();

    private:
        void refresh();  // re-populates list from ActivityLog::get()
        void onClear(cocos2d::CCObject*);
        void onCopy(cocos2d::CCObject*);
        std::string buildLogText() const;

        cocos2d::CCNode* m_list = nullptr;
    };

} // namespace ep2p
