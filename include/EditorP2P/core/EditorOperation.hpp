#pragma once

#include <EditorP2P/config/BuildConfig.hpp>
#include <EditorP2P/core/Types.hpp>
#include <EditorP2P/protocol/GroupMetadata.hpp>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace ep2p {

    enum class EditorOperationType {
        Place,
        CommitEdit,
        Delete,
        SnapshotCreate,
        SnapshotUpdate,
        SnapshotDelete
    };

    enum class EditorOperationDirection {
        LocalSend,
        RemoteReceive,
        LocalApply
    };

    struct EditorOperation {
        uint64_t id = 0;
        EditorOperationType type = EditorOperationType::Place;
        EditorOperationDirection direction = EditorOperationDirection::LocalSend;
        NetworkObjectId netId = INVALID_OBJECT_ID;
        uint16_t gdObjectId = 0;
        float x = 0.f;
        float y = 0.f;
        float rotation = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        uint32_t playerId = 0;
        uint32_t messageSequence = 0;
        std::string origin;
        std::string saveString;
        GroupMetadata groups;
    };

    class EditorOperationRecorder {
    public:
        static EditorOperationRecorder& get();

        uint64_t record(EditorOperation operation);
        std::vector<EditorOperation> snapshot() const;
        void clear();

    private:
        mutable std::mutex m_mutex;
        std::deque<EditorOperation> m_operations;
        uint64_t m_nextId = 1;
        static constexpr size_t MAX_OPERATIONS = 512;
    };

    const char* operationTypeName(EditorOperationType type);
    const char* operationDirectionName(EditorOperationDirection direction);

} // namespace ep2p
