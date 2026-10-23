// Shared test fixtures for the Halo 3 / ODST muzzle publication: the behaviour the shared
// publication template must keep, written once and run by both titles' tests
// (halo3_muzzle_publication_tests, odst_muzzle_tests). Test-only header; nothing in src/ includes it.
//
// Each test routes its palette store, lens and reticle stubs into the recorder below,
// then calls RunLegacyPublicationFixtures with a traits type over its own
// host globals.
#pragma once
#include <Windows.h>
#include <cstring>
#include <utility>
#include "../src/common/weapon_muzzle.h"

namespace legacy_publication_fixture
{
    // One publish, in call order: P = palette store, L = lens anchor, R = reticle.
    struct Event
    {
        char kind{};
        weapon_muzzle::Palette palette{};
    };
    inline Event events[16]{};
    inline unsigned eventCount = 0;
    inline void Clear()
    {
        eventCount = 0;
    }
    inline void Record(char kind, const weapon_muzzle::Palette& palette)
    {
        if (eventCount < 16)
            events[eventCount] = {kind, palette};
        ++eventCount;
    }
    inline void RecordReceipt(char kind, const weapon_muzzle::Receipt& receipt)
    {
        weapon_muzzle::Palette palette{};
        palette.barrels[0] = receipt;
        Record(kind, palette);
    }
    inline bool Full(const weapon_muzzle::Palette& palette)
    {
        return palette.barrels[0].identity != 0 || palette.barrels[1].identity != 0;
    }
}

// The title's muzzle store, recording each Publish before it commits.
class RecordingMuzzleStore
{
    weapon_muzzle::Store store_;

public:
    bool Publish(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& palette) noexcept
    {
        legacy_publication_fixture::Record('P', palette);
        return store_.Publish(title, slot, palette);
    }
    template <class... Args>
    bool Read(Args&&... args) const noexcept
    {
        return store_.Read(std::forward<Args>(args)...);
    }
};

