#pragma once

#include <EditorP2P/protocol/Message.hpp>
#include <EditorP2P/util/Result.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ep2p {

    // Encodes and decodes Messages to/from raw byte frames.
    //
    // V1 encoding: payload is a pipe-delimited UTF-8 string.
    //   e.g. hello payload: "1|ABCD-1234|PlayerName|1"
    //                        ^  ^          ^           ^ requestedRole
    //                        |  sessionKey displayName
    //                        protocolVersion
    //
    // This is easy to inspect in a hex dump during development.
    // A future milestone can replace this with packed binary structs inside Message::payload.
    class MessageCodec {
    public:
        // Encode a Message to a full framed byte buffer (FrameHeader + payload bytes).
        static std::vector<uint8_t> encode(const Message& msg);

        // Feed bytes into the incremental parser.
        // Call repeatedly as bytes arrive from the TCP stream.
        // onMessage is called for each fully parsed Message (may be called 0-N times per feed).
        void feed(const uint8_t* data, size_t len,
                  std::function<void(Message)> onMessage);

        void reset();

        // --- Payload helpers (encode named fields into the payload string) ---

        struct HelloFields {
            uint16_t    protocolVersion;
            std::string sessionKey;
            std::string displayName;
            uint8_t     requestedRole;
        };
        static std::string encodeHello(const HelloFields& f);
        static Result<HelloFields> decodeHello(const std::string& payload);

        struct HelloAckFields {
            bool        accepted;
            uint32_t    assignedPeerId;
            uint8_t     grantedRole;
            std::string rejectReason;
            std::string hostDisplayName;
            std::string roomName;
        };
        static std::string encodeHelloAck(const HelloAckFields& f);
        static Result<HelloAckFields> decodeHelloAck(const std::string& payload);

        struct PlaceObjectFields {
            uint32_t tempClientId;
            uint16_t gdObjectId;
            float    x, y;
            float    rotation;
            float    scaleX, scaleY;
            std::string saveString;
        };
        static std::string encodePlaceObject(const PlaceObjectFields& f);
        static Result<PlaceObjectFields> decodePlaceObject(const std::string& payload);

        struct PresenceFields {
            float       x = 0.f;
            float       y = 0.f;
            uint32_t    colorRgb = 0xFF5050u;
            std::string displayName;
        };
        static std::string encodePresence(const PresenceFields& f);
        static Result<PresenceFields> decodePresence(const std::string& payload);

        // Record separator for snapshot payloads — never appears in GD save strings.
        static constexpr char SNAPSHOT_RECORD_SEP = '\x1e';
        // Maximum bytes per snapshot chunk (leaves headroom inside MAX_FRAME_PAYLOAD).
        static constexpr size_t SNAPSHOT_CHUNK_BUDGET = 60000;

        struct SnapshotObjectEntry {
            NetworkObjectId netId      = 0;
            std::string     saveString;
        };

        struct SnapshotChunkFields {
            uint32_t chunkIndex  = 0;
            uint32_t totalChunks = 0;
            std::vector<SnapshotObjectEntry> objects;
        };

        static std::string encodeSnapshotChunk(const SnapshotChunkFields& f);
        static Result<SnapshotChunkFields> decodeSnapshotChunk(const std::string& payload);

    private:
        enum class ParseState { WaitMagic, WaitHeader, WaitPayload };

        ParseState           m_parseState = ParseState::WaitMagic;
        std::vector<uint8_t> m_buf;
        uint16_t             m_pendingType   = 0;
        uint32_t             m_pendingLen    = 0;
        uint32_t             m_pendingSender = 0;
        uint32_t             m_pendingSeq    = 0;
    };

} // namespace ep2p
