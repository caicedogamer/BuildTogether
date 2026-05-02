#pragma once

#include <cstdint>
#include <string>

// Foundational type aliases and enumerations used across all modules.
// No GD/Geode headers. No heavy dependencies.

namespace ep2p {

    using PlayerId        = uint32_t;
    using NetworkObjectId = uint32_t;
    using TimestampMs     = uint64_t;
    using SessionKey      = std::string;  // format: "XXXX-XXXX"

    // Which role this GD instance is playing in the current session.
    enum class SessionMode {
        None,   // not in a session
        Host,   // started a session, owns canonical state
        Peer,   // joined someone else's session
    };

    // Lifecycle state of the active session.
    enum class SessionState {
        Idle,           // no session
        Hosting,        // server socket open, waiting for peer to connect
        Joining,        // TCP connect + hello in flight
        Connected,      // handshake complete, active session
        Disconnecting,  // graceful shutdown in progress
        Error,          // unrecoverable; user must restart session
    };

} // namespace ep2p
