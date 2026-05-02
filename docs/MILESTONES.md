# Milestones

## Milestone 1 — Compile and Load

**Goal:** Mod compiles with `geode build` and logs a message on load. No crashes.

Deliverables:
- `CMakeLists.txt` + `mod.json` valid.
- `main.cpp` logs "EditorP2P loaded".
- All scaffold headers included and compiling.
- `WSAStartup` called in `$execute`.

Acceptance: GD launches with the mod enabled. Geode mod list shows "EditorP2P 0.1.0".

---

## Milestone 2 — Host/Join UI + Handshake

**Goal:** In-editor collab button opens Host or Join popup. Two instances complete hello/hello_ack handshake.

Deliverables:
- `EditorHooks` injects collab button into EditorUI toolbar.
- `HostLayer` popup: room name, port, session key, "Start Hosting" button.
- `JoinLayer` popup: join code field, "Scan LAN" button, "Join" button.
- `HostSession` accepts one TCP connection and validates `hello`.
- `PeerSession` connects, sends `hello`, receives `hello_ack`.
- `SessionManager` transitions state machine correctly.
- `NotificationBanner` (or Geode notification) shows connect/disconnect events.

Acceptance: Two GD instances, peer popup → enter join code → both show "Connected".

---

## Milestone 3 — Peer Cursor / Name Tag (First Playable Proof)

**Goal:** Peer moves mouse in GD editor. Host sees a colored dot + name tag at that position.

Deliverables:
- `EditorHooks` samples cursor world position each frame.
- `PeerSession` sends `presence_update` via UDP at ~20 Hz.
- `HostSession` receives UDP datagrams, dispatches to main thread.
- `PresenceRenderer` (CCLayer over editor) draws dot + label per peer.
- Stale cursors fade after 2 seconds.

Acceptance: Move peer mouse → host sees cursor moving in real time with <150 ms lag on LAN.

---

## Milestone 4 — Object Placement Sync

**Goal:** Peer places an object. It appears in the host's editor with a host-assigned ID.
Host places an object. It appears in the peer's editor.

Deliverables:
- `EditorHooks` intercepts `addObject` on both sides.
- Peer sends `place_object` with provisional temp ID.
- Host assigns `NetworkObjectId`, replies with `place_object_ack`, broadcasts to all peers.
- `ObjectRegistry` on peer binds temp ID → canonical ID on ack.
- `RemoteOps::applyPlaceObject` creates the GD object on the receiving side.
- `isApplyingRemote` guard prevents hook re-fire loops.
- `ActivityLog` records each placement.

Acceptance: Both players place blocks that appear on each other's screens within ~300 ms on LAN.

---

## Milestone 5 — Locks, Permissions, Save Flow

**Goal:** Concurrent edits are safe. Roles restrict actions. Save is host-controlled.

Deliverables:
- `ObjectLockManager`: peer requests lock before editing; host grants or denies.
- Locked objects show a visual indicator (colored outline or label).
- Peer unlock on deselect → `commit_edit` → host applies → `unlock_object` broadcast.
- `PermissionsLayer`: host can change peer's role (Viewer / Builder / TrustedBuilder).
- `SaveGuard`: peer without `canSave` sees "Request Save" instead of direct save.
- `save_request` → host notification → host approves → `save_command` broadcast.
- `state_resync_request` / `state_snapshot` flow for manual desync recovery.

Acceptance: Role changes take effect immediately. Peer cannot save without permission.
Requesting save shows notification on host.

---

## Roadmap Track A - Deterministic Playback

**Goal:** Replace fragile live hook timing with a replayable operation stream. Every
placement, delete, edit, and resync action should be recordable, inspectable, and
replayable in order so desync bugs can be reproduced without guessing from screenshots.

### A1 - Operation Capture

Deliverables:
- Add an `EditorOperation` model with stable fields: operation type, local sequence,
  network object ID, GD object ID, explicit transform, save string, timestamp, source
  player, and origin (`hook`, `scanner`, `snapshot`, `resync`, `remote`).
- Capture local operations before sending TCP messages.
- Capture remote operations after decode but before applying them to GD.
- Log operation IDs in every placement/edit/delete line so local and remote logs can be
  matched one-to-one.
