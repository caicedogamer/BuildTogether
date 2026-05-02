#pragma once

#include <string>
#include <cstdint>

namespace ep2p {

    // A network address: IPv4 host string + port.
    struct Endpoint {
        std::string   host;
        uint16_t      port = 0;

        bool empty()    const { return host.empty() || port == 0; }
        std::string str() const;  // returns "host:port"

        static Endpoint fromString(const std::string& hostPort);  // parses "host:port"
        bool operator==(const Endpoint& o) const { return host == o.host && port == o.port; }
    };

} // namespace ep2p
