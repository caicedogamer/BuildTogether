#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ep2p {

    // Platform-independent transport interface.
    // Implementations (WinSocketTransport) live in net/ and are isolated from the rest of the code.
    // Future: swap in a relay-backed transport or QUIC without touching higher layers.
    class ITransport {
    public:
        virtual ~ITransport() = default;

        // Open / close the underlying socket.
        virtual bool start() = 0;
        virtual void stop()  = 0;

        // Send raw bytes. Returns false on error (caller should disconnect).
        virtual bool send(const uint8_t* data, size_t len) = 0;

        // Non-blocking poll. Fills outData if a complete message is available.
        // Returns true if data was received this call.
        virtual bool poll(std::vector<uint8_t>& outData) = 0;

        virtual bool isConnected() const = 0;

        // Human-readable status for logging.
        virtual std::string statusString() const = 0;
    };

} // namespace ep2p
