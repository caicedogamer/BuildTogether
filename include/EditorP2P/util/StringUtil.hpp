#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace ep2p::StringUtil {

    // Split str by delimiter. Empty segments are included.
    std::vector<std::string> split(const std::string& str, char delim);

    // Join a vector of strings with delimiter.
    std::string join(const std::vector<std::string>& parts, char delim);

    // Trim leading and trailing whitespace.
    std::string trim(const std::string& s);

    // Convert to upper/lower case (ASCII only).
    std::string toUpper(const std::string& s);
    std::string toLower(const std::string& s);

    // Safe conversion from C-string with a fixed max length (null-terminated or truncated).
    std::string fromFixedBuf(const char* buf, size_t maxLen);

    // Copy a std::string into a fixed-size char buffer, always null-terminating.
    void toFixedBuf(const std::string& s, char* buf, size_t bufLen);

    // Parse integer — returns nullopt on failure.
    std::optional<int>      parseInt(const std::string& s);
    std::optional<uint16_t> parsePort(const std::string& s);
    std::optional<float>    parseFloat(const std::string& s);

    // Format a millisecond duration as "HH:MM:SS.mmm".
    std::string formatDuration(uint64_t ms);

} // namespace ep2p::StringUtil
