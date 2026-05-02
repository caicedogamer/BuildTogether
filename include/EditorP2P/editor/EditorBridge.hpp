#pragma once

#include <EditorP2P/editor/Presence.hpp>
#include <EditorP2P/core/Types.hpp>
#include <string>
#include <vector>

namespace ep2p {

    // EditorBridge is the boundary between the networking/session layer and the GD editor.
    // It converts SessionManager events into GD editor API calls (and vice versa).
    // All methods must be called on the main (Cocos) thread.
    //
    // GD-specific code lives here and in EditorHooks — nowhere else.
    class EditorBridge {
    public:
        static EditorBridge& get();

        // Called once when a session becomes Connected.
        void onSessionConnected();

        // Called when the session ends.
        void onSessionDisconnected();

        // Presence — called by SessionManager when a PresenceUpdate arrives.
        void applyRemotePresence(const PresenceState& state);
        void removeRemotePresence(PlayerId playerId);

        // Object placement — called by SessionManager after the host confirms placement.
        // TODO: signature will use GD types (GameObject* or placement params) in Milestone 4.
        void* applyRemotePlacement(NetworkObjectId netId, uint16_t gdObjectId,
                                  float x, float y, float rotation,
                                  float scaleX, float scaleY,
                                  const std::string& saveString = {});

        // Object deletion — called by SessionManager when a delete is confirmed.
        void applyRemoteDeletion(NetworkObjectId netId);

        // Object edit commit — called by SessionManager when a commit is confirmed.
        // useSaveStringTransform=false (default): explicit x/y/rotation/scale are
        //   authoritative; saveString is carried for future non-transform use but its
        //   position/rotation/scale keys are NOT applied (live CommitEdit path).
        // useSaveStringTransform=true: transform is read from saveString keys instead
        //   (snapshot/resync reconstruction path where explicit params are unavailable).
        void applyRemoteEdit(NetworkObjectId netId,
                             float x, float y, float rotation,
                             float scaleX, float scaleY,
                             const std::string& saveString = {},
                             bool useSaveStringTransform = false);

        // Returns the current cursor world position (sampled by EditorHooks each frame).
        float cursorWorldX() const { return m_cursorX; }
        float cursorWorldY() const { return m_cursorY; }
        void  setCursorWorld(float x, float y) { m_cursorX = x; m_cursorY = y; }

    private:
        EditorBridge() = default;

        float m_cursorX = 0.f;
        float m_cursorY = 0.f;
    };

} // namespace ep2p
