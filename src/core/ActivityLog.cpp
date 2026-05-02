#include <EditorP2P/core/ActivityLog.hpp>
#include <EditorP2P/config/BuildConfig.hpp>
#include <EditorP2P/util/Clock.hpp>

namespace ep2p {

    ActivityLog& ActivityLog::get() {
        static ActivityLog instance;
        return instance;
    }

    void ActivityLog::add(ActivityEventType type,
                          PlayerId actorId,
                          NetworkObjectId objectId,
                          std::string detail)
    {
        // Trim oldest entry when the log is at capacity.
        if (m_events.size() >= static_cast<size_t>(ACTIVITY_LOG_MAX)) {
            m_events.erase(m_events.begin());
        }

        ActivityEvent ev;
        ev.type      = type;
        ev.actorId   = actorId;
        ev.objectId  = objectId;
        ev.detail    = std::move(detail);
        ev.timestamp = Clock::sessionMs();

        m_events.push_back(ev);

        if (onEvent) {
            onEvent(m_events.back());
        }
    }

    void ActivityLog::clear() {
        m_events.clear();
    }

} // namespace ep2p
