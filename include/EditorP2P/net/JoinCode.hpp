#pragma once

#include <EditorP2P/net/Endpoint.hpp>
#include <optional>
#include <string>

namespace ep2p {

    // Encodes a full join address as a single user-shareable string.
    // Format: "192.168.1.25:43720#ABCD-1234"
    //          -----------------  ---------
    //              Endpoint       SessionKey
    struct JoinCode {
        Endpoint    endpoint;
        std::string sessionKey;

        // Returns std::nullopt if the string is malformed.
        static std::optional<JoinCode> parse(const std::string& raw);

        // Produces "ip:port#key"
        std::string format() const;

        // Generates a random 8-character session key in "XXXX-XXXX" format.
        static std::string generateKey();

        bool valid() const { return !endpoint.empty() && sessionKey.size() == 9; }
    };

} // namespace ep2p
