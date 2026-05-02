#include <EditorP2P/protocol/Message.hpp>
#include <EditorP2P/protocol/MessageTypes.hpp>

namespace ep2p {

    std::string_view messageTypeName(MessageType t) {
        switch (t) {
            case MessageType::Hello:              return "Hello";
            case MessageType::HelloAck:           return "HelloAck";
            case MessageType::Goodbye:            return "Goodbye";
            case MessageType::Heartbeat:          return "Heartbeat";
            case MessageType::PresenceUpdate:     return "PresenceUpdate";
            case MessageType::PlaceObject:        return "PlaceObject";
            case MessageType::PlaceObjectAck:     return "PlaceObjectAck";
            case MessageType::LockRequest:        return "LockRequest";
            case MessageType::LockGranted:        return "LockGranted";
            case MessageType::LockDenied:         return "LockDenied";
            case MessageType::EditObject:         return "EditObject";
            case MessageType::CommitEdit:         return "CommitEdit";
            case MessageType::UnlockObject:       return "UnlockObject";
            case MessageType::DeleteObject:       return "DeleteObject";
            case MessageType::PermissionUpdate:   return "PermissionUpdate";
            case MessageType::SaveRequest:        return "SaveRequest";
            case MessageType::SaveCommand:        return "SaveCommand";
            case MessageType::ActivityEvent:      return "ActivityEvent";
            case MessageType::StateResyncRequest: return "StateResyncRequest";
            case MessageType::StateSnapshot:      return "StateSnapshot";
            case MessageType::Error:              return "Error";
            default:                              return "Unknown";
        }
    }

} // namespace ep2p
