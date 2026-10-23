#pragma once
// Title-neutral DXGI view-format helpers (pure; numeric DXGI_FORMAT values so
// tests need no d3d headers). A TYPELESS texture has no default RTV/SRV: name
// the concrete format explicitly or it can never be bound (Halo 2's scene
// target is TYPELESS; CE's eye images can be TYPELESS too).
// Used by dll/core/vr.cpp and titles/h2/halo2_render_logic.h.
#include <cstdint>

namespace dxgi_view_format
{
    inline uint32_t Concrete(uint32_t format) noexcept
    {
        switch (format)
        {
        case 90:
            return 87; // B8G8R8A8_TYPELESS -> B8G8R8A8_UNORM
        case 92:
            return 88; // B8G8R8X8_TYPELESS -> B8G8R8X8_UNORM
        case 27:
            return 28; // R8G8B8A8_TYPELESS -> R8G8B8A8_UNORM
        case 10:
            return 11; // R16G16B16A16_TYPELESS -> _UNORM
        case 1:
            return 2; // R32G32B32A32_TYPELESS -> _FLOAT
        case 23:
            return 24; // R10G10B10A2_TYPELESS -> _UNORM
        default:
            return format;
        }
    }

    inline bool IsTypeless(uint32_t format) noexcept
    {
        return Concrete(format) != format;
    }
} // namespace dxgi_view_format
