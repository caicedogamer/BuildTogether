#include <EditorP2P/editor/EditorBridge.hpp>
#include <EditorP2P/editor/ObjectRegistry.hpp>
#include <EditorP2P/util/StringUtil.hpp>
#include <EditorP2P/ui/PresenceRenderer.hpp>
#include <Geode/Geode.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include <optional>
#include <vector>

// EditorBridge connects SessionManager events to live GD editor state.
// All methods MUST be called on the main (Cocos) thread.

namespace ep2p {

    namespace {
        constexpr int EP2P_PRESENCE_RENDERER_TAG = 4372001;

        PresenceRenderer* getPresenceRenderer() {
            auto* lel = LevelEditorLayer::get();
            if (!lel) return nullptr;

            auto* node = lel->getChildByTag(EP2P_PRESENCE_RENDERER_TAG);
            return static_cast<PresenceRenderer*>(node);
        }

        PresenceRenderer* ensurePresenceRenderer() {
            auto* lel = LevelEditorLayer::get();
            if (!lel) return nullptr;

            if (auto* existing = getPresenceRenderer()) {
                return existing;
            }

            auto* renderer = PresenceRenderer::create();
            if (!renderer) return nullptr;

            renderer->setTag(EP2P_PRESENCE_RENDERER_TAG);
            lel->addChild(renderer, 200);
            return renderer;
        }

        int objectIdFromSaveString(const std::string& saveString) {
            auto parts = StringUtil::split(saveString, ',');
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                auto key = StringUtil::parseInt(parts[i]);
                if (key && *key == 1) {
                    auto id = StringUtil::parseInt(parts[i + 1]);
                    if (id && *id > 0) {
                        return *id;
                    }
                    return 0;
                }
            }
            return 0;
        }

