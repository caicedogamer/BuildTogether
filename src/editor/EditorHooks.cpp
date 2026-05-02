#include <EditorP2P/editor/EditorHooks.hpp>
#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/editor/EditorBridge.hpp>
#include <EditorP2P/editor/ObjectRegistry.hpp>
#include <EditorP2P/config/BuildConfig.hpp>
#include <EditorP2P/ui/HostLayer.hpp>
#include <EditorP2P/ui/JoinLayer.hpp>
#include <EditorP2P/ui/PresenceRenderer.hpp>
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/SetGroupIDLayer.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace geode::prelude;
using namespace ep2p;

namespace {
    constexpr int EP2P_PRESENCE_RENDERER_TAG = 4372001;

    void showEditorP2PMenu() {
        geode::createQuickPopup(
            "EditorP2P",
            "Choose a collaboration action.",
            "Host",
            "Join",
            [](FLAlertLayer*, bool joinPressed) {
                if (joinPressed) {
                    if (auto* layer = JoinLayer::create()) {
                        layer->show();
                    }
                } else {
                    if (auto* layer = HostLayer::create()) {
                        layer->show();
                    }
                }
            }
        );
    }

    std::vector<GameObject*> collectObjects(cocos2d::CCArray* objects) {
        std::vector<GameObject*> result;
        if (!objects) return result;

        result.reserve(objects->count());
        for (auto* object : CCArrayExt<GameObject*>(objects)) {
            if (object) result.push_back(object);
        }
        return result;
    }

    ObjectRegistry::ObjectFingerprint buildObjectFingerprint(GameObject* object) {
        ObjectRegistry::ObjectFingerprint fingerprint;
        if (!object) return fingerprint;

        auto pos = object->getPosition();
        fingerprint.objectId = object->m_objectID;
        fingerprint.x = pos.x;
        fingerprint.y = pos.y;
        fingerprint.rotation = object->getRotation();
        fingerprint.scaleX = object->m_scaleX;
        fingerprint.scaleY = object->m_scaleY;
        if (auto* lel = LevelEditorLayer::get()) {
            fingerprint.saveString = object->getSaveString(lel);
        }
        return fingerprint;
    }

    void refreshObjectFingerprint(GameObject* object, NetworkObjectId netId) {
        if (!object || netId == INVALID_OBJECT_ID) return;
        ObjectRegistry::get().updateFingerprint(netId, buildObjectFingerprint(object));
    }

    NetworkObjectId recoverNetworkId(GameObject* object, const char* context, bool rebind) {
        if (!object) return INVALID_OBJECT_ID;

        auto netId = ObjectRegistry::get().findNetworkId(object);
        if (netId != INVALID_OBJECT_ID) {
            refreshObjectFingerprint(object, netId);
            return netId;
        }

        auto fingerprint = buildObjectFingerprint(object);
        ObjectRegistry::MatchStats stats;
        netId = ObjectRegistry::get().findLikelyNetworkId(fingerprint, &stats);
        if (netId == INVALID_OBJECT_ID) {
            geode::log::warn(
                "[EP2P hook] {} unresolved object={} objectId={} pos=({},{}) registryCount={} objectIdMatches={} saveMatches={} transformMatches={}",
                context,
                static_cast<void*>(object),
                fingerprint.objectId,
                fingerprint.x,
                fingerprint.y,
                stats.registryCount,
                stats.objectIdMatches,
                stats.exactSaveStringMatches,
                stats.transformMatches
            );
            return INVALID_OBJECT_ID;
        }

        geode::log::warn(
            "[EP2P hook] {} recovered stale registry pointer object={} objectId={} pos=({},{}) netId={} saveMatches={} transformMatches={}",
            context,
            static_cast<void*>(object),
            fingerprint.objectId,
            fingerprint.x,
            fingerprint.y,
            netId,
            stats.exactSaveStringMatches,
            stats.transformMatches
        );
        if (rebind) {
            ObjectRegistry::get().bindObject(netId, object);
            ObjectRegistry::get().updateFingerprint(netId, std::move(fingerprint));
        }
        return netId;
    }

    struct TrackedObject {
        GameObject* object = nullptr;
        NetworkObjectId netId = INVALID_OBJECT_ID;
        int objectId = 0;
    };

    std::vector<TrackedObject> collectTrackedObjects(cocos2d::CCArray* objects) {
        std::vector<TrackedObject> result;
        if (!objects) return result;

        result.reserve(objects->count());
        for (auto* object : CCArrayExt<GameObject*>(objects)) {
            TrackedObject tracked;
            tracked.object = object;
            tracked.netId = ObjectRegistry::get().findNetworkId(object);
            tracked.objectId = object ? object->m_objectID : 0;
            result.push_back(tracked);
        }
        return result;
    }

