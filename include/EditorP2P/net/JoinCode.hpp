#pragma once

#include <EditorP2P/net/Endpoint.hpp>
#include <optional>
#include <string>

namespace ep2p {

    // Encodes a full join address as a single user-shareable string.
    // Legacy format: "192.168.1.25:43720#ABCD-1234"
    // Compact format: "BT1-..." (base64url of the legacy format)
    //          -----------------  ---------
    //              Endpoint       SessionKey
    struct JoinCode {
        Endpoint    endpoint;
        std::string sessionKey;

        // Returns std::nullopt if the string is malformed.
        static std::optional<JoinCode> parse(const std::string& raw);

        // Produces "host:port#key"
        std::string format() const;

        // Produces "BT1-..." for friendlier copy/paste without showing the endpoint.
        std::string formatCompact() const;

        // Generates a random 8-character session key in "XXXX-XXXX" format.
        static std::string generateKey();

        bool valid() const { return !endpoint.empty() && sessionKey.size() == 9; }
    };

} // namespace ep2p
