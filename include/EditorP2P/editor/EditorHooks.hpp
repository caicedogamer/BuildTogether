#pragma once

// EditorHooks wires up Geode $modify hooks into the GD editor.
// The hook class definitions live in EditorHooks.cpp so this header stays free
// of GD binding includes.
//
// Hooks installed:
//   LevelEditorLayer::init         - attach PresenceRenderer and schedule SessionManager tick
//   EditorUI::init                 - attach editor UI hook state
//   EditorUI::keyDown              - open the EditorP2P menu with F9
//   LevelEditorLayer::removeObject - later forward deletions once network IDs exist
//   EditorUI::deselectAll          - treat as edit-commit for any locked object

namespace ep2p {

    // Call once from main.cpp to log hook setup and wire future runtime callbacks.
    // The $modify hooks register themselves automatically via static init.
    void installEditorHooks();

} // namespace ep2p
