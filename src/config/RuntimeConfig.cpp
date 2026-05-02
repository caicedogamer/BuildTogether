#include <EditorP2P/config/RuntimeConfig.hpp>

namespace ep2p {

    RuntimeConfig& RuntimeConfig::get() {
        static RuntimeConfig instance;
        return instance;
    }

} // namespace ep2p
