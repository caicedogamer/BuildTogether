#include <EditorP2P/util/Clock.hpp>

#include <chrono>
#include <atomic>

namespace ep2p {
namespace Clock {

    using SteadyClock = std::chrono::steady_clock;
    using SystemClock = std::chrono::system_clock;

    // Session start captured by markSessionStart().
    static std::atomic<uint64_t> s_sessionStartMs{0};

    TimestampMs now() {
        auto tp = SystemClock::now().time_since_epoch();
        return static_cast<TimestampMs>(
            std::chrono::duration_cast<std::chrono::milliseconds>(tp).count());
    }

    TimestampMs elapsed(TimestampMs sinceMs) {
        TimestampMs n = now();
        return (n > sinceMs) ? (n - sinceMs) : 0;
    }

    void markSessionStart() {
        s_sessionStartMs.store(now());
    }

    TimestampMs sessionMs() {
        uint64_t start = s_sessionStartMs.load();
        if (start == 0) return 0;
        TimestampMs n = now();
        return (n > start) ? (n - start) : 0;
    }

} // namespace Clock
} // namespace ep2p
