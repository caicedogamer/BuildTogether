#pragma once

#include <EditorP2P/core/EditorOperation.hpp>

#include <vector>

namespace ep2p {

    struct PlaybackResult {
        int applied = 0;
        int skipped = 0;
        int failed = 0;
    };

    class OperationPlayback {
    public:
        static OperationPlayback& get();

        PlaybackResult replay(const std::vector<EditorOperation>& operations,
                              bool dryRun = false);
        bool apply(const EditorOperation& operation, bool dryRun = false);

    private:
        OperationPlayback() = default;
    };

} // namespace ep2p
