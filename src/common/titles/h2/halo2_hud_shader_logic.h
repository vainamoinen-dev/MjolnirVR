// Recognises Halo 2 HUD and crosshair pixel shaders by hash and maps HUD draws into the VR raster.
// Used by src/dll/core/d3d11_hook.cpp, tests/core_tests.cpp and tests/halo2_hud_element_tests.cpp.
// Pure logic: no Windows or D3D calls, no globals, no logging.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "halo2_render_logic.h"
#include "../../hud_element_logic.h"

// Runtime identities taken from the current Toggle HUD for Halo 2 v1.2
// 3DMigoto replacement set. That mod changes the actual shipping HUD by
// selecting these original pixel shaders, so these are render-path evidence,
// not inferred engine addresses. Its d3dx.ini explicitly uses 3DMigoto's
// traditional unseeded 64-bit FNV-1 shader hash.
namespace halo2_hud_shader
{
    enum class Role : uint8_t
    {
        Other = 0,
        GameplayHud,
        Crosshair,
    };

    inline constexpr uint64_t kCrosshairHash = 0x0a9b60d8f40268f6ULL;

    inline constexpr std::array<uint64_t, 15> kGameplayHudHashes{{
        0x417f1685ec6744aaULL, // shield
        0x50e9b7e9476762fcULL, // shield meter
        0x5138655c43bc2879ULL, // ammunition
        0x740e59c9d34791b5ULL, // weapon icons
        0xbdfb9aa0140f7f34ULL, // round icon
        0xc7f2063e1d1d87dfULL, // motion sensor
        0xf972418445d6e438ULL, // pickup notification
        0x11deb23283167559ULL, // multiplayer CUI / HUD
        0x1c889b6efc492695ULL,
        0x1fe71a633df057f9ULL,
        0x7681944ad6937bd6ULL,
        0xc975aaa399a4ae72ULL,
        0xf0540984f5878178ULL,
        0xf665e50c823d4832ULL,
        0x2c96ddc4df718dd8ULL,
    }};

    // Per-element HUD (hud_element::Element) for the labelled gameplay shaders.
    // Unlabelled multiplayer/CUI hashes return -1 and keep the whole-HUD layout.
    // Halo 2 has no anchor information at this draw boundary, so only the manual
    // x/y offsets apply (the pull is a Reach/Halo 3 feature).
    inline constexpr int ElementForHash(uint64_t hash) noexcept
    {
        switch (hash)
        {
        case 0x417f1685ec6744aaULL: // shield
        case 0x50e9b7e9476762fcULL: // shield meter
            return hud_element::Shield;
        case 0x5138655c43bc2879ULL: // ammunition
        case 0x740e59c9d34791b5ULL: // weapon icons
        case 0xbdfb9aa0140f7f34ULL: // round icon
            return hud_element::Weapon;
        case 0xc7f2063e1d1d87dfULL: // motion sensor
            return hud_element::Motion;
        case 0xf972418445d6e438ULL: // pickup notification
            return hud_element::Messages;
        default:
            return -1;
        }
    }

    // Manual element offset in viewport pixels. x/y are HUD pixels at 720 lines
    // (+x right, +y up), scaled to this viewport's height so a setting means the
    // same on every render size. False: no offset.
    inline bool ElementViewportOffset(
        float x, float y, float viewportHeight, float limit, float& dx, float& dy) noexcept
    {
        dx = dy = 0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(viewportHeight) || viewportHeight <= 0 ||
            !(limit > 0))
            return false;
        const auto clamp = [limit](float v) { return v < -limit ? -limit : v > limit ? limit : v; };
        const float scale = viewportHeight / 720.0f;
        dx = clamp(x) * scale;
        dy = -clamp(y) * scale;
        return dx != 0 || dy != 0;
    }

    inline constexpr Role Classify(uint64_t hash) noexcept
    {
        if (hash == kCrosshairHash)
            return Role::Crosshair;
        for (const uint64_t gameplayHash : kGameplayHudHashes)
            if (hash == gameplayHash)
                return Role::GameplayHud;
        return Role::Other;
    }

    inline uint64_t MigotoFnv1(const void* data, size_t size) noexcept
    {
        if (!data && size)
            return 0;
        constexpr uint64_t prime = 0x100000001b3ULL;
        uint64_t hash = 0;
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t index = 0; index < size; ++index)
        {
            hash *= prime;
            hash ^= bytes[index];
        }
        return hash;
    }

    struct RasterAffine
    {
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };

    inline bool BuildRasterAffine(
        const Halo2CameraRectangle& source, const Halo2CameraRectangle& layout, RasterAffine& output) noexcept
    {
        const float sourceWidth = static_cast<float>(static_cast<int32_t>(source.x1) - source.x0);
        const float sourceHeight = static_cast<float>(static_cast<int32_t>(source.y1) - source.y0);
        const float layoutWidth = static_cast<float>(static_cast<int32_t>(layout.x1) - layout.x0);
        const float layoutHeight = static_cast<float>(static_cast<int32_t>(layout.y1) - layout.y0);
        if (sourceWidth < 8.0f || sourceHeight < 8.0f || layoutWidth < 8.0f || layoutHeight < 8.0f)
        {
            return false;
        }

        RasterAffine result{};
        result.scaleX = layoutWidth / sourceWidth;
        result.scaleY = layoutHeight / sourceHeight;
        result.offsetX = static_cast<float>(layout.x0) - static_cast<float>(source.x0) * result.scaleX;
        result.offsetY = static_cast<float>(layout.y0) - static_cast<float>(source.y0) * result.scaleY;
        if (!std::isfinite(result.scaleX) || !std::isfinite(result.scaleY) || !std::isfinite(result.offsetX) ||
            !std::isfinite(result.offsetY) || result.scaleX <= 0.0f || result.scaleY <= 0.0f)
        {
            return false;
        }
        output = result;
        return true;
    }

    inline float MapX(const RasterAffine& transform, float x) noexcept
    {
        return transform.offsetX + x * transform.scaleX;
    }

    inline float MapY(const RasterAffine& transform, float y) noexcept
    {
        return transform.offsetY + y * transform.scaleY;
    }
}
