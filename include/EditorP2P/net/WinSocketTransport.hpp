#pragma once

#ifdef EP2P_WINDOWS

#include <EditorP2P/net/Transport.hpp>
#include <EditorP2P/net/Endpoint.hpp>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>

// Winsock — included only in this Windows-specific file and its .cpp.
// Do NOT include winsock2.h anywhere else; include this header instead.
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace ep2p {

    // TCP transport for the control channel.
    // One instance per connection (host creates one per accepted client; peer creates one for its outbound connection).
    class WinTcpTransport : public ITransport {
    public:
        // Host side: wrap an already-accepted SOCKET.
        explicit WinTcpTransport(SOCKET sock);

        // Peer side: connect to a remote endpoint.
        WinTcpTransport(const Endpoint& remote, int connectTimeoutMs = 5000);

        ~WinTcpTransport() override;

        bool start() override;
        void stop()  override;
        bool send(const uint8_t* data, size_t len) override;
        bool poll(std::vector<uint8_t>& outData)   override;
        bool isConnected() const override;
        std::string statusString() const override;

        // Blocking receive — used by the dedicated recv thread.
        bool recvExact(uint8_t* buf, size_t len);

        // Callback invoked on the recv thread when data arrives.
        // Caller must marshal to main thread via SessionManager::postToMainThread.
        std::function<void(const uint8_t*, size_t)> onDataReceived;
        std::function<void(std::string reason)>      onDisconnected;

    private:
        void recvLoop();

        SOCKET            m_sock      = INVALID_SOCKET;
        Endpoint          m_remote;
        std::atomic<bool> m_connected = false;
        std::atomic<bool> m_running   = false;
        std::thread       m_recvThread;
        std::mutex        m_sendMutex;
        std::vector<uint8_t> m_recvBuf;  // partial frame accumulator
    };

    // UDP socket for presence updates and LAN discovery broadcast.
    class WinUdpSocket {
    public:
        ~WinUdpSocket();

        bool open(uint16_t bindPort, bool broadcast = false);
        void close();
        bool isOpen() const;

        bool sendTo(const Endpoint& dest, const uint8_t* data, size_t len);
        bool broadcastTo(uint16_t port, const uint8_t* data, size_t len);

        // Returns bytes received (>0), 0 if nothing pending, -1 on error.
        int recvFrom(uint8_t* buf, size_t bufLen, Endpoint& outSender);

    private:
        SOCKET m_sock = INVALID_SOCKET;
    };

} // namespace ep2p

#endif // EP2P_WINDOWS
