#pragma once

#include <cstdint>
#include <string_view>

namespace ep2p {

    enum class MessageType : uint16_t {
        // Handshake
        Hello            = 0x0001,
        HelloAck         = 0x0002,
        Goodbye          = 0x0003,

        // Keep-alive
        Heartbeat        = 0x0004,

        // Presence  (usually UDP, but can travel over TCP)
        PresenceUpdate   = 0x0010,

        // Object operations
        PlaceObject      = 0x0020,
        PlaceObjectAck   = 0x0021,
        LockRequest      = 0x0022,
        LockGranted      = 0x0023,
        LockDenied       = 0x0024,
        EditObject       = 0x0025,
        CommitEdit       = 0x0026,
        UnlockObject     = 0x0027,
        DeleteObject     = 0x0028,

        // Permissions
        PermissionUpdate = 0x0030,

        // Save
        SaveRequest      = 0x0040,
        SaveCommand      = 0x0041,

        // Activity
        ActivityEvent    = 0x0050,

        // State sync
        StateResyncRequest = 0x0060,
        StateSnapshot      = 0x0061,

        // Error
        Error            = 0x00FF,

        Unknown          = 0xFFFF,
    };

    std::string_view messageTypeName(MessageType t);

} // namespace ep2p
