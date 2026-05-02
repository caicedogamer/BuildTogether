#pragma once

#include <EditorP2P/core/Types.hpp>
#include <chrono>

namespace ep2p {

    // Lightweight clock utilities. All functions return milliseconds since the
    // system clock epoch (or since session start when using sessionMs()).
    namespace Clock {

        // Current time in ms since Unix epoch.
        TimestampMs now();

        // Elapsed ms since a previously recorded timestamp.
        TimestampMs elapsed(TimestampMs sinceMs);

        // Record the session start time. Call when a session transitions to Connected.
        void markSessionStart();

        // Ms elapsed since session start. Returns 0 if markSessionStart() was not called.
        TimestampMs sessionMs();

    } // namespace Clock

} // namespace ep2p