    void repairTransformedObjects(const std::vector<TrackedObject>& before,
                                  const std::vector<GameObject*>& after,
                                  const char* context) {
        if (before.size() != after.size()) {
            geode::log::debug(
                "[EP2P hook] {} rebind skipped: beforeCount={} afterCount={}",
                context, before.size(), after.size()
            );
            return;
        }

        for (size_t i = 0; i < after.size(); ++i) {
            auto* current = after[i];
            if (!current) continue;

            auto currentId = ObjectRegistry::get().findNetworkId(current);
            if (currentId != INVALID_OBJECT_ID) {
                refreshObjectFingerprint(current, currentId);
                continue;
            }

            const auto& prior = before[i];
            if (prior.netId == INVALID_OBJECT_ID || prior.objectId != current->m_objectID) {
                continue;
            }

            ObjectRegistry::get().bindObject(prior.netId, current);
            refreshObjectFingerprint(current, prior.netId);
            geode::log::warn(
                "[EP2P hook] {} rebound transformed object oldPtr={} newPtr={} objectId={} netId={}",
                context,
                static_cast<void*>(prior.object),
                static_cast<void*>(current),
                current->m_objectID,
                prior.netId
            );
        }
    }

    std::vector<GameObject*> collectCurrentSelection(EditorUI* ui) {
        std::vector<GameObject*> result;
        if (!ui) return result;

        std::unordered_set<GameObject*> seen;
        if (auto* arr = ui->m_selectedObjects) {
            for (auto* obj : CCArrayExt<GameObject*>(arr)) {
                if (obj && seen.insert(obj).second) result.push_back(obj);
            }
        }
        if (auto* obj = ui->m_selectedObject) {
            if (seen.insert(obj).second) result.push_back(obj);
        }
        return result;
    }

    void commitEditedObjects(const std::vector<GameObject*>& objects) {
        for (auto* object : objects) {
            auto netId = recoverNetworkId(object, "commitEditedObjects", true);
            if (netId != INVALID_OBJECT_ID) {
                SessionManager::get().onLocalObjectEdited(netId);
            }
        }
    }

    void commitEditedObjects(cocos2d::CCArray* objects) {
        commitEditedObjects(collectObjects(objects));
    }

    void commitCurrentSelectionEdit(EditorUI* ui) {
        commitEditedObjects(collectCurrentSelection(ui));
    }

    std::vector<GameObject*> collectGroupLayerTargets(SetGroupIDLayer* layer) {
        std::vector<GameObject*> objects;
        if (!layer) return objects;

        std::unordered_set<GameObject*> seen;
        if (auto* arr = layer->m_targetObjects) {
            objects.reserve(arr->count());
            for (auto* obj : CCArrayExt<GameObject*>(arr)) {
                if (obj && seen.insert(obj).second) objects.push_back(obj);
            }
        }
        if (auto* obj = layer->m_targetObject) {
            if (seen.insert(obj).second) objects.push_back(obj);
        }
        return objects;
    }

    void commitGroupLayerTargets(SetGroupIDLayer* layer, const char* context) {
        auto objects = collectGroupLayerTargets(layer);

        geode::log::debug(
            "[EP2P hook] {} committing group layer targets count={}",
            context,
            objects.size()
        );
        commitEditedObjects(objects);
    }

    // Collect unique netIds from m_selectedObjects + m_selectedObject and
    // send CommitEdit+Unlock for each. Call this BEFORE the base clears selection.
    void releaseCurrentSelection(EditorUI* ui) {
        std::unordered_set<uint32_t> ids;
        if (auto* arr = ui->m_selectedObjects) {
            for (auto* obj : CCArrayExt<GameObject*>(arr)) {
                auto netId = recoverNetworkId(obj, "releaseCurrentSelection", true);
                if (netId != INVALID_OBJECT_ID) ids.insert(netId);
            }
        }
        if (auto* obj = ui->m_selectedObject) {
            auto netId = recoverNetworkId(obj, "releaseCurrentSelection", true);
            if (netId != INVALID_OBJECT_ID) ids.insert(netId);
        }
        for (auto netId : ids) {
            SessionManager::get().onLocalObjectDeselected(netId);
        }
    }

