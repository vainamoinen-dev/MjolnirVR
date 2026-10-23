#pragma once
// Read-only diagnostic that tallies Reach's instance occlusion cache each frame, split by whether a
// scope-glass gun is held, and formats a once-per-second log line. Used by src/dll/core/game.cpp, which
// reads the cache and writes the log; tested in tests/reach_scope_tests.cpp. Never writes engine memory.
//
// A per-frame tally of Reach's instance occlusion cache
// (retail haloreach.dll, decoded from the readback 0x2937CC, the cull test 0x293AB4 and the
// query submit 0x293400). Sampled once per frame after the
// engine's readback and CPU visibility and before the first player_view_render submits the
// frame's queries, so "culling" counts the instances the eyes lose this frame and "pending"
// counts queries whose result did not come back by this frame's readback (late results
// never cull: the cull needs age <= 1).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace reach_occlusion
{
    inline constexpr uintptr_t kKeysRva = 0xD0B5C0;  // uint32_t keys[kEntries]
    inline constexpr uintptr_t kStateRva = 0xCFB5F0; // kStateStride bytes per entry
    inline constexpr int kEntries = 0xFFD;
    inline constexpr size_t kStateStride = 16;

    // Key: negative = never used, bit 30 = deleted, bit 24 = needs a query this frame,
    // low 20 bits = instance + bsp << 13 + window << 17.
    // State byte 0: bit 0 query outstanding, bit 1 result valid, bit 2 last result 0 pixels;
    // byte 3: frames since the last submit (signed, capped at 100).
    struct Tally
    {
        uint32_t live = 0, flagged = 0, occluded = 0, culling = 0, pending = 0;
        uint32_t pendingAge[4]{}; // pending queries by age 0, 1, 2, 3+
    };

    inline void Count(const uint32_t* keys, const uint8_t* states, int count, int window, Tally& t)
    {
        for (int i = 0; i < count; ++i)
        {
            const uint32_t key = keys[i];
            if (static_cast<int32_t>(key) < 0 || (key >> 30 & 1u))
                continue;
            if (static_cast<int>((key & 0xFFFFFu) >> 17) != window)
                continue;
            ++t.live;
            if (key & 0x1000000u)
                ++t.flagged;
            const uint8_t* s = states + static_cast<size_t>(i) * kStateStride;
            const int age = static_cast<int8_t>(s[3]);
            if (s[0] & 4u)
            {
                ++t.occluded;
                if (age <= 1)
                    ++t.culling;
            }
            if (s[0] & 1u)
                ++t.pendingAge[std::clamp(age, 0, 3)], ++t.pending;
        }
    }

    // One second of frames, split by whether a scope-glass gun was held.
    struct Second
    {
        struct Side
        {
            uint32_t frames = 0, scopePasses = 0, passes = 0;
            uint64_t live = 0, flagged = 0, occluded = 0, culling = 0, pending = 0, pendingAge[4]{};
            uint32_t cullingMax = 0;
        } held, none;
        void Add(const Tally& t, bool scopeHeld, bool scopeRan, uint32_t passes)
        {
            Side& s = scopeHeld ? held : none;
            ++s.frames;
            s.scopePasses += scopeRan ? 1u : 0u;
            s.passes += passes;
            s.live += t.live;
            s.flagged += t.flagged;
            s.occluded += t.occluded;
            s.culling += t.culling;
            s.pending += t.pending;
            for (int i = 0; i < 4; ++i)
                s.pendingAge[i] += t.pendingAge[i];
            s.cullingMax = std::max(s.cullingMax, t.culling);
        }
    };

    inline int FormatSide(char* out, size_t size, const char* name, const Second::Side& s)
    {
        if (!s.frames)
            return std::snprintf(out, size, "%s: 0 frames", name);
        const double n = s.frames;
        return std::snprintf(out, size,
            "%s: %u frames (scope pass %u, passes/frame %.2f) live %.0f flagged %.1f occluded %.1f culling %.1f (max %u) "
            "pending %.1f (age 0/1/2/3+ %.1f/%.1f/%.1f/%.1f)",
            name, s.frames, s.scopePasses, s.passes / n, s.live / n, s.flagged / n, s.occluded / n, s.culling / n,
            s.cullingMax, s.pending / n, s.pendingAge[0] / n, s.pendingAge[1] / n, s.pendingAge[2] / n,
            s.pendingAge[3] / n);
    }

    // "Reach occlusion cache: window 0, per-frame means over 1 s | scope gun: ... | no scope: ..."
    inline void Format(const Second& s, char* out, size_t size)
    {
        char held[256], none[256];
        FormatSide(held, sizeof held, "scope gun", s.held);
        FormatSide(none, sizeof none, "no scope", s.none);
        std::snprintf(out, size, "Reach occlusion cache: window 0, per-frame means over 1 s | %s | %s", held, none);
    }
} // namespace reach_occlusion
