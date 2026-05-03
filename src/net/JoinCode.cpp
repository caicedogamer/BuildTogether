#include <EditorP2P/net/JoinCode.hpp>
#include <EditorP2P/util/StringUtil.hpp>
#include <random>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace ep2p {
    namespace {
        constexpr const char* COMPACT_PREFIX = "BT1-";
        constexpr const char* B64URL = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        std::string base64UrlEncode(const std::string& input) {
            std::string out;
            int val = 0;
            int valb = -6;
            for (unsigned char c : input) {
                val = (val << 8) + c;
                valb += 8;
                while (valb >= 0) {
                    out.push_back(B64URL[(val >> valb) & 0x3F]);
                    valb -= 6;
                }
            }
            if (valb > -6) out.push_back(B64URL[((val << 8) >> (valb + 8)) & 0x3F]);
            return out;
        }

        std::optional<std::string> base64UrlDecode(const std::string& input) {
            std::vector<int> table(256, -1);
            for (int i = 0; i < 64; ++i) {
                table[static_cast<unsigned char>(B64URL[i])] = i;
            }

            std::string out;
            int val = 0;
            int valb = -8;
            for (unsigned char c : input) {
                if (table[c] == -1) return std::nullopt;
                val = (val << 6) + table[c];
                valb += 6;
                if (valb >= 0) {
                    out.push_back(static_cast<char>((val >> valb) & 0xFF));
                    valb -= 8;
                }
            }
            return out;
        }

        bool startsWithCompactPrefix(const std::string& raw) {
            return raw.size() > 4 &&
                   (raw[0] == 'B' || raw[0] == 'b') &&
                   (raw[1] == 'T' || raw[1] == 't') &&
                   raw[2] == '1' &&
                   raw[3] == '-';
        }
    }

    std::optional<JoinCode> JoinCode::parse(const std::string& raw) {
        std::string text = StringUtil::trim(raw);

        if (startsWithCompactPrefix(text)) {
            auto decoded = base64UrlDecode(text.substr(4));
            if (!decoded) return std::nullopt;
            return parse(*decoded);
        }

        // Expected format: "192.168.1.25:43720#ABCD-1234"
        auto hashPos = text.find('#');
        if (hashPos == std::string::npos) return std::nullopt;

        std::string epPart  = text.substr(0, hashPos);
        std::string keyPart = text.substr(hashPos + 1);

        auto ep = Endpoint::fromString(epPart);
        if (ep.empty()) return std::nullopt;

        // Session key must be exactly "XXXX-XXXX" (9 chars including the hyphen).
        if (keyPart.size() != 9 || keyPart[4] != '-') return std::nullopt;
        for (size_t i = 0; i < keyPart.size(); ++i) {
            if (i == 4) continue;
            unsigned char c = static_cast<unsigned char>(keyPart[i]);
            if (!std::isalnum(c)) return std::nullopt;
        }

        JoinCode jc;
        jc.endpoint   = ep;
        jc.sessionKey = StringUtil::toUpper(keyPart);
        return jc;
    }

    std::string JoinCode::format() const {
        return endpoint.str() + "#" + sessionKey;
    }

    std::string JoinCode::formatCompact() const {
        return std::string(COMPACT_PREFIX) + base64UrlEncode(format());
    }

    std::string JoinCode::generateKey() {
        static const char CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        static const int  CHARS_LEN = static_cast<int>(sizeof(CHARS) - 1);

        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int> dist(0, CHARS_LEN - 1);

        std::string key(9, '-');
        for (int i = 0; i < 9; ++i) {
            if (i == 4) continue;  // keep the hyphen
            key[i] = CHARS[dist(rng)];
        }
        return key;
    }

} // namespace ep2p