    // Request locks for every registered object in an array.
    void acquireSelectionLocks(cocos2d::CCArray* objects) {
        if (!objects) return;
        for (auto* obj : CCArrayExt<GameObject*>(objects)) {
            auto netId = recoverNetworkId(obj, "acquireSelectionLocks", true);
            if (netId != INVALID_OBJECT_ID) {
                SessionManager::get().onLocalObjectSelected(netId);
            }
        }
    }

    void primeKnownObjects(cocos2d::CCArray* objects) {
        if (!objects) return;
        for (auto* obj : CCArrayExt<GameObject*>(objects)) {
            SessionManager::get().markLocalObjectKnown(obj);
        }
    }

    void discoverUnsyncedObjects(cocos2d::CCArray* objects) {
        if (!objects) return;
        for (auto* obj : CCArrayExt<GameObject*>(objects)) {
            if (!obj) continue;
            auto pos = obj->getPosition();
            SessionManager::get().onLocalObjectDiscovered(
                obj,
                obj->m_objectID,
                pos.x,
                pos.y
            );
        }
    }
}

// ============================================================================
// LevelEditorLayer hook
// ============================================================================

class $modify(EP2P_LevelEditorLayerHook, LevelEditorLayer) {

    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;

        if (auto* renderer = PresenceRenderer::create()) {
            renderer->setTag(EP2P_PRESENCE_RENDERER_TAG);
            this->addChild(renderer, 200);
        }

        // TODO (Milestone 7): register existing objects once network IDs exist.
        this->schedule(schedule_selector(EP2P_LevelEditorLayerHook::smTick));

        return true;
    }

    void smTick(float dt) {
        if (this->m_objectLayer) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto screenCenter = CCPoint{winSize.width / 2.f, winSize.height / 2.f};
            auto editorCenter = this->m_objectLayer->convertToNodeSpace(screenCenter);
            SessionManager::get().onCursorMoved(editorCenter.x, editorCenter.y);
        }
        SessionManager::get().tick(dt);

        auto& manager = SessionManager::get();
        if (!manager.isConnected()) {
            m_fields->m_knownObjectsPrimed = false;
            m_fields->m_objectScanAccum = 0.f;
            return;
        }

        if (!m_fields->m_knownObjectsPrimed) {
            primeKnownObjects(this->m_objects);
            m_fields->m_knownObjectsPrimed = true;
            return;
        }

        m_fields->m_objectScanAccum += dt;
        if (m_fields->m_objectScanAccum >= 0.15f) {
            m_fields->m_objectScanAccum = 0.f;
            discoverUnsyncedObjects(this->m_objects);
        }
    }

    // Track nesting depth: GD sometimes calls createObject internally during
    // another createObject (e.g. composite objects). We only sync the outermost call.
    struct Fields {
        int m_createDepth = 0;
        bool m_knownObjectsPrimed = false;
        float m_objectScanAccum = 0.f;
    };

    void removeObject(GameObject* object, bool noUndo) {
        auto fingerprint = buildObjectFingerprint(object);
        auto netId = recoverNetworkId(object, "removeObject", true);
        geode::log::info(
            "[EP2P hook] removeObject object={} objectId={} pos=({},{}) registryNetId={} registryCount={}",
            static_cast<void*>(object),
            fingerprint.objectId,
            fingerprint.x,
            fingerprint.y,
            netId,
            ObjectRegistry::get().count()
        );
        LevelEditorLayer::removeObject(object, noUndo);

        if (netId != INVALID_OBJECT_ID) {
            SessionManager::get().onLocalObjectDeleted(netId);
        } else {
            geode::log::warn(
                "[EP2P hook] delete skipped: no network id for object={} objectId={} pos=({},{})",
                static_cast<void*>(object),
                fingerprint.objectId,
                fingerprint.x,
                fingerprint.y
            );
        }
    }

    GameObject* createObject(int key, cocos2d::CCPoint position, bool noUndo) {
        ++m_fields->m_createDepth;
        auto* object = LevelEditorLayer::createObject(key, position, noUndo);
        bool isTopLevel = (m_fields->m_createDepth-- == 1);

        if (object) {
            geode::log::debug(
                "[EP2P hook] createObject key={} m_objectID={} "
                "requestedPos=({},{}) actualPos=({},{}) topLevel={}",
                key, object->m_objectID,
                position.x, position.y,
                object->getPositionX(), object->getPositionY(),
                isTopLevel
            );
            if (isTopLevel) {
                // Pass key and the requested position directly — both are more
                // reliable than reading back from the object post-creation.
                SessionManager::get().onLocalObjectPlaced(
                    object, key, position.x, position.y
                );
                refreshObjectFingerprint(object, ObjectRegistry::get().findNetworkId(object));
            }
        }
        return object;
    }
};

