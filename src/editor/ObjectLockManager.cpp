#include <EditorP2P/editor/ObjectLockManager.hpp>
#include <EditorP2P/config/BuildConfig.hpp>

namespace ep2p {

    ObjectLockManager& ObjectLockManager::get() {
        static ObjectLockManager instance;
        return instance;
    }

    bool ObjectLockManager::tryLock(NetworkObjectId netId, PlayerId requesterId,
                                     LockDenyReason& outReason)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_locks.find(netId);
        if (it != m_locks.end()) {
            outReason = LockDenyReason::AlreadyLocked;
            return false;
        }
        m_locks[netId] = requesterId;
        return true;
    }

    void ObjectLockManager::unlock(NetworkObjectId netId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_locks.erase(netId);
    }

    void ObjectLockManager::unlockAllForPlayer(PlayerId playerId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_locks.begin(); it != m_locks.end(); ) {
            if (it->second == playerId) it = m_locks.erase(it);
            else ++it;
        }
    }

    bool ObjectLockManager::isLocked(NetworkObjectId netId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_locks.count(netId) != 0;
    }

    PlayerId ObjectLockManager::lockHolder(NetworkObjectId netId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_locks.find(netId);
        return (it != m_locks.end()) ? it->second : INVALID_PLAYER_ID;
    }

    void ObjectLockManager::shadowLock(NetworkObjectId netId, PlayerId holder) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shadow[netId] = holder;
    }

    void ObjectLockManager::shadowUnlock(NetworkObjectId netId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shadow.erase(netId);
    }

    bool ObjectLockManager::shadowIsLocked(NetworkObjectId netId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_shadow.count(netId) != 0;
    }

    std::vector<NetworkObjectId> ObjectLockManager::lockedBy(PlayerId playerId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<NetworkObjectId> result;
        for (const auto& [netId, holder] : m_locks) {
            if (holder == playerId) result.push_back(netId);
        }
        return result;
    }

    void ObjectLockManager::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_locks.clear();
        m_shadow.clear();
    }

} // namespace ep2p
