#include <EditorP2P/net/Endpoint.hpp>
#include <EditorP2P/util/StringUtil.hpp>

namespace ep2p {

    std::string Endpoint::str() const {
        return host + ":" + std::to_string(port);
    }

    Endpoint Endpoint::fromString(const std::string& hostPort) {
        auto colonPos = hostPort.rfind(':');
        if (colonPos == std::string::npos) return {};

        Endpoint ep;
        ep.host = StringUtil::trim(hostPort.substr(0, colonPos));
        if (ep.host.empty()) return {};

        auto portStr = StringUtil::trim(hostPort.substr(colonPos + 1));
        auto port = StringUtil::parsePort(portStr);
        if (!port) return {};

        ep.port = *port;
        return ep;
    }

} // namespace ep2p
