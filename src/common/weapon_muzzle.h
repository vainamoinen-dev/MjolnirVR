// Weapon muzzles: authored muzzle markers per gun, muzzle rays from the drawn gun, and fresh-receipt rules.
// Used by dual_reticle_logic.h, lens_anchor.h, src/dll/core/game.cpp and the CE and Halo 2 adapters; tested by
// tests/weapon_muzzle_tests.cpp and the per-title muzzle tests. No globals; it writes only marker records
// the caller passes in.
#pragma once
#include "runtime_types.h"
#include "titles/ce/haloce_snapshot.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace weapon_muzzle
{
    struct Marker
    {
        GameTitle title{};
        uint64_t identity{};
        uint16_t nodeCount{}, node{};
        uint8_t barrel{};
        float position[3]{}, forward[3]{}, up[3]{0, 0, 1};
    };
    struct Ray
    {
        float position[3]{}, direction[3]{}, up[3]{0, 0, 1};
    };

    // The adapter must resolve this exact authored node in its committed visible
    // palette. A controller/grip pose is not a substitute for that node matrix.
    inline bool Transform(
        const Marker& marker, float scale, const float basis[9], const float position[3], Ray& output) noexcept
    {
        if (!marker.identity || !marker.nodeCount || marker.node >= marker.nodeCount || !basis || !position ||
            !std::isfinite(scale) || scale <= .000001f || scale >= 100.f)
            return false;
        for (int column = 0; column < 3; ++column)
        {
            float length = 0;
            for (int row = 0; row < 3; ++row)
            {
                const auto component = basis[column * 3 + row];
                if (!std::isfinite(component))
                    return false;
                length += component * component;
            }
            if (std::abs(length - 1.f) > .05f)
                return false;
            for (int other = 0; other < column; ++other)
            {
                float dot = 0;
                for (int row = 0; row < 3; ++row)
                    dot += basis[column * 3 + row] * basis[other * 3 + row];
                if (std::abs(dot) > .025f)
                    return false;
            }
        }
        Ray staged{};
        float length = 0, upLength = 0, dot = 0;
        for (float& value : staged.up)
            value = 0;
        for (int row = 0; row < 3; ++row)
        {
            staged.position[row] = position[row];
            for (int column = 0; column < 3; ++column)
            {
                staged.position[row] += scale * basis[column * 3 + row] * marker.position[column];
                staged.direction[row] += basis[column * 3 + row] * marker.forward[column];
                staged.up[row] += basis[column * 3 + row] * marker.up[column];
            }
            if (!std::isfinite(staged.position[row]) || std::abs(staged.position[row]) > 1000000.f ||
                !std::isfinite(staged.direction[row]) || !std::isfinite(staged.up[row]))
                return false;
            length += staged.direction[row] * staged.direction[row];
            upLength += staged.up[row] * staged.up[row];
            dot += staged.direction[row] * staged.up[row];
        }
        if (!std::isfinite(length) || length < .95f || length > 1.05f || !std::isfinite(upLength) || upLength < .95f ||
            upLength > 1.05f || std::abs(dot) > .025f)
            return false;
        const float inverse = 1.f / std::sqrt(length);
        for (float& value : staged.direction)
            value *= inverse;
        const float upInverse = 1.f / std::sqrt(upLength);
        for (float& value : staged.up)
            value *= upInverse;
        output = staged;
        return true;
    }

    struct Receipt
    {
        GameTitle title{};
        uint32_t generation{}, unit = UINT32_MAX, weapon = UINT32_MAX;
        uint64_t identity{}, space{}, serial{}, at{};
        int64_t timeNs{};
        uint8_t slot{}, barrel{};
        bool leftHanded{};
        Ray ray{};
    };
    inline bool Fresh(const Receipt& sample, GameTitle title, uint32_t generation, uint32_t unit, uint32_t weapon,
        uint64_t space, uint64_t now, int64_t timeNs, uint8_t slot, uint8_t barrel, bool leftHanded) noexcept
    {
        return title != GameTitle::None && sample.title == title && generation && sample.generation == generation &&
               unit != UINT32_MAX && sample.unit == unit && weapon != UINT32_MAX && sample.weapon == weapon &&
               sample.identity && space && sample.space == space && sample.serial && sample.at && now >= sample.at &&
               now - sample.at <= 100 && sample.timeNs > 0 && timeNs >= sample.timeNs &&
               timeNs - sample.timeNs <= 100000000 && slot < 2 && sample.slot == slot && barrel < 2 &&
               sample.barrel == barrel && sample.leftHanded == leftHanded;
    }
    using Publication = halo_ce::Snapshot<Receipt>;

    // Both barrels belong to one committed palette. Publish the whole sample so a
    // rejected/hidden node immediately invalidates its previous successful receipt.
    struct Palette
    {
        Receipt barrels[2]{};
    };
    class Store
    {
        halo_ce::Snapshot<Palette> slots_[7][2];
        static bool Supported(GameTitle title) noexcept
        {
            return title >= GameTitle::Halo3 && title <= GameTitle::Halo2;
        }

    public:
        bool Publish(GameTitle title, uint8_t slot, const Palette& palette) noexcept
        {
            if (!Supported(title) || slot >= 2)
                return false;
            return slots_[static_cast<unsigned>(title)][slot].Publish(palette);
        }
        bool Read(GameTitle title, uint32_t generation, uint32_t unit, uint32_t weapon, uint64_t space, uint64_t now,
            int64_t timeNs, uint8_t slot, uint8_t barrel, bool leftHanded, Receipt& output) const noexcept
        {
            if (!Supported(title) || slot >= 2 || barrel >= 2)
                return false;
            Palette palette{};
            if (!slots_[static_cast<unsigned>(title)][slot].Read(palette))
                return false;
            const auto& sample = palette.barrels[barrel];
            if (!Fresh(sample, title, generation, unit, weapon, space, now, timeNs, slot, barrel, leftHanded))
                return false;
            output = sample;
            return true;
        }
    };
}

