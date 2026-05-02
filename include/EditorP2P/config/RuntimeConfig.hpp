#pragma once

#include <string>

namespace ep2p {

    // Runtime configuration loaded from Geode mod settings or changed at runtime.
    // This is a plain data struct — no Geode headers here so tests can use it.
    struct RuntimeConfig {
        std::string displayName   = "User";
        unsigned short hostPort   = 43720;

        // Singleton accessor. Populated from Geode settings in main.cpp.
        static RuntimeConfig& get();
    };

} // namespace ep2p
