#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ep2p {

    struct GroupField {
        int key = 0;
        std::vector<int> values;
    };

    struct GroupMetadata {
        std::vector<int> objectGroups;
        std::vector<int> targetGroups;
        std::vector<GroupField> fields;

        bool empty() const {
            return objectGroups.empty() && targetGroups.empty() && fields.empty();
        }
    };

    class GroupMetadataParser {
    public:
        static GroupMetadata parseSaveString(const std::string& saveString);
        static std::string summarize(const GroupMetadata& metadata);
    };

} // namespace ep2p
