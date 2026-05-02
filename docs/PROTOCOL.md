# Protocol

## Transport

All TCP messages use a 10-byte frame header followed by a variable-length payload:

```
[uint32 magic=0xED1C0110][uint16 type][uint32 payload_len][payload bytes...]
```

UDP presence datagrams use a compact fixed struct (no framing header, validated by magic).

## Encoding

V1 uses a simple text encoding: fields delimited by `|` within the payload string.
This trades compactness for debuggability during development.
A future milestone can replace this with packed binary structs without changing message types.

## Message Types

### Handshake

| Type | Direction | Description |
|---|---|---|
| `hello` (0x01) | Peer → Host | Initial connection request. Carries protocol version, session key, display name. |
| `hello_ack` (0x02) | Host → Peer | Accept or reject. Carries assigned peer ID, granted role, reject reason if denied. |
| `goodbye` (0x03) | Either | Clean disconnect notification with optional reason. |

### Keep-Alive

| Type | Direction | Description |
|---|---|---|
| `heartbeat` (0x04) | Either | Ping. Receiver replies with same message. No-reply within timeout = disconnect. |

### Presence (typically UDP)

| Type | Direction | Description |
|---|---|---|
| `presence_update` (0x10) | Either | Cursor world position + display name. Fire-and-forget. Includes sequence number for drop detection. |

### Object Operations

| Type | Direction | Description |
|---|---|---|
| `place_object` (0x20) | Peer → Host | Peer placed an object. Carries provisional client ID, GD object type, position. |
| `place_object_ack` (0x21) | Host → Peer | Confirms placement with canonical `NetworkObjectId`. Or denies with reason. |
| `lock_request` (0x22) | Peer → Host | Request exclusive edit lock on a `NetworkObjectId`. |
| `lock_granted` (0x23) | Host → Peer | Lock was granted. |
| `lock_denied` (0x24) | Host → Peer | Lock was denied. Carries reason (already locked, no permission, not found). |
| `edit_object` (0x25) | Host → All | Host broadcasts an in-progress edit (position/transform only, during drag). Reserved for future use. |
| `commit_edit` (0x26) | Peer → Host | Peer finalised an edit. Carries `NetworkObjectId` + new transform. |
| `unlock_object` (0x27) | Host → All | Host releases a lock after commit or peer disconnect. |
| `delete_object` (0x28) | Peer → Host | Peer requests deletion of a `NetworkObjectId`. |

Object payloads carry explicit transform fields plus the raw GD save string. Explicit
position, rotation, and scale fields are authoritative for live edits; non-transform
configuration such as groups and trigger targets is recovered from the save string.
Group IDs are global to the shared level and are never remapped by the protocol.

### Permissions

| Type | Direction | Description |
|---|---|---|
| `permission_update` (0x30) | Host → Peer | Host changed the peer's role/flags. |

### Save

| Type | Direction | Description |
|---|---|---|
| `save_request` (0x40) | Peer → Host | Peer requests that host saves the level. |
| `save_command` (0x41) | Host → Peer | Host instructs all TrustedBuilders to consider level saved (host executes save locally). |

### Activity

| Type | Direction | Description |
|---|---|---|
| `activity_event` (0x50) | Host → Peer | Host notifies peer of a loggable event (join, leave, placement, etc.). |

### State Sync

| Type | Direction | Description |
|---|---|---|
| `state_resync_request` (0x60) | Peer → Host | Peer suspects desync and requests a full snapshot. |
| `state_snapshot` (0x61) | Host → Peer | Full serialised object list. Peer rebuilds editor state from this. |

### Error

| Type | Direction | Description |
|---|---|---|
| `error` (0xFF) | Either | Protocol-level error. Carries error code + human-readable message. |

## Error Codes

| Code | Meaning |
|---|---|
| 0x01 | Protocol version mismatch |
| 0x02 | Invalid session key |
| 0x03 | Session full (V1: max 1 peer) |
| 0x04 | Permission denied |
| 0x05 | Object not found |
| 0x06 | Object already locked |
| 0xFF | Generic / unspecified error |
