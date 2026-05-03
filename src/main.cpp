#ifdef EP2P_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <Geode/Geode.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GameManager.hpp>

#include <EditorP2P/core/SessionManager.hpp>
#include <EditorP2P/config/RuntimeConfig.hpp>
#include <EditorP2P/editor/EditorHooks.hpp>
#include <EditorP2P/util/Clock.hpp>

using namespace geode::prelude;
using namespace ep2p;

namespace {
#ifdef EP2P_WINDOWS
    bool g_winsockStarted = false;
#endif

    struct EditorP2PCleanup {
        ~EditorP2PCleanup() {
            SessionManager::get().disconnect("Mod unloaded");
#ifdef EP2P_WINDOWS
            if (g_winsockStarted) {
                WSACleanup();
                g_winsockStarted = false;
            }
#endif
        }
    };

    EditorP2PCleanup g_cleanup;

    bool isAllowedNameChar(char ch) {
        return (ch >= 'A' && ch <= 'Z') ||
               (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') ||
               ch == '_' || ch == '-';
    }

    std::string sanitizeDisplayName(std::string name) {
        std::string out;
        out.reserve(name.size());
        for (char ch : name) {
            if (isAllowedNameChar(ch)) out.push_back(ch);
            if (out.size() >= 31) break;
        }
        return out;
    }

    std::string resolveDisplayName() {
        auto configured = Mod::get()->getSettingValue<std::string>("display-name");
        auto sanitized = sanitizeDisplayName(configured);
        if (!sanitized.empty() && sanitized != "Player") {
            return sanitized;
        }

        if (auto* account = GJAccountManager::sharedState()) {
            sanitized = sanitizeDisplayName(account->m_username.c_str());
            if (!sanitized.empty()) return sanitized;
        }

        if (auto* gameManager = GameManager::sharedState()) {
            sanitized = sanitizeDisplayName(gameManager->m_playerName.c_str());
            if (!sanitized.empty()) return sanitized;
        }

        return "User";
    }
}

$on_mod(Loaded) {
    // --- Winsock initialization ---
    // Winsock must be initialized before any socket calls.
    // WSACleanup is called in the $on_mod(Unloaded) block below.
#ifdef EP2P_WINDOWS
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        log::error("[EditorP2P] WSAStartup failed: {}", wsaResult);
        return;
    }
    g_winsockStarted = true;
    log::info("[EditorP2P] Winsock initialized (v{}.{})",
              LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
#endif

    // --- Load runtime config from Geode settings ---
    auto& cfg = RuntimeConfig::get();
    cfg.displayName = resolveDisplayName();
    cfg.hostPort    = static_cast<unsigned short>(
                          Mod::get()->getSettingValue<int64_t>("default-port"));
    cfg.relayHost   = Mod::get()->getSettingValue<std::string>("relay-host");

    // --- Install editor hooks (Geode $modify hooks self-register on static init,
    //     but installEditorHooks() logs confirmation and sets up any callbacks) ---
    installEditorHooks();

    log::info("[EditorP2P] v{} loaded. Display name: '{}', port: {}, relay: {}",
              Mod::get()->getVersion().toNonVString(),
              cfg.displayName,
              cfg.hostPort,
              cfg.relayHost);
}
