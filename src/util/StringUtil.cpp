#include <EditorP2P/util/StringUtil.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace ep2p::StringUtil {

    std::vector<std::string> split(const std::string& str, char delim) {
        std::vector<std::string> parts;
        std::string token;
        std::istringstream ss(str);
        while (std::getline(ss, token, delim)) {
            parts.push_back(token);
        }
        return parts;
    }

    std::string join(const std::vector<std::string>& parts, char delim) {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) result += delim;
            result += parts[i];
        }
        return result;
    }

    std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return {};
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    std::string toUpper(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
        return out;
    }

    std::string toLower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string fromFixedBuf(const char* buf, size_t maxLen) {
        size_t len = strnlen(buf, maxLen);
        return std::string(buf, len);
    }

    void toFixedBuf(const std::string& s, char* buf, size_t bufLen) {
        if (bufLen == 0) return;
        size_t copyLen = std::min(s.size(), bufLen - 1);
        std::memcpy(buf, s.data(), copyLen);
        buf[copyLen] = '\0';
    }

    std::optional<int> parseInt(const std::string& s) {
        try {
            size_t pos;
            int val = std::stoi(s, &pos);
            if (pos != s.size()) return std::nullopt;
            return val;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<uint16_t> parsePort(const std::string& s) {
        auto v = parseInt(s);
        if (!v || *v < 1 || *v > 65535) return std::nullopt;
        return static_cast<uint16_t>(*v);
    }

    std::optional<float> parseFloat(const std::string& s) {
        try {
            size_t pos;
            float val = std::stof(s, &pos);
            if (pos != s.size()) return std::nullopt;
            return val;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string formatDuration(uint64_t ms) {
        uint64_t totalSec = ms / 1000;
        uint64_t msRem    = ms % 1000;
        uint64_t sec      = totalSec % 60;
        uint64_t min      = (totalSec / 60) % 60;
        uint64_t hr       = totalSec / 3600;

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu.%03llu",
            static_cast<unsigned long long>(hr),
            static_cast<unsigned long long>(min),
            static_cast<unsigned long long>(sec),
            static_cast<unsigned long long>(msRem));
        return buf;
    }

} // namespace ep2p::StringUtil
