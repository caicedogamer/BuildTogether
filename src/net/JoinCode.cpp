#include <EditorP2P/net/JoinCode.hpp>
#include <EditorP2P/util/StringUtil.hpp>
#include <random>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ep2p {

    std::optional<JoinCode> JoinCode::parse(const std::string& raw) {
        // Expected format: "192.168.1.25:43720#ABCD-1234"
        auto hashPos = raw.find('#');
        if (hashPos == std::string::npos) return std::nullopt;

        std::string epPart  = raw.substr(0, hashPos);
        std::string keyPart = raw.substr(hashPos + 1);

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
