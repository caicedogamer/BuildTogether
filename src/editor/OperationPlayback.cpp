#include <EditorP2P/editor/OperationPlayback.hpp>
#include <EditorP2P/editor/EditorBridge.hpp>

#include <Geode/Geode.hpp>

namespace ep2p {

    OperationPlayback& OperationPlayback::get() {
        static OperationPlayback instance;
        return instance;
    }

    PlaybackResult OperationPlayback::replay(
        const std::vector<EditorOperation>& operations,
        bool dryRun
    ) {
        PlaybackResult result;
        for (const auto& operation : operations) {
            if (apply(operation, dryRun)) {
                ++result.applied;
            } else {
                ++result.failed;
            }
        }
        return result;
    }

    bool OperationPlayback::apply(const EditorOperation& operation, bool dryRun) {
        if (operation.netId == INVALID_OBJECT_ID) {
            geode::log::warn("[EditorP2P] Playback skipped op={} with invalid netId", operation.id);
            return false;
        }

        geode::log::info(
            "[EditorP2P] Playback {} op={} type={} netId={} pos=({}, {})",
            dryRun ? "dry-run" : "apply",
            operation.id,
            operationTypeName(operation.type),
            operation.netId,
            operation.x,
            operation.y
        );
        if (dryRun) return true;

        switch (operation.type) {
            case EditorOperationType::Place:
            case EditorOperationType::SnapshotCreate:
                return EditorBridge::get().applyRemotePlacement(
                    operation.netId,
                    operation.gdObjectId,
                    operation.x,
                    operation.y,
                    operation.rotation,
                    operation.scaleX,
                    operation.scaleY,
                    operation.saveString
                ) != nullptr;

            case EditorOperationType::CommitEdit:
                EditorBridge::get().applyRemoteEdit(
                    operation.netId,
                    operation.x,
                    operation.y,
                    operation.rotation,
                    operation.scaleX,
                    operation.scaleY,
                    operation.saveString,
                    false
                );
                return true;

            case EditorOperationType::SnapshotUpdate:
                EditorBridge::get().applyRemoteEdit(
                    operation.netId,
                    operation.x,
                    operation.y,
                    operation.rotation,
                    operation.scaleX,
                    operation.scaleY,
                    operation.saveString,
                    true
                );
                return true;

            case EditorOperationType::Delete:
            case EditorOperationType::SnapshotDelete:
                EditorBridge::get().applyRemoteDeletion(operation.netId);
                return true;

            default:
                return false;
        }
    }

} // namespace ep2p
