#pragma once

#include <EditorP2P/core/Types.hpp>
#include <string>
#include <cstdint>

namespace ep2p {

    // The live state of one remote player's cursor in the editor.
    struct PresenceState {
        PlayerId    playerId    = 0;
        std::string displayName;
        float       editorX    = 0.f;   // world-space coordinates inside the GD editor
        float       editorY    = 0.f;
        uint32_t    colorRgb   = 0xFF5050u;  // default red; assigned from ColorPalette
        TimestampMs lastUpdate = 0;      // ms timestamp of the last received update
        uint32_t    sequence   = 0;      // drop outdated UDP packets if seq < lastSeq
    };

} // namespace ep2p
