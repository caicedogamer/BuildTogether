#include <EditorP2P/net/Heartbeat.hpp>

namespace ep2p {

    static std::chrono::steady_clock::time_point steadyNow() {
        return std::chrono::steady_clock::now();
    }

    void Heartbeat::start(
        std::function<void()> sendPing,
        std::function<void()> onTimeout,
        int intervalMs,
        int timeoutMs)
    {
        m_sendPing       = std::move(sendPing);
        m_onTimeout      = std::move(onTimeout);
        m_intervalMs     = intervalMs;
        m_timeoutMs      = timeoutMs;
        m_running        = true;
        m_waitingForPong = false;
        m_lastPing       = steadyNow();
        m_lastPong       = steadyNow();
    }

    void Heartbeat::stop() {
        m_running = false;
    }

    void Heartbeat::pongReceived() {
        m_lastPong       = steadyNow();
        m_waitingForPong = false;
    }

    void Heartbeat::tick() {
        if (!m_running) return;

        auto now = steadyNow();

        auto msSinceLastPing = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastPing).count();
        auto msSinceLastPong = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastPong).count();

        if (m_waitingForPong && msSinceLastPong > m_timeoutMs) {
            m_running = false;
            if (m_onTimeout) m_onTimeout();
            return;
        }

        if (!m_waitingForPong && msSinceLastPing > m_intervalMs) {
            m_lastPing       = now;
            m_waitingForPong = true;
            if (m_sendPing) m_sendPing();
        }
    }

} // namespace ep2p