// Fx (per test): Context, Node, kTitle, HasLens(marker), Publish(tag,context,nodes,generation),
// Config(), BindingsReady(), RuntimeGeneration(), Scope(), Owner(),
// Weapon(slot), SetSeated(bool), SetLookup(checksum,count), LookupChecksum(),
// LookupCount(), SetLookupFault(bool).
template <class Fx>
void RunLegacyPublicationFixtures(void (*check)(bool, const char*))
{
    namespace fx = legacy_publication_fixture;
    using Context = typename Fx::Context;
    using Node = typename Fx::Node;
    constexpr GameTitle title = Fx::kTitle;
    constexpr uint32_t generation = 7;

    // A barrel-0 marker whose model also has a scope lens row, so the lens
    // switch has something to switch off.
    const weapon_muzzle::Marker* pick = nullptr;
    for (const auto& marker : weapon_muzzle::kMarkers)
    {
        if (marker.title != title || marker.barrel != 0)
            continue;
        if (Fx::HasLens(marker))
        {
            pick = &marker;
            break;
        }
    }
    check(pick != nullptr, "M2 fixture setup: an authored barrel-0 marker with a lens row");
    if (!pick)
        return;
    const uint32_t checksum = uint32_t(pick->identity);
    const int count = pick->nodeCount;
    static Node nodes[64];
    for (auto& node : nodes)
    {
        node.scale = 2;
        node.translation[0] = 2;
        node.translation[1] = 3;
        node.translation[2] = 4;
    }
    const auto tracking = Fx::Scope().anatomicalTracking;

    auto fresh = [&]
    {
        Fx::Config().gun_barrel_aim = true;
        Fx::Config().left_handed = false;
        Fx::Config().scope_lens_node_anchor = true;
        Fx::BindingsReady() = true;
        Fx::RuntimeGeneration() = generation;
        Fx::Scope().armed = true;
        Fx::Scope().anatomicalTracking = tracking;
        Fx::SetSeated(false);
        Fx::SetLookup(checksum, count);
        Fx::SetLookupFault(false);
        fx::Clear();
    };
    auto context = [&](int slot)
    {
        Context c{};
        c.slot = slot;
        c.generation = generation;
        c.muzzleUnit = Fx::Owner();
        c.muzzleWeapon = Fx::Weapon(slot);
        return c;
    };
    auto publish = [&](const Context& c, const Node* destination, uint32_t gen)
    {
        fx::Clear();
        Fx::Publish(123, c, destination, gen);
    };
    // Past the gate: palette, lens, reticle, in that order; the reticle gets the palette's barrel 0.
    auto expectThree = [&](bool palette, bool lens, const char* name)
    {
        bool ok = fx::eventCount == 3 && fx::events[0].kind == 'P' && fx::events[1].kind == 'L' &&
                  fx::events[2].kind == 'R' && fx::Full(fx::events[0].palette) == palette &&
                  fx::Full(fx::events[1].palette) == lens &&
                  fx::events[2].palette.barrels[0].identity == fx::events[0].palette.barrels[0].identity;
        check(ok, name);
    };

    // 1. Gate table: nothing at all is published.
    struct Gate
    {
        const char* name;
        void (*arrange)(Context&, const Node*&, uint32_t&);
    };
    const Gate gates[]{
        {"M2 gate: barrel option off publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::Config().gun_barrel_aim = false; }},
        {"M2 gate: bindings not ready publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::BindingsReady() = false; }},
        {"M2 gate: invalid context publishes nothing", [](Context& c, const Node*&, uint32_t&) { c.valid = false; }},
        {"M2 gate: player 1 publishes nothing", [](Context& c, const Node*&, uint32_t&) { c.player = 1; }},
        {"M2 gate: slot -1 publishes nothing", [](Context& c, const Node*&, uint32_t&) { c.slot = -1; }},
        {"M2 gate: slot 2 publishes nothing", [](Context& c, const Node*&, uint32_t&) { c.slot = 2; }},
        {"M2 gate: generation 0 publishes nothing",
            [](Context& c, const Node*&, uint32_t& g)
            {
                g = 0;
                c.generation = 0;
            }},
        {"M2 gate: context from another generation publishes nothing",
            [](Context& c, const Node*&, uint32_t&) { c.generation = 8; }},
        {"M2 gate: stale runtime generation publishes nothing",
            [](Context& c, const Node*&, uint32_t& g)
            {
                g = 8;
                c.generation = 8;
            }},
        {"M2 gate: no destination publishes nothing", [](Context&, const Node*& d, uint32_t&) { d = nullptr; }},
        {"M2 gate: solve scope not armed publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::Scope().armed = false; }},
        {"M2 gate: node count 0 publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::SetLookup(Fx::LookupChecksum(), 0); }},
        {"M2 gate: node count -1 publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::SetLookup(Fx::LookupChecksum(), -1); }},
        {"M2 gate: node count 65 publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::SetLookup(Fx::LookupChecksum(), 65); }},
        {"M2 gate: a model with no authored marker publishes nothing",
            [](Context&, const Node*&, uint32_t&) { Fx::SetLookup(0x7FFFFFF1u, Fx::LookupCount()); }},
    };
    check(!weapon_muzzle::Find(title, 0x7FFFFFF1u, 0) && !weapon_muzzle::Find(title, 0x7FFFFFF1u, 1),
        "M2 fixture setup: the unauthored checksum has no marker");
    for (const auto& gate : gates)
    {
        fresh();
        Context c = context(0);
        const Node* destination = nodes;
        uint32_t gen = generation;
        gate.arrange(c, destination, gen);
        publish(c, destination, gen);
        check(fx::eventCount == 0, gate.name);
    }

    // 2. Empty-publish table: one empty palette, one empty lens, one reticle with the empty palette.
    for (int slot = 0; slot < 2; ++slot)
    {
        struct Empty
        {
            const char* name;
            void (*arrange)(Context&, int);
        };
        const Empty empties[]{
            {"M2 empty: seated owner capture fails", [](Context&, int) { Fx::SetSeated(true); }},
            {"M2 empty: another unit in the context", [](Context& c, int) { c.muzzleUnit ^= 0x10000; }},
            {"M2 empty: another weapon in the context", [](Context& c, int) { c.muzzleWeapon ^= 0x10000; }},
            {"M2 empty: tracking serial 0", [](Context&, int) { Fx::Scope().anatomicalTracking.serial = 0; }},
            {"M2 empty: tracking epoch 0", [](Context&, int) { Fx::Scope().anatomicalTracking.referenceEpoch = 0; }},
            {"M2 empty: tracking time 0", [](Context&, int) { Fx::Scope().anatomicalTracking.timeNs = 0; }},
            {"M2 empty: the other hand invalid",
                [](Context&, int s) { Fx::Scope().anatomicalTracking.hands[s == 0 ? 1 : 0].valid = false; }},
        };
        for (const auto& empty : empties)
        {
            fresh();
            Context c = context(slot);
            empty.arrange(c, slot);
            publish(c, nodes, generation);
            expectThree(false, false, empty.name);
        }
        // The same hand being invalid is not a refusal.
        fresh();
        Context c = context(slot);
        Fx::Scope().anatomicalTracking.hands[slot].valid = false;
        publish(c, nodes, generation);
        expectThree(true, true, "M2 accepted: only the other hand's tracking is required");
    }

    // 3. Lens switch.
    for (int slot = 0; slot < 2; ++slot)
    {
        fresh();
        publish(context(slot), nodes, generation);
        expectThree(true, true, "M2 lens on: palette, lens and reticle all carry the gun");
        check(fx::events[1].palette.barrels[0].identity == checksum, "M2 lens on: the lens is this gun's");
        fresh();
        Fx::Config().scope_lens_node_anchor = false;
        publish(context(slot), nodes, generation);
        expectThree(true, false, "M2 lens off: the lens is empty and the palette is not");
    }

    // 4. Fault: one empty palette and nothing else.
    fresh();
    Fx::SetLookupFault(true);
    publish(context(0), nodes, generation);
    check(fx::eventCount == 1 && fx::events[0].kind == 'P' && !fx::Full(fx::events[0].palette),
        "M2 fault: a fault in the model lookup publishes one empty palette");
    void* noAccess = VirtualAlloc(nullptr, sizeof(Node) * 64, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    check(noAccess != nullptr, "M2 fixture setup: a no-access destination");
    fresh();
    publish(context(0), static_cast<const Node*>(noAccess), generation);
    check(fx::eventCount == 1 && fx::events[0].kind == 'P' && !fx::Full(fx::events[0].palette),
        "M2 fault: a fault reading the destination publishes one empty palette");
    if (noAccess)
        VirtualFree(noAccess, 0, MEM_RELEASE);

    // 5. Receipt fields on an accepted publication.
    for (int handed = 0; handed < 2; ++handed)
        for (int slot = 0; slot < 2; ++slot)
        {
            fresh();
            Fx::Config().left_handed = handed != 0;
            const uint64_t before = GetTickCount64();
            publish(context(slot), nodes, generation);
            const uint64_t after = GetTickCount64();
            expectThree(true, true, "M2 receipt: accepted publication");
            const auto& palette = fx::events[0].palette;
            for (uint8_t barrel = 0; barrel < 2; ++barrel)
            {
                const auto& r = palette.barrels[barrel];
                if (!weapon_muzzle::Find(title, checksum, barrel))
                {
                    check(r.identity == 0, "M2 receipt: an unauthored barrel stays empty");
                    continue;
                }
                weapon_muzzle::Ray ray{};
                const auto* marker = weapon_muzzle::Find(title, checksum, barrel);
                check(weapon_muzzle::Transform(
                          *marker, 2, nodes[marker->node].rotation, nodes[marker->node].translation, ray),
                    "M2 receipt: expected ray setup");
                check(r.title == title && r.generation == generation && r.unit == Fx::Owner() &&
                          r.weapon == Fx::Weapon(slot) && r.identity == checksum &&
                          r.space == tracking.referenceEpoch && r.serial == tracking.serial && r.at >= before &&
                          r.at <= after && r.timeNs == tracking.timeNs && r.slot == uint8_t(slot) &&
                          r.barrel == barrel && r.leftHanded == (handed != 0) &&
                          std::memcmp(&r.ray, &ray, sizeof(ray)) == 0,
                    "M2 receipt: title, generation, unit, weapon, checksum, epoch, serial, time, slot, barrel, left_handed and ray");
            }
            const auto& lens = fx::events[1].palette.barrels[0];
            check(lens.title == title && lens.generation == generation && lens.unit == Fx::Owner() &&
                      lens.weapon == Fx::Weapon(slot) && lens.identity == checksum &&
                      lens.space == tracking.referenceEpoch && lens.serial == tracking.serial &&
                      lens.slot == uint8_t(slot) && lens.barrel == 0 && lens.leftHanded == (handed != 0),
                "M2 receipt: the lens carries the same receipt fields");
        }
    fresh();
}