        std::optional<float> floatFieldFromSaveString(
            const std::string& saveString, int wantedKey
        ) {
            auto parts = StringUtil::split(saveString, ',');
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                auto key = StringUtil::parseInt(parts[i]);
                if (key && *key == wantedKey) {
                    return StringUtil::parseFloat(parts[i + 1]);
                }
            }
            return std::nullopt;
        }

        std::optional<int> intFieldFromSaveString(
            const std::string& saveString, int wantedKey
        ) {
            auto f = floatFieldFromSaveString(saveString, wantedKey);
            if (!f) return std::nullopt;
            return static_cast<int>(*f);
        }

        std::optional<std::string> stringFieldFromSaveString(
            const std::string& saveString, int wantedKey
        ) {
            auto parts = StringUtil::split(saveString, ',');
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                auto key = StringUtil::parseInt(parts[i]);
                if (key && *key == wantedKey) {
                    return parts[i + 1];
                }
            }
            return std::nullopt;
        }

        std::vector<int> positiveIntsFromGroupList(const std::string& value) {
            std::vector<int> result;
            std::string current;
            auto flushCurrent = [&]() {
                if (current.empty()) return;

                auto parsed = StringUtil::parseInt(current);
                if (parsed && *parsed > 0) {
                    bool seen = false;
                    for (int existing : result) {
                        if (existing == *parsed) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) result.push_back(*parsed);
                }
                current.clear();
            };

            for (char ch : value) {
                if (ch >= '0' && ch <= '9') {
                    current.push_back(ch);
                    continue;
                }
                flushCurrent();
            }
            flushCurrent();
            return result;
        }

        void applyObjectGroupsFromSaveString(GameObject* object,
                                             const std::string& saveString) {
            if (!object || saveString.empty()) return;

            std::vector<int> oldGroups;
            if (object->m_groups && object->m_groupCount > 0) {
                oldGroups.reserve(object->m_groupCount);
                for (short i = 0; i < object->m_groupCount && i < 10; ++i) {
                    oldGroups.push_back((*object->m_groups)[i]);
                }
            }
            for (int group : oldGroups) {
                if (group > 0) object->removeFromGroup(group);
            }

            auto groupField = stringFieldFromSaveString(saveString, 57);
            auto newGroups = groupField ? positiveIntsFromGroupList(*groupField)
                                        : std::vector<int>{};
            for (int group : newGroups) {
                object->addToGroup(group);
            }

            geode::log::debug(
                "[EditorP2P] applied object groups count={} from key57={}",
                newGroups.size(),
                groupField.value_or("")
            );
        }

        // Call customObjectSetup on an already-created GameObject using
        // key-indexed vectors built from a GD save string.  This propagates
        // trigger-specific properties (groups, duration, ease, colors, etc.)
        // that createObject() does not apply.
        //
        // GD expects:
        //   values[key]  = value string for that save-string key (empty if absent)
        //   exists[key]  = &values[key] if the key was present, nullptr if absent
        //
        // Must be called before explicit transform overrides so that position
        // keys inside the save string do not win over the authoritative wire params.
        void applyCustomSetupFromSaveString(GameObject* object,
                                            const std::string& saveString) {
            if (!object || saveString.empty()) return;

            auto parts = StringUtil::split(saveString, ',');
            if (parts.size() < 2) return;

            // First pass: find the maximum key present in the save string.
            size_t maxKey = 0;
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                auto k = StringUtil::parseInt(parts[i]);
                if (k && *k > 0 && static_cast<size_t>(*k) > maxKey)
                    maxKey = static_cast<size_t>(*k);
            }
            if (maxKey == 0) return;

            // customObjectSetup accesses fixed indices unconditionally (e.g. key 21
            // for color channel, key 44 for blending, key 57 for groups).  If the
            // save string only has keys up to, say, 32, a 33-element vector leaves
            // those fixed accesses reading garbage beyond the allocation — causing the
            // 0xFFFFFFFFFFFFFFFF crash seen in atol.  Always allocate at least 1024
            // entries so every index GD may touch is within bounds and null-initialised.
            static constexpr size_t MIN_VECTOR_SIZE = 1024;
            size_t vecSize = std::max(maxKey + 1, MIN_VECTOR_SIZE);

            // exists[key] must be a const char* (stored as void*): GD passes each
            // entry directly to atol/atof, not as a gd::string*.  Build values first
            // (final size, no further resizes) then point exists entries at c_str().
            gd::vector<gd::string> values(vecSize);
            gd::vector<void*>      exists(vecSize, nullptr);

            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                auto k = StringUtil::parseInt(parts[i]);
                if (k && *k > 0 && static_cast<size_t>(*k) < vecSize)
                    values[static_cast<size_t>(*k)] = parts[i + 1];
            }
            // c_str() for SSO strings points into the vector's own storage; safe as
            // long as values is alive and not resized, which we guarantee above.
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                auto k = StringUtil::parseInt(parts[i]);
                if (k && *k > 0 && static_cast<size_t>(*k) < vecSize)
                    exists[static_cast<size_t>(*k)] =
                        const_cast<char*>(values[static_cast<size_t>(*k)].c_str());
            }

            object->customObjectSetup(values, exists);
        }

        // Apply layer-related properties from a GD object save string to an
        // already-created GameObject.  Must be called after createObject() so the
        // node is already in the scene; only direct field writes + setCustomZLayer
        // are used (no re-parenting) so it is safe to call at any time.
        //
        // Save-string keys used:
        //   20 = Editor Layer 1  (m_editorLayer, short)
        //   61 = Editor Layer 2  (m_editorLayer2, short)
        //   24 = Z Layer         (m_zLayer, ZLayer enum int value)
        //   25 = Z Order         (m_zOrder, int)
        void applyLayerFieldsFromSaveString(GameObject* object,
                                            const std::string& saveString) {
            if (!object || saveString.empty()) return;

            if (auto v = intFieldFromSaveString(saveString, 20))
                object->m_editorLayer = static_cast<short>(*v);

            if (auto v = intFieldFromSaveString(saveString, 61))
                object->m_editorLayer2 = static_cast<short>(*v);

            if (auto v = intFieldFromSaveString(saveString, 24))
                object->setCustomZLayer(*v);

            if (auto v = intFieldFromSaveString(saveString, 25))
                object->m_zOrder = *v;

            // Refresh the editor layer tint so the object appears at full opacity
            // when it matches the active layer and dimmed when it does not.
            // updateObjectColors reads lel->m_currentLayer and applies the correct
            // color/opacity; without this call, directly writing m_editorLayer has
            // no visual effect.
            if (auto* lel = LevelEditorLayer::get()) {
                auto* arr = cocos2d::CCArray::createWithObject(object);
                lel->updateObjectColors(arr);
            }
        }
    }

    EditorBridge& EditorBridge::get() {
        static EditorBridge instance;
        return instance;
    }

    void EditorBridge::onSessionConnected() {
        ensurePresenceRenderer();
    }

    void EditorBridge::onSessionDisconnected() {
        if (auto* renderer = getPresenceRenderer()) {
            renderer->removeAllPeers();
        }
    }

    void EditorBridge::applyRemotePresence(const PresenceState& state) {
        if (auto* renderer = ensurePresenceRenderer()) {
            renderer->updatePeer(state);
        }
    }

    void EditorBridge::removeRemotePresence(PlayerId playerId) {
        if (auto* renderer = getPresenceRenderer()) {
            renderer->removePeer(playerId);
        }
    }

    void* EditorBridge::applyRemotePlacement(NetworkObjectId netId, uint16_t gdObjectId,
                                             float x, float y, float rotation,
                                             float scaleX, float scaleY,
                                             const std::string& saveString)
    {
        auto* lel = LevelEditorLayer::get();
        if (!lel) return nullptr;

        geode::log::info(
            "[EditorP2P] applyRemotePlacement: creating gdObjectId={} at ({},{})",
            gdObjectId, x, y
        );
        // Live placement must use the normal editor creation API. Rebuilding
        // arbitrary object save strings here has proven crash-prone inside GD's
        // object parser, especially for trigger-heavy objects.
        auto objectId = static_cast<int>(gdObjectId);
        if (objectId <= 0) {
            objectId = objectIdFromSaveString(saveString);
        }
        if (gdObjectId == 0 && !saveString.empty()) {
            x = floatFieldFromSaveString(saveString, 2).value_or(x);
            y = floatFieldFromSaveString(saveString, 3).value_or(y);
            rotation = floatFieldFromSaveString(saveString, 6).value_or(rotation);
            auto uniformScale = floatFieldFromSaveString(saveString, 32);
            if (uniformScale) {
                scaleX = *uniformScale;
                scaleY = *uniformScale;
            }
        }
        if (objectId <= 0) {
            geode::log::warn("[EditorP2P] applyRemotePlacement: missing object id");
            return nullptr;
        }
        auto* object = lel->createObject(objectId, {x, y}, true);
        if (!object) {
            geode::log::warn(
                "[EditorP2P] applyRemotePlacement: createObject({}) returned nullptr", gdObjectId
            );
            return nullptr;
        }
        geode::log::info(
            "[EditorP2P] applyRemotePlacement: created ok m_objectID={}", object->m_objectID
        );

        // Apply trigger-specific properties first; explicit wire params override
        // any transform keys that customObjectSetup may have written.
        applyCustomSetupFromSaveString(object, saveString);
        applyObjectGroupsFromSaveString(object, saveString);
        object->setPosition({x, y});
        object->setRotation(rotation);
        object->updateCustomScaleX(scaleX);
        object->updateCustomScaleY(scaleY);
        applyLayerFieldsFromSaveString(object, saveString);
        lel->updateObjectSection(object);
        ObjectRegistry::get().bindObject(netId, object);
        return object;
    }

    void EditorBridge::applyRemoteDeletion(NetworkObjectId netId) {
        auto* lel = LevelEditorLayer::get();
        if (!lel) return;

        auto* object = static_cast<GameObject*>(ObjectRegistry::get().findByNetworkId(netId));
        if (!object) return;

        lel->removeObject(object, true);
    }

    void EditorBridge::applyRemoteEdit(NetworkObjectId netId,
                                        float x, float y, float rotation,
                                        float scaleX, float scaleY,
                                        const std::string& saveString,
                                        bool useSaveStringTransform)
    {
        // Only let saveString override transform when the caller explicitly asks for
        // it (snapshot reconstruction).  For live CommitEdit the explicit params
        // come from object->getPosition() at commit time and are always correct.
        if (useSaveStringTransform && !saveString.empty()) {
            x        = floatFieldFromSaveString(saveString, 2).value_or(x);
            y        = floatFieldFromSaveString(saveString, 3).value_or(y);
            rotation = floatFieldFromSaveString(saveString, 6).value_or(rotation);
            auto uniformScale = floatFieldFromSaveString(saveString, 32);
            if (uniformScale) {
                scaleX = *uniformScale;
                scaleY = *uniformScale;
            }
        }

        auto* object = static_cast<GameObject*>(ObjectRegistry::get().findByNetworkId(netId));
        if (!object) return;

        // Re-apply trigger-specific properties so changes made via trigger editors
        // (groups, colors, duration, etc.) propagate; explicit wire params then
        // override any transform keys customObjectSetup may have written.
        applyCustomSetupFromSaveString(object, saveString);
        applyObjectGroupsFromSaveString(object, saveString);
        object->setPosition({x, y});
        object->setRotation(rotation);
        // updateCustomScaleX/Y updates m_customScaleX/Y and refreshes internal
        // derived state (hitbox, display), matching what the editor's scale UI does.
        object->updateCustomScaleX(scaleX);
        object->updateCustomScaleY(scaleY);
        // Refresh layer assignment on every edit so editor-layer / z-order changes
        // made remotely (e.g. via SetGroupIDLayer) propagate correctly.
        applyLayerFieldsFromSaveString(object, saveString);

        geode::log::info(
            "[EditorP2P] applyRemoteEdit id={} pos=({},{}) rot={} scale=({},{}) useSS={}",
            netId, x, y, rotation, scaleX, scaleY, useSaveStringTransform
        );

        if (auto* lel = LevelEditorLayer::get()) {
            lel->updateObjectSection(object);
        }
    }

} // namespace ep2p
