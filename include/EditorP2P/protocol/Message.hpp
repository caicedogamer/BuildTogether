#pragma once

#include <EditorP2P/protocol/MessageTypes.hpp>
#include <EditorP2P/core/Types.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace ep2p {

    // Wire frame header (10 bytes, little-endian).
    // Followed immediately by `payloadLen` payload bytes.
    #pragma pack(push, 1)
    struct FrameHeader {
        uint32_t magic      = 0xED1C0110u;  // sanity / framing guard
        uint16_t type       = 0;
        uint32_t payloadLen = 0;
    };
    #pragma pack(pop)

    static_assert(sizeof(FrameHeader) == 10, "FrameHeader must be exactly 10 bytes");

    // Decoded in-memory representation of one protocol message.
    // The payload is kept as a raw string for V1; MessageCodec knows how to
    // encode/decode each MessageType's fields into/out of this string.
    struct Message {
        MessageType type       = MessageType::Unknown;
        PlayerId    senderId   = 0;
        uint32_t    sequence   = 0;
        std::string payload;   // type-specific encoded content

        bool valid() const { return type != MessageType::Unknown; }
    };

    // Compact struct for UDP presence datagrams (no FrameHeader — the magic field
    // inside the struct serves as a minimal sanity check).
    #pragma pack(push, 1)
    struct PresenceDatagram {
        uint32_t magic    = 0xED1C0110u;
        uint32_t senderId = 0;
        float    worldX   = 0.f;
        float    worldY   = 0.f;
        uint32_t sequence = 0;
    };
    #pragma pack(pop)

    static_assert(sizeof(PresenceDatagram) == 20, "PresenceDatagram must be 20 bytes");

} // namespace ep2p
