#include <EditorP2P/core/EditorOperation.hpp>

namespace ep2p {

    EditorOperationRecorder& EditorOperationRecorder::get() {
        static EditorOperationRecorder instance;
        return instance;
    }

    uint64_t EditorOperationRecorder::record(EditorOperation operation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        operation.id = m_nextId++;
        if (operation.saveString.size() > 4096) {
            operation.saveString.resize(4096);
        }
        m_operations.push_back(std::move(operation));
        while (m_operations.size() > MAX_OPERATIONS) {
            m_operations.pop_front();
        }
        return m_operations.back().id;
    }

    std::vector<EditorOperation> EditorOperationRecorder::snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_operations.begin(), m_operations.end()};
    }

    void EditorOperationRecorder::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_operations.clear();
    }

    const char* operationTypeName(EditorOperationType type) {
        switch (type) {
            case EditorOperationType::Place: return "place";
            case EditorOperationType::CommitEdit: return "commit_edit";
            case EditorOperationType::Delete: return "delete";
            case EditorOperationType::SnapshotCreate: return "snapshot_create";
            case EditorOperationType::SnapshotUpdate: return "snapshot_update";
            case EditorOperationType::SnapshotDelete: return "snapshot_delete";
            default: return "unknown";
        }
    }

    const char* operationDirectionName(EditorOperationDirection direction) {
        switch (direction) {
            case EditorOperationDirection::LocalSend: return "local_send";
            case EditorOperationDirection::RemoteReceive: return "remote_receive";
            case EditorOperationDirection::LocalApply: return "local_apply";
            default: return "unknown";
        }
    }

} // namespace ep2p
