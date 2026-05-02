#include <EditorP2P/protocol/MessageCodec.hpp>
#include <EditorP2P/util/StringUtil.hpp>
#include <EditorP2P/config/BuildConfig.hpp>

#include <cstring>
#include <sstream>

namespace ep2p {

    // =========================================================================
    // Frame encoding
    // =========================================================================

    std::vector<uint8_t> MessageCodec::encode(const Message& msg) {
        // Build payload string: "senderId|seq|payload"
        std::string body = std::to_string(msg.senderId) + "|"
                         + std::to_string(msg.sequence) + "|"
                         + msg.payload;

        FrameHeader hdr;
        hdr.magic      = FRAME_MAGIC;
        hdr.type       = static_cast<uint16_t>(msg.type);
        hdr.payloadLen = static_cast<uint32_t>(body.size());

        std::vector<uint8_t> frame(sizeof(FrameHeader) + body.size());
        std::memcpy(frame.data(), &hdr, sizeof(FrameHeader));
        std::memcpy(frame.data() + sizeof(FrameHeader), body.data(), body.size());
        return frame;
    }

    // =========================================================================
    // Incremental frame parser
    // =========================================================================

    void MessageCodec::feed(const uint8_t* data, size_t len,
                            std::function<void(Message)> onMessage)
    {
        m_buf.insert(m_buf.end(), data, data + len);

        while (true) {
            if (m_parseState == ParseState::WaitMagic) {
                if (m_buf.size() < sizeof(FrameHeader)) break;

                FrameHeader hdr;
                std::memcpy(&hdr, m_buf.data(), sizeof(FrameHeader));

                if (hdr.magic != FRAME_MAGIC) {
                    // Framing error: discard one byte and try again.
                    m_buf.erase(m_buf.begin());
                    continue;
                }
                if (hdr.payloadLen > MAX_FRAME_PAYLOAD) {
                    // Reject oversized frames.
                    reset();
                    break;
                }

                m_pendingType   = hdr.type;
                m_pendingLen    = hdr.payloadLen;
                m_buf.erase(m_buf.begin(), m_buf.begin() + sizeof(FrameHeader));
                m_parseState = ParseState::WaitPayload;
            }

            if (m_parseState == ParseState::WaitPayload) {
                if (m_buf.size() < m_pendingLen) break;

                std::string body(reinterpret_cast<const char*>(m_buf.data()), m_pendingLen);
                m_buf.erase(m_buf.begin(), m_buf.begin() + m_pendingLen);
                m_parseState = ParseState::WaitMagic;

                // Parse "senderId|seq|payload" wrapper.
                auto parts = StringUtil::split(body, '|');
                if (parts.size() < 3) continue;

                auto senderId = StringUtil::parseInt(parts[0]);
                auto seq      = StringUtil::parseInt(parts[1]);
                if (!senderId || !seq) continue;

                Message msg;
                msg.type     = static_cast<MessageType>(m_pendingType);
                msg.senderId = static_cast<uint32_t>(*senderId);
                msg.sequence = static_cast<uint32_t>(*seq);
                // Re-join the remaining parts as the payload (supports '|' inside payload).
                msg.payload  = StringUtil::join(
                    std::vector<std::string>(parts.begin() + 2, parts.end()), '|');

                onMessage(std::move(msg));
            }
        }
    }

    void MessageCodec::reset() {
        m_buf.clear();
        m_parseState  = ParseState::WaitMagic;
        m_pendingType = 0;
        m_pendingLen  = 0;
    }

    // =========================================================================
    // Payload helpers
    // =========================================================================

    std::string MessageCodec::encodeHello(const HelloFields& f) {
        std::ostringstream ss;
        ss << f.protocolVersion << "|"
           << f.sessionKey      << "|"
           << f.displayName     << "|"
           << static_cast<int>(f.requestedRole);
        return ss.str();
    }

    Result<MessageCodec::HelloFields> MessageCodec::decodeHello(const std::string& payload) {
        auto parts = StringUtil::split(payload, '|');
        if (parts.size() < 4) return Result<HelloFields>::err("Invalid hello payload");

        HelloFields f;
        auto ver = StringUtil::parseInt(parts[0]);
        auto role = StringUtil::parseInt(parts[3]);
        if (!ver || !role) return Result<HelloFields>::err("Invalid hello fields");

        f.protocolVersion = static_cast<uint16_t>(*ver);
        f.sessionKey      = parts[1];
        f.displayName     = parts[2];
        f.requestedRole   = static_cast<uint8_t>(*role);
        return Result<HelloFields>::ok(f);
    }

    std::string MessageCodec::encodeHelloAck(const HelloAckFields& f) {
        std::ostringstream ss;
        ss << (f.accepted ? 1 : 0)          << "|"
           << f.assignedPeerId              << "|"
           << static_cast<int>(f.grantedRole) << "|"
           << f.rejectReason               << "|"
           << f.hostDisplayName            << "|"
           << f.roomName;
        return ss.str();
    }

    Result<MessageCodec::HelloAckFields> MessageCodec::decodeHelloAck(const std::string& payload) {
        auto parts = StringUtil::split(payload, '|');
        if (parts.size() < 6) return Result<HelloAckFields>::err("Invalid hello_ack payload");

        auto peerId = StringUtil::parseInt(parts[1]);
        auto role = StringUtil::parseInt(parts[2]);
        if (!peerId || !role || *peerId < 0 || *role < 0) {
            return Result<HelloAckFields>::err("Invalid hello_ack numeric fields");
        }

        HelloAckFields f;
        f.accepted        = (parts[0] == "1");
        f.assignedPeerId  = static_cast<uint32_t>(*peerId);
        f.grantedRole     = static_cast<uint8_t>(*role);
        f.rejectReason    = parts[3];
        f.hostDisplayName = parts[4];
        f.roomName        = parts[5];
        return Result<HelloAckFields>::ok(f);
    }

