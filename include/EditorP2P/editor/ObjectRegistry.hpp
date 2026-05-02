#pragma once

#include <EditorP2P/core/Types.hpp>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>

namespace ep2p {

    // Bidirectional map between canonical NetworkObjectIds and GD GameObject pointers.
    //
    // OWNERSHIP RULES:
    //   Host: calls registerObject() to assign a new ID to a newly placed object.
    //   Peer: calls bindObject() to link an existing GD object to a host-assigned ID.
    //   Neither side calls the other's method.
    //
    // The registry does NOT own the GameObject* — those are owned by GD's LevelEditorLayer.
    // Entries must be removed via removeBy*() whenever an object is deleted or the level
    // is reloaded (e.g. after undo/redo invalidates all pointers).
    class ObjectRegistry {
    public:
        struct ObjectFingerprint {
            int objectId = 0;
            float x = 0.f;
            float y = 0.f;
            float rotation = 0.f;
            float scaleX = 1.f;
            float scaleY = 1.f;
            std::string saveString;
        };

        struct MatchStats {
            size_t registryCount = 0;
            size_t objectIdMatches = 0;
            size_t exactSaveStringMatches = 0;
            size_t transformMatches = 0;
        };

        static ObjectRegistry& get();

        // Host only: assign a new NetworkObjectId to a GD object.
        NetworkObjectId registerObject(void* gdObject);  // TODO: typed as GameObject*

        // Peer only: bind a host-confirmed ID to a locally created GD object.
        void bindObject(NetworkObjectId netId, void* gdObject);

        void*           findByNetworkId(NetworkObjectId netId) const;
        NetworkObjectId findNetworkId(void* gdObject) const;
        NetworkObjectId findLikelyNetworkId(
            const ObjectFingerprint& fingerprint,
            MatchStats* stats = nullptr
        ) const;
        bool            hasNetworkId(NetworkObjectId netId) const;
        bool            hasObject(void* gdObject) const;

        void updateFingerprint(NetworkObjectId netId, ObjectFingerprint fingerprint);

        void removeByNetworkId(NetworkObjectId netId);
        void removeByObject(void* gdObject);

        void   clear();
        size_t count() const;

        // Iterate all entries. Useful when building a StateSnapshot.
        void forEach(std::function<void(NetworkObjectId, void*)> fn) const;

    private:
        ObjectRegistry() = default;

        mutable std::mutex                              m_mutex;
        std::unordered_map<NetworkObjectId, void*>      m_byId;
        std::unordered_map<void*, NetworkObjectId>      m_byPtr;
        std::unordered_map<NetworkObjectId, ObjectFingerprint> m_fingerprints;
        NetworkObjectId                                 m_nextId = 1;  // host only
    };

} // namespace ep2p
