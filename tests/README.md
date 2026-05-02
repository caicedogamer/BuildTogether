# Tests

Unit tests for modules that have no GD/Geode dependency.
These can be compiled and run without a Geometry Dash installation.

## Scope

| Module | What to test |
|---|---|
| `net/JoinCode` | `parse()` round-trip, malformed inputs, `generateKey()` uniqueness |
| `net/Endpoint` | `fromString()` with valid/invalid inputs |
| `protocol/MessageCodec` | Single frame encode/decode, frame split across multiple `feed()` calls, two frames in one `feed()`, bad magic rejected, oversized frame rejected |
| `protocol/Serializer` (future) | Encode/decode round-trip for each message type |
| `core/Permissions` | `permissionsForRole()` for every role, `toBits()` / `fromBits()` round-trip |
| `editor/ObjectRegistry` | Register, lookup, remove, clear, peer-only `bindObject` |
| `editor/ObjectLockManager` | `tryLock`, double-lock denied, unlock, `unlockAllForPlayer`, shadow ops |
| `util/StringUtil` | `split`, `join`, `trim`, `parseInt`, `parseFloat`, `parsePort` |
| `util/Clock` | `now()` monotonically increasing, `elapsed()`, `sessionMs()` after `markSessionStart()` |

## Build

Enable tests by passing `-DEP2P_BUILD_TESTS=ON` to CMake.
The current smoke tests use plain `assert` so they can run without a Geometry Dash installation.
To configure only the tests without the Geode SDK, also pass `-DEP2P_BUILD_MOD=OFF`.

## Rules

- No GD or Geode headers in test files.
- No `editor/EditorHooks`, `editor/EditorBridge`, or any `ui/` file in tests (they require GD).
- Stub `void*` in `ObjectRegistry` tests; do not use real `GameObject*`.

## Playback Fixtures

Regression operation streams live in `tests/fixtures/*.ops`. They cover drag placement,
multi-select warp, delete after warp, push/resync after edits, and trigger placement
with group/config fields. They are pipe-delimited debug fixtures for manual playback
and future bridge tests.
