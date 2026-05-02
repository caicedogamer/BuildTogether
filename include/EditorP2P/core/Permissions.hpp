#pragma once

#include <cstdint>

namespace ep2p {

    enum class Role : uint8_t {
        Viewer         = 0,  // presence only, no edits
        Builder        = 1,  // place/move/delete, can request save
        TrustedBuilder = 2,  // Builder + can trigger host-side save
        Owner          = 3,  // host; all permissions
    };

    struct PermissionFlags {
        bool canEdit        = false;
        bool canDelete      = false;
        bool canLock        = false;
        bool canRequestSave = false;
        bool canSave        = false;  // TrustedBuilder+
        bool canInvite      = false;  // reserved for >2 player expansion

        // Encode/decode to a single bitmask for wire transfer.
        uint32_t toBits() const;
        static PermissionFlags fromBits(uint32_t bits);
    };

    // Returns the default permission set for a given role.
    PermissionFlags permissionsForRole(Role role);

    // Human-readable role name for UI display.
    const char* roleName(Role role);

} // namespace ep2p
