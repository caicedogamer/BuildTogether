#pragma once

#include <EditorP2P/core/Types.hpp>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace ep2p {

    enum class LockDenyReason : uint8_t {
        AlreadyLocked  = 0x01,
        ObjectNotFound = 0x02,
        NoPermission   = 0x03,
    };

    // Host-authoritative lock table.
    // Only the host mutates m_locks (tryLock / unlock).
    // Peers maintain a shadow copy via shadowLock / shadowUnlock for local UI feedback
    // (e.g. showing a visual indicator on locked objects before the round-trip completes).
    class ObjectLockManager {
    public:
        static ObjectLockManager& get();

        // Host only — authoritative operations.
        bool tryLock(NetworkObjectId netId, PlayerId requesterId, LockDenyReason& outReason);
        void unlock(NetworkObjectId netId);
        void unlockAllForPlayer(PlayerId playerId);  // call on peer disconnect

        bool     isLocked(NetworkObjectId netId) const;
        PlayerId lockHolder(NetworkObjectId netId) const;  // INVALID_PLAYER_ID if unlocked

        // Peer only — shadow copy updated by LockGranted / LockDenied / UnlockObject messages.
        void shadowLock(NetworkObjectId netId, PlayerId holder);
        void shadowUnlock(NetworkObjectId netId);
        bool shadowIsLocked(NetworkObjectId netId) const;

        // Returns all objects currently locked by a given player.
        std::vector<NetworkObjectId> lockedBy(PlayerId playerId) const;

        void clear();

    private:
        ObjectLockManager() = default;

        mutable std::mutex                            m_mutex;
        std::unordered_map<NetworkObjectId, PlayerId> m_locks;   // authoritative (host)
        std::unordered_map<NetworkObjectId, PlayerId> m_shadow;  // mirror (peer)
    };

} // namespace ep2p
