# EditorP2P

Prototype Geode mod that adds direct P2P live collaboration to the Geometry Dash level editor.

## Goals

- Two players (host + peer) can open the same level in GD's editor simultaneously.
- The peer's cursor and name tag are visible to the host in real time.
- Object placement syncs after the object is released (not during drag).
- No public servers. No matchmaking service. Direct LAN or manual IP connection only.

## V1 Scope

- Windows only.
- 2 players maximum: one host, one peer.
- Host is authoritative - the host owns the canonical editor state.
- LAN discovery (UDP broadcast) + manual IP:port#sessionKey join.
- Milestone 1: mod compiles and loads in GD.
- Milestone 2: host/join UI works, handshake completes.
- Milestone 3: peer cursor appears in host editor (first playable proof).
- Milestone 4: object placement syncs after release.
- Milestone 5: object locking, permissions, save flow.

## Building

**Prerequisites**
- Geometry Dash (Steam) installed.
- [Geode](https://geode-sdk.org) installed (`geode sdk install`).
- CMake 3.21+, MSVC or Clang on Windows.

**Build**
```sh
geode build
```
Or manually:
```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The compiled `.geode` package appears in `build/` and can be dragged into GD's mods folder or installed via the Geode launcher.

**Testing with two instances**

Launch GD twice (Steam: right-click -> Manage -> Browse local files, run exe directly).
One instance hosts, the other joins via the in-editor collab button.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Protocol

See [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Milestones

See [docs/MILESTONES.md](docs/MILESTONES.md).

Current roadmap tracks include deterministic playback/replay tooling and group
collection/preservation for grouped objects and trigger configuration.
