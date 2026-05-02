#pragma once

#include <EditorP2P/core/Types.hpp>
#include <string>
#include <vector>
#include <functional>

namespace ep2p {

    enum class ActivityEventType : uint8_t {
        PeerJoined        = 0x01,
        PeerLeft          = 0x02,
        ObjectPlaced      = 0x10,
        ObjectMoved       = 0x11,
        ObjectDeleted     = 0x12,
        ObjectLocked      = 0x13,
        ObjectUnlocked    = 0x14,
        SaveRequested     = 0x20,
        SaveExecuted      = 0x21,
        PermissionChanged = 0x30,
        DesyncDetected    = 0xF0,
        Generic           = 0xFE,
        Error             = 0xFF,
    };

    struct ActivityEvent {
        ActivityEventType type;
        PlayerId          actorId;      // player who caused the event (HOST_PLAYER_ID for host)
        NetworkObjectId   objectId;     // 0 if not object-related
        std::string       detail;       // human-readable description
        TimestampMs       timestamp;    // ms since session start
    };

    // In-memory append-only activity log. Host-side only; peers receive events via
    // activity_event messages and may display them, but do not maintain an authoritative log.
    class ActivityLog {
    public:
        static ActivityLog& get();

        void add(ActivityEventType type,
                 PlayerId actorId,
                 NetworkObjectId objectId,
                 std::string detail);

        void clear();

        const std::vector<ActivityEvent>& events() const { return m_events; }
        size_t count() const { return m_events.size(); }

        // Optional callback fired on the main thread when a new event is added.
        // Set by ActivityLogLayer to refresh its list view.
        std::function<void(const ActivityEvent&)> onEvent;

    private:
        ActivityLog() = default;
        std::vector<ActivityEvent> m_events;
        TimestampMs                m_sessionStart = 0;
    };

} // namespace ep2p
