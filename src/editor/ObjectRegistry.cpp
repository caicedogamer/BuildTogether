#include <EditorP2P/editor/ObjectRegistry.hpp>
#include <EditorP2P/config/BuildConfig.hpp>

#include <cmath>
#include <utility>

namespace ep2p {

    namespace {
        bool sameObjectId(const ObjectRegistry::ObjectFingerprint& a,
                          const ObjectRegistry::ObjectFingerprint& b) {
            return a.objectId <= 0 || b.objectId <= 0 || a.objectId == b.objectId;
        }

        bool closeEnough(float a, float b, float epsilon) {
            return std::abs(a - b) <= epsilon;
        }

        bool transformMatches(const ObjectRegistry::ObjectFingerprint& a,
                              const ObjectRegistry::ObjectFingerprint& b) {
            return closeEnough(a.x, b.x, 0.5f) &&
                   closeEnough(a.y, b.y, 0.5f) &&
                   closeEnough(a.rotation, b.rotation, 0.01f) &&
                   closeEnough(a.scaleX, b.scaleX, 0.001f) &&
                   closeEnough(a.scaleY, b.scaleY, 0.001f);
        }
    }

    ObjectRegistry& ObjectRegistry::get() {
        static ObjectRegistry instance;
        return instance;
    }

    NetworkObjectId ObjectRegistry::registerObject(void* gdObject) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!gdObject) return INVALID_OBJECT_ID;

        // Return existing ID if already registered (idempotent).
        auto it = m_byPtr.find(gdObject);
        if (it != m_byPtr.end()) return it->second;

        NetworkObjectId id = m_nextId++;
        m_byId[id]       = gdObject;
        m_byPtr[gdObject] = id;
        return id;
    }

    void ObjectRegistry::bindObject(NetworkObjectId netId, void* gdObject) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!gdObject || netId == INVALID_OBJECT_ID) return;

        auto existingPtr = m_byId.find(netId);
        if (existingPtr != m_byId.end() && existingPtr->second != gdObject) {
            m_byPtr.erase(existingPtr->second);
        }

        auto existingId = m_byPtr.find(gdObject);
        if (existingId != m_byPtr.end() && existingId->second != netId) {
            m_fingerprints.erase(existingId->second);
            m_byId.erase(existingId->second);
        }

        m_byId[netId]     = gdObject;
        m_byPtr[gdObject] = netId;
    }

    void* ObjectRegistry::findByNetworkId(NetworkObjectId netId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_byId.find(netId);
        return (it != m_byId.end()) ? it->second : nullptr;
    }

    NetworkObjectId ObjectRegistry::findNetworkId(void* gdObject) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_byPtr.find(gdObject);
        return (it != m_byPtr.end()) ? it->second : INVALID_OBJECT_ID;
    }

    NetworkObjectId ObjectRegistry::findLikelyNetworkId(
        const ObjectFingerprint& fingerprint,
        MatchStats* stats
    ) const {
        std::lock_guard<std::mutex> lock(m_mutex);

        MatchStats localStats;
        localStats.registryCount = m_byId.size();

        NetworkObjectId exactSaveStringId = INVALID_OBJECT_ID;
        NetworkObjectId transformId = INVALID_OBJECT_ID;
        for (const auto& [netId, registered] : m_fingerprints) {
            if (!sameObjectId(fingerprint, registered)) continue;
            ++localStats.objectIdMatches;

            if (!fingerprint.saveString.empty() &&
                fingerprint.saveString == registered.saveString) {
                ++localStats.exactSaveStringMatches;
                exactSaveStringId =
                    localStats.exactSaveStringMatches == 1 ? netId : INVALID_OBJECT_ID;
            }

            if (transformMatches(fingerprint, registered)) {
                ++localStats.transformMatches;
                transformId =
                    localStats.transformMatches == 1 ? netId : INVALID_OBJECT_ID;
            }
        }

        if (stats) *stats = localStats;

        if (localStats.exactSaveStringMatches == 1) return exactSaveStringId;
        if (localStats.transformMatches == 1) return transformId;
        return INVALID_OBJECT_ID;
    }

    bool ObjectRegistry::hasNetworkId(NetworkObjectId netId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_byId.count(netId) != 0;
    }

    bool ObjectRegistry::hasObject(void* gdObject) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_byPtr.count(gdObject) != 0;
    }

    void ObjectRegistry::updateFingerprint(NetworkObjectId netId, ObjectFingerprint fingerprint) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (netId == INVALID_OBJECT_ID || m_byId.count(netId) == 0) return;
        m_fingerprints[netId] = std::move(fingerprint);
    }

    void ObjectRegistry::removeByNetworkId(NetworkObjectId netId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_byId.find(netId);
        if (it == m_byId.end()) return;
        m_byPtr.erase(it->second);
        m_fingerprints.erase(netId);
        m_byId.erase(it);
    }

    void ObjectRegistry::removeByObject(void* gdObject) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_byPtr.find(gdObject);
        if (it == m_byPtr.end()) return;
        m_fingerprints.erase(it->second);
        m_byId.erase(it->second);
        m_byPtr.erase(it);
    }

    void ObjectRegistry::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_byId.clear();
        m_byPtr.clear();
        m_fingerprints.clear();
        // Do NOT reset m_nextId — IDs are already in flight on the network.
    }

    size_t ObjectRegistry::count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_byId.size();
    }

    void ObjectRegistry::forEach(std::function<void(NetworkObjectId, void*)> fn) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, ptr] : m_byId) {
            fn(id, ptr);
        }
    }

} // namespace ep2p
