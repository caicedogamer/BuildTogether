#pragma once

#include <EditorP2P/editor/Presence.hpp>
#include <EditorP2P/core/Types.hpp>
#include <Geode/cocos/include/cocos2d.h>
#include <unordered_map>
#include <string>

namespace ep2p {

    // CCLayer drawn on top of the GD level editor (z-order 200).
    // Renders a colored dot + name label for each remote player cursor.
    // Added as a child of LevelEditorLayer in the EditorHooks LevelEditorLayer::init hook.
    //
    // World-space -> screen-space conversion happens in update() using the editor's
    // m_objectLayer transform. This correctly handles camera pan and zoom.
    class PresenceRenderer : public cocos2d::CCLayer {
    public:
        static PresenceRenderer* create();
        bool init() override;
        void update(float dt) override;

        // Called from EditorBridge::applyRemotePresence on the main thread.
        void updatePeer(const PresenceState& state);
        void removePeer(PlayerId playerId);
        void removeAllPeers();

    private:
        struct PeerNode {
            PlayerId             playerId;
            cocos2d::CCNode*        dot   = nullptr;  // colored cursor marker
            cocos2d::CCLabelBMFont* label = nullptr;  // name tag above the dot
        };

        void addPeerNode(const PresenceState& state);
        void updateNodePosition(PeerNode& node, float worldX, float worldY);
        void cullStalePeers();

        std::unordered_map<PlayerId, PresenceState> m_states;
        std::unordered_map<PlayerId, PeerNode>      m_nodes;
    };

} // namespace ep2p
