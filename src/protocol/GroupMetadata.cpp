#include <EditorP2P/protocol/GroupMetadata.hpp>
#include <EditorP2P/util/StringUtil.hpp>

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace ep2p {
    namespace {
        bool isObjectGroupKey(int key) {
            return key == 57;
        }

        bool isTargetGroupKey(int key) {
            switch (key) {
                case 51:
                case 71:
                case 80:
                case 81:
                case 100:
                case 101:
                case 104:
                case 105:
                case 108:
                case 109:
                case 110:
                case 114:
                    return true;
                default:
                    return false;
            }
        }

        std::vector<int> extractPositiveInts(const std::string& value) {
            std::vector<int> result;
            std::string current;
            for (char ch : value) {
                if (ch >= '0' && ch <= '9') {
                    current.push_back(ch);
                    continue;
                }

                if (!current.empty()) {
                    auto parsed = StringUtil::parseInt(current);
                    if (parsed && *parsed > 0) result.push_back(*parsed);
                    current.clear();
                }
            }

            if (!current.empty()) {
                auto parsed = StringUtil::parseInt(current);
                if (parsed && *parsed > 0) result.push_back(*parsed);
            }
            return result;
        }

        void appendUnique(std::vector<int>& out, const std::vector<int>& values) {
            for (int value : values) {
                if (std::find(out.begin(), out.end(), value) == out.end()) {
                    out.push_back(value);
                }
            }
        }

        std::string joinInts(const std::vector<int>& values) {
            std::ostringstream ss;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i != 0) ss << ",";
                ss << values[i];
            }
            return ss.str();
        }
    }

    GroupMetadata GroupMetadataParser::parseSaveString(const std::string& saveString) {
        GroupMetadata metadata;
        auto parts = StringUtil::split(saveString, ',');
        for (size_t i = 0; i + 1 < parts.size(); i += 2) {
            auto key = StringUtil::parseInt(parts[i]);
            if (!key) continue;

            auto values = extractPositiveInts(parts[i + 1]);
            if (values.empty()) continue;

            if (isObjectGroupKey(*key)) {
                appendUnique(metadata.objectGroups, values);
                metadata.fields.push_back({*key, values});
            } else if (isTargetGroupKey(*key)) {
                appendUnique(metadata.targetGroups, values);
                metadata.fields.push_back({*key, values});
            }
        }
        return metadata;
    }

    std::string GroupMetadataParser::summarize(const GroupMetadata& metadata) {
        if (metadata.empty()) return "groups=none";

        std::ostringstream ss;
        ss << "objectGroups=[" << joinInts(metadata.objectGroups) << "] "
           << "targetGroups=[" << joinInts(metadata.targetGroups) << "] "
           << "fields=" << metadata.fields.size();
        return ss.str();
    }

} // namespace ep2p