#include "weapon_muzzles.generated.h"
namespace weapon_muzzle
{
    inline const Marker* Find(GameTitle title, uint64_t identity, uint8_t barrel) noexcept
    {
        if (!identity || barrel >= 2)
            return nullptr;
        for (const auto& marker : kMarkers)
            if (marker.title == title && marker.identity == identity && marker.barrel == barrel)
                return &marker;
        return nullptr;
    }

    // Fills both barrels of one committed palette. `base` carries every receipt
    // field except barrel and ray; `nodeRay(marker, ray)` resolves the title's own
    // committed node for that marker and transforms it (false skips the barrel).
    // A barrel whose marker is missing or does not fit `count` nodes stays empty.
    template <class NodeRay>
    void FillPalette(
        Palette& palette, GameTitle title, uint64_t identity, uint32_t count, const Receipt& base, NodeRay nodeRay)
    {
        for (uint8_t barrel = 0; barrel < 2; ++barrel)
        {
            const auto* marker = Find(title, identity, barrel);
            Ray ray{};
            if (!marker || marker->nodeCount != count || marker->node >= count || !nodeRay(*marker, ray))
                continue;
            Receipt receipt = base;
            receipt.barrel = barrel;
            receipt.ray = ray;
            palette.barrels[barrel] = receipt;
        }
    }

    // The firing-query context is usable for one shot only if it was sampled
    // after this install, by this generation, within the last 100 ms.
    inline bool QuerySampleFresh(uint64_t sampleMs, uint32_t sampleGeneration, uint64_t installedAtMs,
        uint32_t generation, uint64_t now) noexcept
    {
        return sampleMs && sampleMs >= installedAtMs && sampleGeneration == generation && now >= sampleMs &&
               now - sampleMs <= 100;
    }

    // Native marker record world frame shared by all six titles: forward +3C,
    // side +48 (up x forward), up +54, position +60. Everything else in the
    // record (node, region, local matrix, scale, flags) is left as native wrote it.
    inline void WriteMarkerWorldFrame(
        void* markers, const float direction[3], const float up[3], const float position[3]) noexcept
    {
        auto* bytes = static_cast<uint8_t*>(markers);
        const float side[3]{up[1] * direction[2] - up[2] * direction[1], up[2] * direction[0] - up[0] * direction[2],
            up[0] * direction[1] - up[1] * direction[0]};
        std::memcpy(bytes + 0x3C, direction, 12);
        std::memcpy(bytes + 0x48, side, 12);
        std::memcpy(bytes + 0x54, up, 12);
        std::memcpy(bytes + 0x60, position, 12);
    }
}
