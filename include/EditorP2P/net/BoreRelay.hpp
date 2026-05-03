#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#ifdef EP2P_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#endif

namespace ep2p {

// Establishes a TCP tunnel through bore.pub so the host can accept connections
// without port forwarding. Implements the bore wire protocol:
//   control port 7835, framing: 4-byte big-endian length + JSON body.
class BoreRelay {
public:
    // Called on main thread with "bore.pub:PORT" when the tunnel is ready.
    using ReadyCallback = std::function<void(std::string)>;
    // Called on main thread with a reason string when the tunnel stops.
    using StopCallback  = std::function<void(std::string)>;

    void start(uint16_t localPort, ReadyCallback onReady, StopCallback onStop);
    void stop();
    bool isRunning() const { return m_running.load(); }

private:
    void        controlLoop(uint16_t localPort, ReadyCallback onReady, StopCallback onStop);
    void        bridgeConnection(const std::string& uuid, uint16_t localPort);
    bool        sendMsg(SOCKET sock, const std::string& json);
    std::string recvMsg(SOCKET sock);

    SOCKET            m_controlSock  = INVALID_SOCKET;
    std::thread       m_controlThread;
    std::atomic<bool> m_running      { false };
};

} // namespace ep2p
