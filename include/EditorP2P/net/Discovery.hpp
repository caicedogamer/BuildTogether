#pragma once

#include <EditorP2P/net/Endpoint.hpp>
#include <functional>
#include <string>
#include <thread>
#include <atomic>

namespace ep2p {

    // Information broadcast by the host over LAN so peers can discover sessions
    // without manually typing an IP address.
    struct DiscoveryInfo {
        std::string roomName;
        std::string hostName;
        Endpoint    endpoint;        // host IP + control port
        uint16_t    protocolVersion = 0;
    };

    // Broadcasts a DiscoveryInfo payload over UDP every LAN_BROADCAST_INTERVAL_MS.
    // Run on a background thread; call start() to begin, stop() to end.
    class LanBroadcaster {
    public:
        void start(const DiscoveryInfo& info);
        void stop();
        bool isRunning() const { return m_running.load(); }

    private:
        void broadcastLoop();

        DiscoveryInfo     m_info;
        std::thread       m_thread;
        std::atomic<bool> m_running = false;
    };

    // Listens for LanBroadcaster packets and calls onFound for each unique host discovered.
    // Scanning is blocking for durationMs; call from a dedicated thread or a thread pool.
    class LanScanner {
    public:
        // Blocks for durationMs. onFound may be called multiple times (once per unique host).
        void scan(int durationMs, std::function<void(DiscoveryInfo)> onFound);
    };

} // namespace ep2p
