#include <EditorP2P/core/Session.hpp>
#include <EditorP2P/config/BuildConfig.hpp>
#include <algorithm>

namespace ep2p {

    Session::Session(const SessionConfig& config)
        : m_config(config)
        , m_state(SessionState::Idle)
    {
        if (config.mode == SessionMode::Host) {
            m_myPlayerId = HOST_PLAYER_ID;
            m_myRole     = Role::Owner;
        }
    }

    void Session::addPeer(const PeerRecord& peer) {
        m_peers.push_back(peer);
    }

    void Session::removePeer(PlayerId id) {
        m_peers.erase(
            std::remove_if(m_peers.begin(), m_peers.end(),
                [id](const PeerRecord& p) { return p.id == id; }),
            m_peers.end()
        );
    }

    PeerRecord* Session::findPeer(PlayerId id) {
        for (auto& p : m_peers) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    const PeerRecord* Session::findPeer(PlayerId id) const {
        for (const auto& p : m_peers) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

} // namespace ep2p
