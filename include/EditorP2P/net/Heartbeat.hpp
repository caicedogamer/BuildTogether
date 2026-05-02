#pragma once

#include <EditorP2P/config/BuildConfig.hpp>
#include <chrono>
#include <functional>
#include <atomic>

// Heartbeat uses std::chrono::steady_clock internally for sub-second precision.
// It does NOT depend on ep2p::Clock (which returns wall-clock milliseconds) because
// steady_clock is more appropriate for measuring intervals.

namespace ep2p {

    // Tracks heartbeat timing for a single connection.
    // The owner calls tick() regularly (e.g. in the recv loop or a timer thread).
    // sendPing is called when it is time to send a ping.
    // onTimeout is called when no pong has been received within HEARTBEAT_TIMEOUT_MS.
    class Heartbeat {
    public:
        void start(
            std::function<void()> sendPing,
            std::function<void()> onTimeout,
            int intervalMs = HEARTBEAT_INTERVAL_MS,
            int timeoutMs  = HEARTBEAT_TIMEOUT_MS
        );
        void stop();

        void pongReceived();   // reset the timeout clock
        void tick();           // call from recv/timer thread; fires ping / timeout as needed

        bool isRunning() const { return m_running.load(); }

    private:
        using Clock = std::chrono::steady_clock;
        using TP    = std::chrono::time_point<Clock>;

        std::function<void()> m_sendPing;
        std::function<void()> m_onTimeout;
        int               m_intervalMs = HEARTBEAT_INTERVAL_MS;
        int               m_timeoutMs  = HEARTBEAT_TIMEOUT_MS;
        TP                m_lastPing;
        TP                m_lastPong;
        std::atomic<bool> m_running = false;
        bool              m_waitingForPong = false;
    };

} // namespace ep2p
