# Architecture

## Overview

EditorP2P is a host-authoritative, direct P2P mod. There is no central server.
One player hosts; one player joins. The host owns the canonical level state.

```
┌─────────────────────────┐        TCP (control)        ┌─────────────────────────┐
│        HOST GD          │◄───────────────────────────►│        PEER GD          │
│                         │        UDP (presence)        │                         │
│  LevelEditorLayer       │◄───────────────────────────►│  LevelEditorLayer       │
│    └─ PresenceRenderer  │                              │    └─ PresenceRenderer  │
│  SessionManager         │                              │  SessionManager         │
│  ObjectRegistry (auth.) │                              │  ObjectRegistry (mirror)│
│  LockManager (auth.)    │                              │  LockManager (shadow)   │
└─────────────────────────┘                              └─────────────────────────┘
```

## Module Map

| Module | Responsibility |
|---|---|
| `core/` | Session lifecycle, permissions, activity log. No GD/Geode dependencies. |
| `net/` | Transport abstraction, Windows socket impl, LAN discovery, heartbeat. No GD/Geode. |
| `protocol/` | Message types, codec (encode/decode). No GD/Geode. |
| `editor/` | Hooks into GD editor, object registry, lock manager, presence bridge. GD/Geode OK. |
| `ui/` | Geode popup layers for host/join/permissions/activity log. Geode only. |
| `util/` | Clock, Result type, string utilities. No GD/Geode. |
| `config/` | Compile-time constants and runtime settings. No GD/Geode. |

## Transport

- **TCP** on `CONTROL_PORT` (default 43720): reliable ordered delivery for all control
  and object messages (hello, place_object, lock_*, save_*, etc.).
- **UDP** on `PRESENCE_PORT` (default 43721): fire-and-forget presence updates (cursor
  position). Stale presence is culled by timestamp on the receiver side.
- LAN discovery uses UDP broadcast on `PRESENCE_PORT`.

## Host-Authoritative Rules

1. The host assigns all canonical `NetworkObjectId` values. Peers use provisional IDs
   until the host confirms placement via `place_object_ack`.
2. All object locks are granted or denied by the host.
3. Object state edits are not applied locally until the host broadcasts the commit.
4. The host can send a full `state_snapshot` at any time to resync a desynced peer.
5. GD group IDs are level-authoritative global IDs. EditorP2P does not remap them per
   peer; object membership groups, target groups, color groups, and trigger references
   are preserved as authored in the shared level save strings.

## Threading

- All network I/O runs on background `std::thread`s.
- All Cocos/GD API calls happen on the main thread only.
- `MainThreadDispatch` (in `SessionManager`) provides a thread-safe queue that drains
  on the Cocos scheduler tick.

## V1 Limitations

- 2 players maximum (1 host + 1 peer). The `MAX_PEERS` constant gates this.
- IPv4 only.
- No NAT traversal. LAN or manually port-forwarded connections only.
- No persistence across GD sessions. State is in-memory only.
