#include <EditorP2P/net/WinSocketTransport.hpp>

#ifdef EP2P_WINDOWS

#include <EditorP2P/config/BuildConfig.hpp>
#include <EditorP2P/protocol/Message.hpp>

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace ep2p {

    WinTcpTransport::WinTcpTransport(SOCKET sock)
        : m_sock(sock)
        , m_connected(sock != INVALID_SOCKET)
    {}

    WinTcpTransport::WinTcpTransport(const Endpoint& remote, int connectTimeoutMs)
        : m_remote(remote)
    {
        m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_sock == INVALID_SOCKET) return;

        u_long nonBlocking = 1;
        ioctlsocket(m_sock, FIONBIO, &nonBlocking);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(remote.port);
        if (inet_pton(AF_INET, remote.host.c_str(), &addr.sin_addr) != 1) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
            return;
        }

        int rc = ::connect(m_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                closesocket(m_sock);
                m_sock = INVALID_SOCKET;
                return;
            }

            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(m_sock, &writeSet);

            timeval timeout{};
            timeout.tv_sec = connectTimeoutMs / 1000;
            timeout.tv_usec = (connectTimeoutMs % 1000) * 1000;

            rc = select(0, nullptr, &writeSet, nullptr, &timeout);
            if (rc <= 0) {
                closesocket(m_sock);
                m_sock = INVALID_SOCKET;
                return;
            }

            int soError = 0;
            int soLen = sizeof(soError);
            getsockopt(m_sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soLen);
            if (soError != 0) {
                closesocket(m_sock);
                m_sock = INVALID_SOCKET;
                return;
            }
        }

        u_long blocking = 0;
        ioctlsocket(m_sock, FIONBIO, &blocking);
        m_connected = true;
    }

    WinTcpTransport::~WinTcpTransport() {
        stop();
    }

    bool WinTcpTransport::start() {
        if (m_sock == INVALID_SOCKET) return false;
        m_running = true;
        m_connected = true;
        if (!m_recvThread.joinable()) {
            m_recvThread = std::thread(&WinTcpTransport::recvLoop, this);
        }
        return true;
    }

    void WinTcpTransport::stop() {
        m_running = false;
        if (m_sock != INVALID_SOCKET) {
            shutdown(m_sock, SD_BOTH);
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        if (m_recvThread.joinable()) {
            m_recvThread.join();
        }
        m_connected = false;
    }

    bool WinTcpTransport::send(const uint8_t* data, size_t len) {
        if (!m_connected || m_sock == INVALID_SOCKET) return false;
        std::lock_guard<std::mutex> lock(m_sendMutex);

        size_t sentTotal = 0;
        while (sentTotal < len) {
            int chunk = static_cast<int>(std::min<size_t>(len - sentTotal, 64 * 1024));
            int sent = ::send(
                m_sock,
                reinterpret_cast<const char*>(data + sentTotal),
                chunk,
                0
            );
            if (sent == SOCKET_ERROR || sent == 0) {
                m_connected = false;
                return false;
            }
            sentTotal += static_cast<size_t>(sent);
        }
        return true;
    }

    bool WinTcpTransport::poll(std::vector<uint8_t>& outData) {
        (void)outData;
        return false;
    }

    bool WinTcpTransport::isConnected() const {
        return m_connected.load();
    }

    std::string WinTcpTransport::statusString() const {
        if (!m_connected) return "Disconnected";
        return m_remote.empty() ? "Connected" : "Connected to " + m_remote.str();
    }

    bool WinTcpTransport::recvExact(uint8_t* buf, size_t len) {
        size_t received = 0;
        while (m_running && received < len) {
            int n = ::recv(
                m_sock,
                reinterpret_cast<char*>(buf + received),
                static_cast<int>(len - received),
                0
            );
            if (n <= 0) return false;
            received += static_cast<size_t>(n);
        }
        return received == len;
    }

    void WinTcpTransport::recvLoop() {
        std::string reason = "Connection closed";

        while (m_running) {
            FrameHeader hdr;
            if (!recvExact(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr))) break;

            if (hdr.magic != FRAME_MAGIC) {
                reason = "Bad frame magic";
                break;
            }
            if (hdr.payloadLen > MAX_FRAME_PAYLOAD) {
                reason = "Frame too large";
                break;
            }

            std::vector<uint8_t> frame(sizeof(FrameHeader) + hdr.payloadLen);
            std::memcpy(frame.data(), &hdr, sizeof(hdr));
            if (hdr.payloadLen > 0 &&
                !recvExact(frame.data() + sizeof(FrameHeader), hdr.payloadLen)) {
                break;
            }

            if (onDataReceived) {
                if (hdr.type != static_cast<uint16_t>(MessageType::Heartbeat) &&
                    hdr.type != static_cast<uint16_t>(MessageType::PresenceUpdate)) {
                    geode::log::info(
                        "[EditorP2P] TCP frame received: type={} payload={}",
                        hdr.type,
                        hdr.payloadLen
                    );
                }
                onDataReceived(frame.data(), frame.size());
            }
        }

        m_running = false;
        m_connected = false;
        if (onDisconnected && m_sock != INVALID_SOCKET) {
            onDisconnected(reason);
        }
    }

    WinUdpSocket::~WinUdpSocket() {
        close();
    }

    bool WinUdpSocket::open(uint16_t bindPort, bool broadcast) {
        m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sock == INVALID_SOCKET) return false;

        BOOL reuse = TRUE;
        setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        if (broadcast) {
            BOOL bc = TRUE;
            setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST,
                       reinterpret_cast<const char*>(&bc), sizeof(bc));
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(bindPort);

        if (::bind(m_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            close();
            return false;
        }

        u_long mode = 1;
        ioctlsocket(m_sock, FIONBIO, &mode);

        return true;
    }

    void WinUdpSocket::close() {
        if (m_sock != INVALID_SOCKET) {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    bool WinUdpSocket::isOpen() const {
        return m_sock != INVALID_SOCKET;
    }

    bool WinUdpSocket::sendTo(const Endpoint& dest, const uint8_t* data, size_t len) {
        if (!isOpen()) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(dest.port);
        inet_pton(AF_INET, dest.host.c_str(), &addr.sin_addr);

        return ::sendto(m_sock,
                        reinterpret_cast<const char*>(data),
                        static_cast<int>(len), 0,
                        reinterpret_cast<sockaddr*>(&addr),
                        sizeof(addr)) != SOCKET_ERROR;
    }

    bool WinUdpSocket::broadcastTo(uint16_t port, const uint8_t* data, size_t len) {
        if (!isOpen()) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_BROADCAST;

        return ::sendto(m_sock,
                        reinterpret_cast<const char*>(data),
                        static_cast<int>(len), 0,
                        reinterpret_cast<sockaddr*>(&addr),
                        sizeof(addr)) != SOCKET_ERROR;
    }

    int WinUdpSocket::recvFrom(uint8_t* buf, size_t bufLen, Endpoint& outSender) {
        sockaddr_in from{};
        int fromLen = sizeof(from);

        int n = ::recvfrom(m_sock,
                           reinterpret_cast<char*>(buf),
                           static_cast<int>(bufLen), 0,
                           reinterpret_cast<sockaddr*>(&from),
                           &fromLen);
        if (n == SOCKET_ERROR) {
            return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
        }

        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ipBuf, sizeof(ipBuf));
        outSender.host = ipBuf;
        outSender.port = ntohs(from.sin_port);
        return n;
    }

} // namespace ep2p

#endif // EP2P_WINDOWS
