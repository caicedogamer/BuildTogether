#pragma once

#include <EditorP2P/core/Types.hpp>
#include <EditorP2P/core/Permissions.hpp>
#include <EditorP2P/net/Endpoint.hpp>
#include <string>
#include <vector>

namespace ep2p {

    // All configuration needed to start or join a session.
    struct SessionConfig {
        SessionMode mode = SessionMode::None;

        // Common
        std::string displayName;
        std::string sessionKey;   // "XXXX-XXXX"

        // Host only
        std::string  roomName;
        unsigned short hostPort = 43720;

        // Peer only
        Endpoint remoteEndpoint;  // host IP + port to connect to
    };

    // Per-peer state tracked by the host.
    struct PeerRecord {
        PlayerId    id;
        std::string displayName;
        Role        role = Role::Builder;
        PermissionFlags permissions;
        bool        connected = false;
    };

    // Owns the active session lifecycle. Operates without GD/Geode dependencies.
    // Networking calls are dispatched to HostSession / PeerSession inside SessionManager.
    class Session {
    public:
        explicit Session(const SessionConfig& config);

        // State accessors
        SessionMode  mode()  const { return m_config.mode; }
        SessionState state() const { return m_state; }
        bool         isHost() const { return m_config.mode == SessionMode::Host; }
        bool         isPeer() const { return m_config.mode == SessionMode::Peer; }
        bool         isConnected() const { return m_state == SessionState::Connected; }

        const SessionConfig& config() const { return m_config; }
        PlayerId             myPlayerId() const { return m_myPlayerId; }
        Role                 myRole()  const { return m_myRole; }

        // Peer record management (host only)
        void              addPeer(const PeerRecord& peer);
        void              removePeer(PlayerId id);
        PeerRecord*       findPeer(PlayerId id);
        const PeerRecord* findPeer(PlayerId id) const;
        const std::vector<PeerRecord>& peers() const { return m_peers; }

        // State transitions — called by SessionManager, not directly by UI.
        void setState(SessionState s) { m_state = s; }
        void setMyPlayerId(PlayerId id) { m_myPlayerId = id; }
        void setMyRole(Role role) { m_myRole = role; }

    private:
        SessionConfig m_config;
        SessionState  m_state = SessionState::Idle;
        PlayerId      m_myPlayerId = 0;
        Role          m_myRole = Role::Owner;

        // Host only: connected peers
        std::vector<PeerRecord> m_peers;  // V1: at most 1 entry
    };

} // namespace ep2p