    std::string MessageCodec::encodePlaceObject(const PlaceObjectFields& f) {
        std::ostringstream ss;
        ss << f.tempClientId << "|"
           << f.gdObjectId   << "|"
           << f.x            << "|"
           << f.y            << "|"
           << f.rotation     << "|"
           << f.scaleX       << "|"
           << f.scaleY       << "|"
           << f.saveString;
        return ss.str();
    }

    Result<MessageCodec::PlaceObjectFields> MessageCodec::decodePlaceObject(
        const std::string& payload)
    {
        auto parts = StringUtil::split(payload, '|');
        if (parts.size() < 7) return Result<PlaceObjectFields>::err("Invalid place_object payload");

        auto tempId = StringUtil::parseInt(parts[0]);
        auto objectId = StringUtil::parseInt(parts[1]);
        PlaceObjectFields f;
        if (!tempId || !objectId || *tempId < 0 || *objectId < 0 || *objectId > 65535) {
            return Result<PlaceObjectFields>::err("Invalid place_object IDs");
        }

        f.tempClientId = static_cast<uint32_t>(*tempId);
        f.gdObjectId   = static_cast<uint16_t>(*objectId);
        auto x      = StringUtil::parseFloat(parts[2]);
        auto y      = StringUtil::parseFloat(parts[3]);
        auto rot    = StringUtil::parseFloat(parts[4]);
        auto scaleX = StringUtil::parseFloat(parts[5]);
        auto scaleY = StringUtil::parseFloat(parts[6]);
        if (!x || !y || !rot || !scaleX || !scaleY) {
            return Result<PlaceObjectFields>::err("Invalid float in place_object");
        }
        f.x      = *x;  f.y      = *y;
        f.rotation = *rot;
        f.scaleX = *scaleX; f.scaleY = *scaleY;
        if (parts.size() > 7) {
            f.saveString = StringUtil::join(
                std::vector<std::string>(parts.begin() + 7, parts.end()), '|'
            );
        }
        return Result<PlaceObjectFields>::ok(f);
    }

    std::string MessageCodec::encodePresence(const PresenceFields& f) {
        std::ostringstream ss;
        ss << f.x << "|"
           << f.y << "|"
           << f.colorRgb << "|"
           << f.displayName;
        return ss.str();
    }

    Result<MessageCodec::PresenceFields> MessageCodec::decodePresence(const std::string& payload) {
        auto parts = StringUtil::split(payload, '|');
        if (parts.size() < 4) return Result<PresenceFields>::err("Invalid presence payload");

        auto x = StringUtil::parseFloat(parts[0]);
        auto y = StringUtil::parseFloat(parts[1]);
        auto color = StringUtil::parseInt(parts[2]);
        if (!x || !y || !color || *color < 0) {
            return Result<PresenceFields>::err("Invalid presence fields");
        }

        PresenceFields f;
        f.x = *x;
        f.y = *y;
        f.colorRgb = static_cast<uint32_t>(*color);
        f.displayName = StringUtil::join(
            std::vector<std::string>(parts.begin() + 3, parts.end()), '|'
        );
        return Result<PresenceFields>::ok(f);
    }

    std::string MessageCodec::encodeSnapshotChunk(const SnapshotChunkFields& f) {
        std::ostringstream ss;
        ss << f.chunkIndex << "|" << f.totalChunks << "|";
        for (const auto& entry : f.objects) {
            ss << entry.netId << "|" << entry.saveString << SNAPSHOT_RECORD_SEP;
        }
        return ss.str();
    }

    Result<MessageCodec::SnapshotChunkFields> MessageCodec::decodeSnapshotChunk(
        const std::string& payload)
    {
        // "chunkIndex|totalChunks|netId1|saveString1\x1EnetId2|saveString2\x1E..."
        auto firstPipe = payload.find('|');
        if (firstPipe == std::string::npos)
            return Result<SnapshotChunkFields>::err("Missing chunk index pipe");
        auto secondPipe = payload.find('|', firstPipe + 1);
        if (secondPipe == std::string::npos)
            return Result<SnapshotChunkFields>::err("Missing total chunks pipe");

        auto ci = StringUtil::parseInt(payload.substr(0, firstPipe));
        auto tc = StringUtil::parseInt(payload.substr(firstPipe + 1, secondPipe - firstPipe - 1));
        if (!ci || !tc || *ci < 0 || *tc <= 0)
            return Result<SnapshotChunkFields>::err("Invalid snapshot chunk header");

        SnapshotChunkFields f;
        f.chunkIndex  = static_cast<uint32_t>(*ci);
        f.totalChunks = static_cast<uint32_t>(*tc);

        const std::string records = payload.substr(secondPipe + 1);
        size_t pos = 0;
        while (pos < records.size()) {
            auto recEnd = records.find(SNAPSHOT_RECORD_SEP, pos);
            if (recEnd == std::string::npos) break;

            const auto rec = records.substr(pos, recEnd - pos);
            pos = recEnd + 1;
            if (rec.empty()) continue;

            auto pipe = rec.find('|');
            if (pipe == std::string::npos) continue;
            auto netIdOpt = StringUtil::parseInt(rec.substr(0, pipe));
            if (!netIdOpt || *netIdOpt <= 0) continue;

            SnapshotObjectEntry entry;
            entry.netId      = static_cast<NetworkObjectId>(*netIdOpt);
            entry.saveString = rec.substr(pipe + 1);
            f.objects.push_back(std::move(entry));
        }

        return Result<SnapshotChunkFields>::ok(std::move(f));
    }

} // namespace ep2p
