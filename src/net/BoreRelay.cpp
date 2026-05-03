#include <EditorP2P/net/BoreRelay.hpp>

#ifdef EP2P_WINDOWS

#include <Geode/Geode.hpp>
#include <ws2tcpip.h>
#include <cstring>
#include <string>
#include <thread>

namespace ep2p {

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static SOCKET connectToBore() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("bore.pub", "7835", &hints, &res) != 0 || !res) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    int rc = connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);
    if (rc == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

static SOCKET connectLocal(uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

static bool recvExact(SOCKET sock, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        int r = recv(sock, reinterpret_cast<char*>(buf + got),
                     static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

// Minimal JSON field extractors — covers bore's simple message shapes.
static int jsonInt(const std::string& json, const std::string& key) {
    auto kp = json.find("\"" + key + "\"");
    if (kp == std::string::npos) return -1;
    auto cp = json.find(':', kp);
    if (cp == std::string::npos) return -1;
    size_t i = cp + 1;
    while (i < json.size() && std::isspace((unsigned char)json[i])) ++i;
    try { return std::stoi(json.substr(i)); } catch (...) { return -1; }
}

static std::string jsonStr(const std::string& json, const std::string& key) {
    auto kp = json.find("\"" + key + "\"");
    if (kp == std::string::npos) return {};
    auto cp = json.find(':', kp);
    if (cp == std::string::npos) return {};
    auto q1 = json.find('"', cp + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// -------------------------------------------------------------------------
// BoreRelay
// -------------------------------------------------------------------------

bool BoreRelay::sendMsg(SOCKET sock, const std::string& json) {
    uint32_t net = htonl(static_cast<uint32_t>(json.size()));
    if (send(sock, reinterpret_cast<const char*>(&net), 4, 0) == SOCKET_ERROR)
        return false;
    return send(sock, json.c_str(), static_cast<int>(json.size()), 0) != SOCKET_ERROR;
}

std::string BoreRelay::recvMsg(SOCKET sock) {
    uint32_t netLen = 0;
    if (!recvExact(sock, reinterpret_cast<uint8_t*>(&netLen), 4)) return {};
    uint32_t len = ntohl(netLen);
    if (len == 0 || len > 65536) return {};
    std::string buf(len, '\0');
    if (!recvExact(sock, reinterpret_cast<uint8_t*>(&buf[0]), len)) return {};
    return buf;
}

void BoreRelay::bridgeConnection(const std::string& uuid, uint16_t localPort) {
    SOCKET boreSock = connectToBore();
    if (boreSock == INVALID_SOCKET) return;

    if (!sendMsg(boreSock, "{\"Accept\":\"" + uuid + "\"}")) {
        closesocket(boreSock);
        return;
    }

    SOCKET localSock = connectLocal(localPort);
    if (localSock == INVALID_SOCKET) {
        closesocket(boreSock);
        return;
    }

    // bore → local
    std::thread([boreSock, localSock]() {
        char buf[4096];
        bool ok = true;
        while (ok) {
            int n = recv(boreSock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            for (int sent = 0; sent < n && ok;) {
                int r = send(localSock, buf + sent, n - sent, 0);
                if (r == SOCKET_ERROR) { ok = false; } else { sent += r; }
            }
        }
        shutdown(localSock, SD_SEND);
    }).detach();

    // local → bore (owns cleanup of both sockets)
    std::thread([boreSock, localSock]() {
        char buf[4096];
        bool ok = true;
        while (ok) {
            int n = recv(localSock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            for (int sent = 0; sent < n && ok;) {
                int r = send(boreSock, buf + sent, n - sent, 0);
                if (r == SOCKET_ERROR) { ok = false; } else { sent += r; }
            }
        }
        shutdown(boreSock, SD_SEND);
        closesocket(boreSock);
        closesocket(localSock);
    }).detach();
}

void BoreRelay::controlLoop(uint16_t localPort,
                             ReadyCallback onReady,
                             StopCallback  onStop)
{
    auto fail = [&](std::string reason) {
        m_running = false;
        geode::Loader::get()->queueInMainThread([onStop, reason]() {
            onStop(reason);
        });
    };

    m_controlSock = connectToBore();
    if (m_controlSock == INVALID_SOCKET)
        return fail("Could not reach bore.pub — check your internet connection.");

    // Hello(localPort): {"Hello": 43720}
    if (!sendMsg(m_controlSock, "{\"Hello\":" + std::to_string(localPort) + "}"))
        return fail("Failed to send hello to bore.pub.");

    std::string resp = recvMsg(m_controlSock);
    if (resp.empty())
        return fail("No response from bore.pub.");

    int publicPort = jsonInt(resp, "Hello");
    if (publicPort <= 0) {
        std::string err = jsonStr(resp, "Error");
        return fail("bore.pub rejected the request: " + (err.empty() ? resp : err));
    }

    std::string publicAddr = "bore.pub:" + std::to_string(publicPort);
    geode::Loader::get()->queueInMainThread([onReady, publicAddr]() {
        onReady(publicAddr);
    });

    while (m_running) {
        std::string msg = recvMsg(m_controlSock);
        if (msg.empty()) break;
        if (msg == "\"Heartbeat\"") continue;

        std::string uuid = jsonStr(msg, "Connection");
        if (!uuid.empty())
            std::thread(&BoreRelay::bridgeConnection, this, uuid, localPort).detach();
    }

    closesocket(m_controlSock);
    m_controlSock = INVALID_SOCKET;
    m_running     = false;
    geode::Loader::get()->queueInMainThread([onStop]() {
        onStop("Relay disconnected.");
    });
}

void BoreRelay::start(uint16_t localPort, ReadyCallback onReady, StopCallback onStop) {
    if (m_running) return;
    m_running       = true;
    m_controlThread = std::thread(&BoreRelay::controlLoop, this,
                                  localPort, onReady, onStop);
}

void BoreRelay::stop() {
    m_running = false;
    if (m_controlSock != INVALID_SOCKET) {
        closesocket(m_controlSock);
        m_controlSock = INVALID_SOCKET;
    }
    if (m_controlThread.joinable())
        m_controlThread.join();
}

} // namespace ep2p

#endif // EP2P_WINDOWS