// ============================================================================
// EditorUI hook
// ============================================================================

class $modify(EP2P_EditorUIHook, EditorUI) {

    bool init(LevelEditorLayer* lel) {
        return EditorUI::init(lel);
    }

    void selectObject(GameObject* object, bool ignoreFilter) {
        auto* previous = this->m_selectedObject;

        EditorUI::selectObject(object, ignoreFilter);

        // If previous primary is no longer in m_selectedObjects after the base
        // call, GD removed it from the selection — release its lock.
        // (If the user ctrl+clicked to ADD, previous is still in the array.)
        if (previous && previous != object) {
            bool stillSelected = this->m_selectedObjects &&
                                 this->m_selectedObjects->containsObject(previous);
            if (!stillSelected) {
                auto prevId = recoverNetworkId(previous, "selectObject.previous", true);
                if (prevId != INVALID_OBJECT_ID) {
                    SessionManager::get().onLocalObjectDeselected(prevId);
                }
            }
        }

        auto netId = recoverNetworkId(object, "selectObject", true);
        if (netId != INVALID_OBJECT_ID) {
            SessionManager::get().onLocalObjectSelected(netId);
        }
    }

    // Called by box-select and explicit multi-select.
    void selectObjects(cocos2d::CCArray* objects, bool ignoreFilter) {
        // Release previous selection before GD replaces it.
        releaseCurrentSelection(this);
        EditorUI::selectObjects(objects, ignoreFilter);
        // Lock all objects in the new selection.
        acquireSelectionLocks(objects);
    }

    // selectObjectsInRect has an inline body and cannot be hooked.
    // Rect-select ends up calling selectObjects internally, so the
    // selectObjects hook covers the lock-acquisition for box select.

    void deselectAll() {
        // Commit+unlock every selected object before GD clears the selection.
        releaseCurrentSelection(this);
        EditorUI::deselectAll();
    }

    // ---- Scale-control delegate callbacks -----------------------------------
    // scaleChangeEnded/transformChangeEnded have inline bodies and cannot be
    // hooked; commit on each value-change callback instead.

    void scaleXChanged(float scaleX, bool lock) {
        EditorUI::scaleXChanged(scaleX, lock);
        auto* obj = this->m_selectedObject;
        auto netId = ObjectRegistry::get().findNetworkId(obj);
        geode::log::debug(
            "[EP2P hook] scaleXChanged val={} lock={} obj={} netId={}",
            scaleX, lock, static_cast<void*>(obj), netId
        );
        commitCurrentSelectionEdit(this);
    }

    void scaleYChanged(float scaleY, bool lock) {
        EditorUI::scaleYChanged(scaleY, lock);
        auto* obj = this->m_selectedObject;
        auto netId = ObjectRegistry::get().findNetworkId(obj);
        geode::log::debug(
            "[EP2P hook] scaleYChanged val={} lock={} obj={} netId={}",
            scaleY, lock, static_cast<void*>(obj), netId
        );
        commitCurrentSelectionEdit(this);
    }

    void scaleXYChanged(float scaleX, float scaleY, bool lock) {
        EditorUI::scaleXYChanged(scaleX, scaleY, lock);
        auto* obj = this->m_selectedObject;
        auto netId = ObjectRegistry::get().findNetworkId(obj);
        geode::log::debug(
            "[EP2P hook] scaleXYChanged x={} y={} lock={} obj={} netId={}",
            scaleX, scaleY, lock, static_cast<void*>(obj), netId
        );
        commitCurrentSelectionEdit(this);
    }

    // ---- Transform-control delegate callbacks --------------------------------

    void transformScaleXChanged(float scaleX) {
        EditorUI::transformScaleXChanged(scaleX);
        auto* obj = this->m_selectedObject;
        auto netId = ObjectRegistry::get().findNetworkId(obj);
        geode::log::debug(
            "[EP2P hook] transformScaleXChanged val={} obj={} netId={}",
            scaleX, static_cast<void*>(obj), netId
        );
        commitCurrentSelectionEdit(this);
    }

    void transformScaleYChanged(float scaleY) {
        EditorUI::transformScaleYChanged(scaleY);
        auto* obj = this->m_selectedObject;
        auto netId = ObjectRegistry::get().findNetworkId(obj);
        geode::log::debug(
            "[EP2P hook] transformScaleYChanged val={} obj={} netId={}",
            scaleY, static_cast<void*>(obj), netId
        );
        commitCurrentSelectionEdit(this);
    }