- Keep the operation stream in memory with a bounded ring buffer.

Acceptance:
- A single drag placement produces a readable ordered list of operations.
- Each `place_object` and `commit_edit` log line includes the same operation ID on both
  sender and receiver.
- The log makes it clear whether a missing object was never captured, never sent, never
  decoded, or failed during apply.

### A2 - Playback Runner

Deliverables:
- Add a debug-only playback runner that can replay captured `EditorOperation` records
  into `EditorBridge` without a live peer.
- Support replaying from an exported text file produced by the activity/debug log.
- Add a dry-run mode that decodes and validates operations without mutating the editor.
- Add guardrails so playback cannot run while connected to a live session unless a debug
  flag is enabled.

Acceptance:
- A captured failing drag-placement sequence can be replayed in one GD instance.
- Replay produces the same object count and transforms as the original receiver path.
- Invalid or unsupported operations fail with a precise log message instead of silently
  skipping.

### A3 - Playback-Based Regression Tests

Deliverables:
- Add fixture files for known bug cases: drag placement, multi-select warp, delete after
  warp, push/resync after edits, trigger placement with config.
- Add tests for codec round-trip and operation ordering.
- Add a small bridge test layer where possible; where GD runtime is required, document
  manual playback test steps.

Acceptance:
- Known failing operation streams stay in the repo as regression fixtures.
- A developer can run one command to verify codec/order invariants.
- Manual GD playback steps are short enough to run before packaging a test build.

---

## Roadmap Track B - Group Collection and Preservation

**Goal:** Collect and preserve group IDs and group-related trigger/object configuration
instead of only syncing geometry. Objects should arrive with the same group memberships,
target groups, parent groups, color groups, and trigger references whenever GD exposes
them through save strings or typed fields.

### B1 - Group Field Inventory

Deliverables:
- Document the GD save-string keys used by the object types we care about first:
  blocks, spikes, portals, triggers, color objects, move/alpha/toggle/spawn triggers.
- Identify which keys represent object membership groups and which represent trigger
  target groups.
- Add parser helpers for comma-pair save strings that can read repeated or compound
  group fields safely.
- Add logs that summarize group metadata for placed/edited objects in debug builds.

Acceptance:
- For a selected object, debug logs can show `objectGroups`, `targetGroups`, and
  `specialRefs` without changing editor state.
- Trigger objects no longer appear as opaque save strings during debugging.

### B2 - Network Payload Support

Deliverables:
- Extend the operation model to carry parsed group metadata alongside the raw save string.
- Keep raw save string as the source of truth for unsupported fields, but make parsed
  group data available for validation and conflict handling.
- Add protocol versioning or capability flags if payload shape changes.
- Add codec tests for objects with multiple groups and trigger target groups.

Acceptance:
- A block with multiple group IDs round-trips through encode/decode unchanged.
- A move/toggle/spawn trigger preserves its target group references through the network
  payload.
- Older peers fail gracefully or ignore the optional group section without crashing.

### B3 - Apply and Resync Semantics

Deliverables:
- During live placement, create the object with safe explicit fields, then apply safe
  group/config fields from the save string or parsed metadata.
- During commit edits, keep explicit transform authoritative and apply only non-transform
  group/config changes from save string.
- During snapshots, rebuild from save strings but validate parsed group metadata against
  the created object.
- Add warnings when a group field could not be applied, including object ID and network ID.

Acceptance:
- Blocks keep membership groups after live placement and after resync.
- Triggers keep target group configuration after live placement and after resync.
- Transform sync cannot be corrupted by stale save-string coordinates.

### B4 - Group Conflict and Remap Rules

Deliverables:
- Decide whether group IDs are global shared IDs or need per-peer remapping.
- If global, document that group IDs are level-authoritative and must not be remapped.
- If remapped, add a `GroupRegistry` with peer-local to host-canonical mappings.
- Add push/resync handling that never duplicates or strips group IDs.

Acceptance:
- Two players can create grouped objects without accidentally stealing or overwriting
  each other's group references.
- Push/resync preserves the same group topology on both editors.
- Group handling behavior is documented in `docs/ARCHITECTURE.md` and `docs/PROTOCOL.md`.
