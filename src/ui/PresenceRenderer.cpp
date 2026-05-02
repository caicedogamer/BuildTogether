#include <EditorP2P/ui/PresenceRenderer.hpp>
#include <EditorP2P/util/Clock.hpp>
#include <EditorP2P/config/BuildConfig.hpp>
#include <Geode/Geode.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

using namespace cocos2d;
using namespace ep2p;

namespace ep2p {

    PresenceRenderer* PresenceRenderer::create() {
        auto* obj = new PresenceRenderer();
        if (obj && obj->init()) {
            obj->autorelease();
            return obj;
        }
        CC_SAFE_DELETE(obj);
        return nullptr;
    }

    bool PresenceRenderer::init() {
        if (!CCLayer::init()) return false;
        // Ignore touches so clicks pass through to the editor below.
        this->setTouchEnabled(false);
        this->scheduleUpdate();
        return true;
    }

    void PresenceRenderer::update(float dt) {
        (void)dt;
        // Recompute screen positions from world coordinates and cull stale peers.
        for (auto& [id, node] : m_nodes) {
            auto stateIt = m_states.find(id);
            if (stateIt != m_states.end()) {
                updateNodePosition(node, stateIt->second.editorX, stateIt->second.editorY);
            }
        }
        cullStalePeers();
    }

    void PresenceRenderer::updatePeer(const PresenceState& state) {
        m_states[state.playerId] = state;

        if (m_nodes.find(state.playerId) == m_nodes.end()) {
            addPeerNode(state);
        } else {
            auto& node = m_nodes[state.playerId];
            if (node.label) {
                node.label->setString(state.displayName.c_str());
            }
        }
        // Position update happens in update() each frame.
    }

    void PresenceRenderer::removePeer(PlayerId playerId) {
        auto nodeIt = m_nodes.find(playerId);
        if (nodeIt != m_nodes.end()) {
            if (nodeIt->second.dot)   nodeIt->second.dot->removeFromParent();
            if (nodeIt->second.label) nodeIt->second.label->removeFromParent();
            m_nodes.erase(nodeIt);
        }
        m_states.erase(playerId);
    }

    void PresenceRenderer::removeAllPeers() {
        for (auto& [id, _] : m_nodes) {
            if (_.dot)   _.dot->removeFromParent();
            if (_.label) _.label->removeFromParent();
        }
        m_nodes.clear();
        m_states.clear();
    }

    void PresenceRenderer::addPeerNode(const PresenceState& state) {
        PeerNode node;
        node.playerId = state.playerId;

        uint8_t r = (state.colorRgb >> 16) & 0xFF;
        uint8_t g = (state.colorRgb >>  8) & 0xFF;
        uint8_t b = (state.colorRgb)       & 0xFF;

        // Small visible marker. This avoids relying on a sprite asset while the
        // prototype is still proving cursor sync.
        node.dot = CCLayerColor::create({r, g, b, 255}, 8.f, 8.f);
        if (node.dot) {
            node.dot->setAnchorPoint({0.5f, 0.5f});
            node.dot->ignoreAnchorPointForPosition(false);
            this->addChild(node.dot, 1);
        }

        // Name label above the dot.
        // TODO (Milestone 3): use the correct .fnt file available in GD
        node.label = CCLabelBMFont::create(state.displayName.c_str(), "bigFont.fnt");
        if (node.label) {
            node.label->setScale(0.35f);
            this->addChild(node.label, 2);
        }

        m_nodes[state.playerId] = node;
    }

    void PresenceRenderer::updateNodePosition(PeerNode& node, float worldX, float worldY) {
        CCPoint pos = {worldX, worldY};
        if (auto* lel = LevelEditorLayer::get()) {
            if (lel->m_objectLayer) {
                auto screen = lel->m_objectLayer->convertToWorldSpace({worldX, worldY});
                pos = this->convertToNodeSpace(screen);
            }
        }
        if (node.dot)   node.dot->setPosition(pos);
        if (node.label) node.label->setPosition({pos.x, pos.y + 20.f});
    }

    void PresenceRenderer::cullStalePeers() {
        TimestampMs now = Clock::now();
        std::vector<PlayerId> toRemove;
        for (const auto& [id, state] : m_states) {
            TimestampMs ageMs = now - state.lastUpdate;
            if (ageMs > static_cast<TimestampMs>(CURSOR_STALE_SECONDS) * 1000u) {
                toRemove.push_back(id);
            }
        }
        for (PlayerId id : toRemove) {
            removePeer(id);
        }
    }

} // namespace ep2p