    void transformScaleXYChanged(float scaleX, float scaleY) {
        EditorUI::transformScaleXYChanged(scaleX, scaleY);
        auto* obj = this->m_selectedObject;
        auto netId = ObjectRegistry::get().findNetworkId(obj);
        geode::log::debug(
            "[EP2P hook] transformScaleXYChanged x={} y={} obj={} netId={}",
            scaleX, scaleY, static_cast<void*>(obj), netId
        );
        commitCurrentSelectionEdit(this);
    }

    // ---- Low-level object array mutations ------------------------------------

    void scaleObjects(cocos2d::CCArray* objects, float scaleX, float scaleY,
                      cocos2d::CCPoint pivotPoint, ObjectScaleType type, bool lockMove) {
        auto before = collectTrackedObjects(objects);
        EditorUI::scaleObjects(objects, scaleX, scaleY, pivotPoint, type, lockMove);
        auto touched = collectObjects(objects);
        repairTransformedObjects(before, touched, "scaleObjects");
        geode::log::debug(
            "[EP2P hook] scaleObjects count={} scaleX={} scaleY={}",
            objects ? static_cast<int>(objects->count()) : -1, scaleX, scaleY
        );
        // Deselect (onLocalObjectDeselected) sends the final commit+unlock when
        // the user releases. Committing here too for multi-select safety.
        commitEditedObjects(touched);
    }

    void transformObjects(cocos2d::CCArray* objects, cocos2d::CCPoint anchor,
                          float scaleX, float scaleY, float rotateX, float rotateY,
                          float warpX, float warpY) {
        auto before = collectTrackedObjects(objects);
        EditorUI::transformObjects(objects, anchor, scaleX, scaleY, rotateX, rotateY, warpX, warpY);
        auto touched = collectObjects(objects);
        repairTransformedObjects(before, touched, "transformObjects");
        geode::log::debug(
            "[EP2P hook] transformObjects count={} scaleX={} scaleY={} rotX={} rotY={} warpX={} warpY={}",
            objects ? static_cast<int>(objects->count()) : -1,
            scaleX, scaleY, rotateX, rotateY, warpX, warpY
        );
        commitEditedObjects(touched);
    }

    void rotateObjects(cocos2d::CCArray* objects, float rotation,
                       cocos2d::CCPoint pivotPoint) {
        auto before = collectTrackedObjects(objects);
        EditorUI::rotateObjects(objects, rotation, pivotPoint);
        auto touched = collectObjects(objects);
        repairTransformedObjects(before, touched, "rotateObjects");
        geode::log::debug(
            "[EP2P hook] rotateObjects count={} rotation={}",
            objects ? static_cast<int>(objects->count()) : -1, rotation
        );
        commitEditedObjects(touched);
    }

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        if (key == cocos2d::KEY_F9) {
            showEditorP2PMenu();
            return;
        }

        EditorUI::keyDown(key, timestamp);
    }
};

// ============================================================================
// SetGroupIDLayer hook
// ============================================================================

class $modify(EP2P_SetGroupIDLayerHook, SetGroupIDLayer) {

    void onAddGroup(cocos2d::CCObject* sender) {
        SetGroupIDLayer::onAddGroup(sender);
        commitGroupLayerTargets(this, "SetGroupIDLayer::onAddGroup");
    }

    void onRemoveFromGroup(cocos2d::CCObject* sender) {
        SetGroupIDLayer::onRemoveFromGroup(sender);
        commitGroupLayerTargets(this, "SetGroupIDLayer::onRemoveFromGroup");
    }

    void callRemoveFromGroup(float dt) {
        SetGroupIDLayer::callRemoveFromGroup(dt);
        commitGroupLayerTargets(this, "SetGroupIDLayer::callRemoveFromGroup");
    }

    void onPaste(cocos2d::CCObject* sender) {
        SetGroupIDLayer::onPaste(sender);
        commitGroupLayerTargets(this, "SetGroupIDLayer::onPaste");
    }

    void onClose(cocos2d::CCObject* sender) {
        auto objects = collectGroupLayerTargets(this);
        SetGroupIDLayer::onClose(sender);
        commitEditedObjects(objects);
    }

    void keyBackClicked() {
        auto objects = collectGroupLayerTargets(this);
        SetGroupIDLayer::keyBackClicked();
        commitEditedObjects(objects);
    }
};

// ============================================================================
// installEditorHooks
// ============================================================================

namespace ep2p {

    void installEditorHooks() {
        geode::log::info("[EditorP2P] Editor hooks installed.");
    }

} // namespace ep2p
