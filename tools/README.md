# Tools

Standalone Windows console utilities for testing networking without launching Geometry Dash.

## echo_server

A TCP server that accepts one connection on `CONTROL_PORT` (43720), parses incoming frames
using the same `MessageCodec`, logs each message type and payload to stdout, and replies with
a stub `HelloAck` (accepted=true, assignedPeerId=1, grantedRole=Builder).

**Purpose:** Test `PeerSession::connect()` and `MessageCodec` framing without a second GD instance.

**Build:** `cmake -B build_tools -S tools/echo_server && cmake --build build_tools`

---

## cursor_sim

Sends synthetic `PresenceDatagram` UDP packets to a target `ip:port` at 20 Hz, sweeping
`worldX` back and forth between 0 and 500 to simulate a moving peer cursor.

**Purpose:** Test `PresenceRenderer` cursor rendering without a second GD instance.

**Usage:** `cursor_sim.exe 192.168.1.25 43721 "TestPeer"`

---

## Adding a tool

1. Create `tools/<name>/CMakeLists.txt` with `add_executable(...)`.
2. Add `add_subdirectory(tools/<name>)` to the root `CMakeLists.txt` inside the
   `if(EP2P_BUILD_TESTS)` block.
3. Link only the modules that have no GD dependency (`net/`, `protocol/`, `util/`).
