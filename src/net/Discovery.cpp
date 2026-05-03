#include <EditorP2P/net/Discovery.hpp>
#include <EditorP2P/net/WinSocketTransport.hpp>
#include <EditorP2P/config/BuildConfig.hpp>
#include <EditorP2P/util/StringUtil.hpp>

#include <thread>
#include <chrono>
#include <sstream>
#include <unordered_set>
#include <cstring>

namespace ep2p {

    // Simple JSON-like broadcast payload (no external JSON lib required for V1).
    // Format: "EDCO|roomName|hostName|port|protocolVersion"
    static std::string encodeDiscoveryPayload(const DiscoveryInfo& info) {
        std::ostringstream ss;
        ss << "EDCO|"
           << info.roomName << "|"
           << info.hostName << "|"
           << info.sessionKey << "|"
           << info.endpoint.port << "|"
           << info.protocolVersion;
        return ss.str();
    }

    static std::optional<DiscoveryInfo> decodeDiscoveryPayload(
        const std::string& raw, const std::string& senderIp)
    {
        if (raw.substr(0, 5) != "EDCO|") return std::nullopt;

        std::istringstream ss(raw.substr(5));
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, '|')) parts.push_back(token);
        if (parts.size() < 5) return std::nullopt;

        DiscoveryInfo info;
        info.roomName         = parts[0];
        info.hostName         = parts[1];
        info.sessionKey       = parts[2];
        info.endpoint.host    = senderIp;
        auto port = StringUtil::parsePort(parts[3]);
        auto protocolVersion = StringUtil::parseInt(parts[4]);
        if (!port || !protocolVersion || *protocolVersion < 0 || *protocolVersion > 65535) {
            return std::nullopt;
        }

        info.endpoint.port    = *port;
        info.protocolVersion  = static_cast<uint16_t>(*protocolVersion);
        return info;
    }

    // -------------------------------------------------------------------------
    // LanBroadcaster
    // -------------------------------------------------------------------------

    void LanBroadcaster::start(const DiscoveryInfo& info) {
        if (m_running) return;
        m_info    = info;
        m_running = true;
        m_thread  = std::thread(&LanBroadcaster::broadcastLoop, this);
    }

    void LanBroadcaster::stop() {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
    }

    void LanBroadcaster::broadcastLoop() {
#ifdef EP2P_WINDOWS
        WinUdpSocket sock;
        if (!sock.open(0, /*broadcast=*/true)) return;  // bind to any ephemeral port

        std::string payload = encodeDiscoveryPayload(m_info);

        while (m_running) {
            sock.broadcastTo(PRESENCE_PORT,
                             reinterpret_cast<const uint8_t*>(payload.data()),
                             payload.size());
            std::this_thread::sleep_for(
                std::chrono::milliseconds(LAN_BROADCAST_INTERVAL_MS));
        }
#endif
    }

    // -------------------------------------------------------------------------
    // LanScanner
    // -------------------------------------------------------------------------

    void LanScanner::scan(int durationMs, std::function<void(DiscoveryInfo)> onFound) {
#ifdef EP2P_WINDOWS
        WinUdpSocket sock;
        if (!sock.open(PRESENCE_PORT, /*broadcast=*/true)) return;

        std::unordered_set<std::string> seen;  // deduplicate by "ip:port"
        uint8_t buf[512];
        auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(durationMs);

        while (std::chrono::steady_clock::now() < deadline) {
            Endpoint sender;
            int n = sock.recvFrom(buf, sizeof(buf), sender);
            if (n > 0) {
                std::string raw(reinterpret_cast<const char*>(buf),
                                static_cast<size_t>(n));
                auto info = decodeDiscoveryPayload(raw, sender.host);
                if (info) {
                    std::string key = info->endpoint.str();
                    if (seen.insert(key).second) {
                        onFound(*info);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#else
        (void)durationMs; (void)onFound;
#endif
    }

} // namespace ep2p
