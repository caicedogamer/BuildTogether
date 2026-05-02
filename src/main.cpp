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
    cfg.displayName = Mod::get()->getSettingValue<std::string>("display-name");
    cfg.hostPort    = static_cast<unsigned short>(
                          Mod::get()->getSettingValue<int64_t>("default-port"));

    // --- Install editor hooks (Geode $modify hooks self-register on static init,
    //     but installEditorHooks() logs confirmation and sets up any callbacks) ---
    installEditorHooks();

    log::info("[EditorP2P] v{} loaded. Display name: '{}', port: {}",
              Mod::get()->getVersion().toNonVString(),
              cfg.displayName,
              cfg.hostPort);
}
