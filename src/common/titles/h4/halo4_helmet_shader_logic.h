// Halo 4 helmet visor frame: finds its pixel shader by 3DMigoto hash and sizes or hides the frame.
// Used by src/dll/core/d3d11_hook.cpp and menu.cpp; tested in tests/halo4_visor_tests.cpp and core_tests.cpp.
// Pure logic: no Windows or D3D calls, no globals, no logging.
#pragma once

#include <cstddef>
#include <cstdint>

// Halo 4's in-helmet visor/framing draw is identified by this exact 3DMigoto
// FNV-1 pixel-shader hash, taken from a known-good headset build. A runtime log
// from that build proves both discovery and live checkbox suppression while
// radar, reticle, shields, ammo, and the remaining HUD shaders stay untouched.
namespace halo4_helmet_shader
{
    inline constexpr uint64_t kVisorFramingHash = 0x4BE62AC49C2BF210ULL;

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

    constexpr bool IsVisorFramingShader(uint64_t hash) noexcept
    {
        return hash == kVisorFramingHash;
    }

    // Helmet frame size independent of the HUD. Halo 4 scales the whole gameplay
    // CUI, visor included (the stock-size visor sits outside the eye crop), about the CUI origin by vertical = hud_size and horizontal =
    // hud_size * hud_aspect (Halo4ComputeNativeHudAffine). With a visor size the
    // exact visor-shader draws get a viewport scaled about its centre by
    // visor/HUD on each axis, so the frame is drawn at its own size. Returns
    // false (draw unchanged) for 0 = "same as HUD", bad input or a ratio of 1.
    inline constexpr float kVisorSizeMin = 0.30f;
    inline constexpr float kVisorSizeMax = 1.00f;
    inline bool VisorViewportScale(
        float hudSize, float hudAspect, float visorSize, float& scaleX, float& scaleY) noexcept
    {
        scaleX = scaleY = 1.0f;
        if (!(visorSize > 0.0f) || !(hudSize > 0.0f) || !(hudAspect > 0.0f) || visorSize != visorSize ||
            hudSize != hudSize || hudAspect != hudAspect)
            return false;
        const auto clamp = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };
        const float hudVertical = clamp(hudSize, 0.30f, 1.0f);
        const float hudHorizontal = clamp(hudVertical * hudAspect, 0.15f, 1.0f);
        const float visorVertical = clamp(visorSize, kVisorSizeMin, kVisorSizeMax);
        const float visorHorizontal = clamp(visorVertical * hudAspect, 0.15f, 1.0f);
        scaleX = visorHorizontal / hudHorizontal;
        scaleY = visorVertical / hudVertical;
        if (!(scaleX > 0.0f) || !(scaleY > 0.0f) || scaleX > 10.0f || scaleY > 10.0f)
        {
            scaleX = scaleY = 1.0f;
            return false;
        }
        return scaleX != 1.0f || scaleY != 1.0f;
    }

    // Size the frame is drawn at. followHud keeps the old behaviour (frame shrinks
    // with the HUD, returns 0 = unchanged). Otherwise an explicit size wins, and 0
    // means "auto": 1.6x the HUD size, so a small HUD leaves the frame out at the
    // view edge instead of squeezing its opaque body into the middle of the view.
    inline constexpr float kAutoVisorFactor = 1.6f;
    inline float EffectiveVisorSize(bool followHud, float visorSize, float hudSize) noexcept
    {
        if (followHud)
            return 0.0f;
        if (visorSize > 0.0f)
            return visorSize;
        if (!(hudSize > 0.0f) || hudSize != hudSize)
            return 0.0f;
        const float auto_ = hudSize * kAutoVisorFactor;
        return auto_ < kVisorSizeMin ? kVisorSizeMin : auto_ > kVisorSizeMax ? kVisorSizeMax : auto_;
    }

    constexpr bool ShouldSuppress(
        bool shaderPathAvailable, bool halo4Active, bool helmetVisible, bool visorShaderBound) noexcept
    {
        return shaderPathAvailable && halo4Active && !helmetVisible && visorShaderBound;
    }
}
