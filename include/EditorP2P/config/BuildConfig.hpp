#pragma once

// Compile-time constants. Nothing here should change at runtime.
// Runtime-configurable values live in RuntimeConfig.

namespace ep2p {

    // Networking ports
    inline constexpr unsigned short CONTROL_PORT  = 43720;  // TCP: host listens, peer connects
    inline constexpr unsigned short PRESENCE_PORT = 43721;  // UDP: cursor presence + LAN discovery

    // Protocol
    inline constexpr unsigned short PROTOCOL_VERSION     = 1;
    inline constexpr unsigned short MIN_COMPAT_VERSION   = 1;  // reject peers below this
    inline constexpr unsigned int   FRAME_MAGIC          = 0xED1C0110u;

    // Session limits (V1: single peer)
    inline constexpr int MAX_PEERS = 1;

    // Presence
    inline constexpr int PRESENCE_RATE_HZ        = 20;     // cursor updates per second
    inline constexpr int CURSOR_STALE_SECONDS    = 2;      // fade cursor after this many seconds of silence

    // Heartbeat
    inline constexpr int HEARTBEAT_INTERVAL_MS   = 2000;
    inline constexpr int HEARTBEAT_TIMEOUT_MS    = 8000;

    // LAN discovery
    inline constexpr int LAN_BROADCAST_INTERVAL_MS = 2000;
    inline constexpr int LAN_SCAN_DURATION_MS       = 3000;

    // Sizing
    inline constexpr int DISPLAY_NAME_MAX_LEN  = 31;
    inline constexpr int ROOM_NAME_MAX_LEN     = 63;
    inline constexpr int SESSION_KEY_LEN       = 9;   // "XXXX-XXXX\0"
    inline constexpr int MAX_FRAME_PAYLOAD     = 65536;
    inline constexpr int ACTIVITY_LOG_MAX      = 200;

    // Player IDs
    inline constexpr unsigned int HOST_PLAYER_ID    = 0u;
    inline constexpr unsigned int INVALID_PLAYER_ID = 0xFFFFFFFFu;
    inline constexpr unsigned int INVALID_OBJECT_ID = 0u;

} // namespace ep2p
