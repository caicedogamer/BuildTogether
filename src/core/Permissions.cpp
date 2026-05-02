#include <EditorP2P/core/Permissions.hpp>

namespace ep2p {

    PermissionFlags permissionsForRole(Role role) {
        PermissionFlags f;
        switch (role) {
            case Role::Viewer:
                // No edit capabilities — presence only.
                break;

            case Role::Builder:
                f.canEdit        = true;
                f.canDelete      = true;
                f.canLock        = true;
                f.canRequestSave = true;
                break;

            case Role::TrustedBuilder:
                f.canEdit        = true;
                f.canDelete      = true;
                f.canLock        = true;
                f.canRequestSave = true;
                f.canSave        = true;
                break;

            case Role::Owner:
                f.canEdit        = true;
                f.canDelete      = true;
                f.canLock        = true;
                f.canRequestSave = true;
                f.canSave        = true;
                f.canInvite      = true;
                break;
        }
        return f;
    }

    uint32_t PermissionFlags::toBits() const {
        uint32_t bits = 0;
        if (canEdit)        bits |= (1u << 0);
        if (canDelete)      bits |= (1u << 1);
        if (canLock)        bits |= (1u << 2);
        if (canRequestSave) bits |= (1u << 3);
        if (canSave)        bits |= (1u << 4);
        if (canInvite)      bits |= (1u << 5);
        return bits;
    }

    PermissionFlags PermissionFlags::fromBits(uint32_t bits) {
        PermissionFlags f;
        f.canEdit        = (bits & (1u << 0)) != 0;
        f.canDelete      = (bits & (1u << 1)) != 0;
        f.canLock        = (bits & (1u << 2)) != 0;
        f.canRequestSave = (bits & (1u << 3)) != 0;
        f.canSave        = (bits & (1u << 4)) != 0;
        f.canInvite      = (bits & (1u << 5)) != 0;
        return f;
    }

    const char* roleName(Role role) {
        switch (role) {
            case Role::Viewer:         return "Viewer";
            case Role::Builder:        return "Builder";
            case Role::TrustedBuilder: return "Trusted Builder";
            case Role::Owner:          return "Owner";
        }
        return "Unknown";
    }

} // namespace ep2p
