// Tests the shared muzzle code in weapon_muzzle.h and muzzle_refusal.h, plus the Halo 2 muzzle
// publication (halo2_muzzle_publication.inl, compiled in directly): palettes, marker writes, query
// freshness and refusals. Built as halomccvr_weapon_muzzle_tests. No game process.
#include "../src/common/weapon_muzzle.h"
#include "../src/common/muzzle_refusal.h"
#include "../src/common/titles/h2/halo2_render_logic.h"
#include "../src/common/contact_melee_motion.h"
namespace lens_anchor
{ // the real header pulls in weapon_profiles.h (its own tests cover it)
    template <class X>
    bool Fill(weapon_muzzle::Palette&, GameTitle, uint64_t, uint32_t, const weapon_muzzle::Receipt&, X)
    {
        return false;
    }
}
static void VR_PublishWeaponLensRay(
    GameTitle, uint8_t, const weapon_muzzle::Receipt&, const contact_melee::TrackingToWorld&) noexcept
{
}
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks{};
static void Check(bool value, const char* name)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}
static bool Near(float a, float b)
{
    return std::abs(a - b) < .00001f;
}
static constexpr uint32_t kOwnedUser = 0;
static std::atomic<uint32_t> g_generation{7};
static uint64_t GetTickCount64()
{
    return 1000;
}
struct Halo2VisibleConsumerContext
{
    bool valid = true;
    uint32_t user = 0, unitObject = 0x12340001, weaponObject = 0x56780002, secondaryWeaponObject = 0x789A0003;
    uint32_t muzzleGeneration = 7;
    uint64_t muzzleSpace = 3, muzzleSerial = 11;
    int64_t muzzleTimeNs = 1000000000;
    bool muzzleLeftHanded = false;
    contact_melee::Frame contactFrames[2]{};
};
static unsigned reticlePublications{};
static void VR_PublishWeaponReticleRay(
    GameTitle title, uint8_t slot, const weapon_muzzle::Receipt&, const contact_melee::TrackingToWorld&) noexcept
{
    Check(title == GameTitle::Halo2 && slot < 2, "reticle publication retains title/slot");
    ++reticlePublications;
}
#include "../src/dll/titles/h2/halo2_muzzle_publication.inl"
int main()
{
    using namespace weapon_muzzle;
    const float identity[]{1, 0, 0, 0, 1, 0, 0, 0, 1}, position[]{100, 200, 300};
    // Asymmetric quarter-turn and reflected palettes expose row/column and
    // handedness errors which identity-only transforms cannot detect.
    const float turn[]{0, 1, 0, -1, 0, 0, 0, 0, 1}, mirror[]{1, 0, 0, 0, -1, 0, 0, 0, 1};
    Marker marker{GameTitle::Halo3, 1, 4, 2, 0, {.25f, .5f, .75f}, {1, 0, 0}};
    Ray ray{};
    Check(Transform(marker, 2, turn, position, ray) && Near(ray.position[0], 99) && Near(ray.position[1], 200.5f) &&
              Near(ray.position[2], 301.5f) && ray.direction[1] == 1,
        "authored node offset transforms with scale and rotated native palette");
    Check(Transform(marker, 2, mirror, position, ray) && Near(ray.position[1], 199) && ray.direction[0] == 1,
        "reflected visible palette preserves its actual muzzle");
    const float roll[]{1, 0, 0, 0, 0, 1, 0, -1, 0};
    Check(Transform(marker, 1, roll, position, ray) && ray.up[1] == -1 && ray.up[2] == 0,
        "weapon roll survives independently of the forward axis");
    Ray sentinel{{7, 8, 9}, {4, 5, 6}};
    ray = sentinel;
    marker.node = 4;
    Check(!Transform(marker, 1, identity, position, ray) && ray.position[0] == 7,
        "unknown node refuses without changing output");
    marker.node = 2;
    float broken[9]{1, 0, 0, 1, 0, 0, 0, 0, 1};
    Check(!Transform(marker, 1, broken, position, ray),
        "collapsed or non-orthogonal palettes never become firing authority");
    Check(!Transform(marker, 0, identity, position, ray), "hidden zero-scale node is not a muzzle");
    marker.position[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!Transform(marker, 1, identity, position, ray), "nonfinite authored data refused");
    for (const auto& authored : kMarkers)
    {
        Check(Find(authored.title, authored.identity, authored.barrel) == &authored, "catalog key is unique and exact");
        Check(Transform(authored, 1, identity, position, ray), "every admitted authored marker forms a finite ray");
        Check(Near(ray.position[0], 100 + authored.position[0]) && Near(ray.position[1], 200 + authored.position[1]) &&
                  Near(ray.position[2], 300 + authored.position[2]),
            "catalog position remains node-local until composition");
    }
    Check(!Find(GameTitle::None, 1, 0) && !Find(GameTitle::Halo3, 1, 0) && !Find(GameTitle::Halo3, 0, 0),
        "unknown title/model has no invented muzzle");
    Receipt receipt{GameTitle::Halo3, 7, 0x12340001, 0x56780002, 1, 3, 8, 1000, 1000000000, 0, 0, false, ray};
    auto fresh = [&]
    { return Fresh(receipt, GameTitle::Halo3, 7, 0x12340001, 0x56780002, 3, 1050, 1050000000, 0, 0, false); };
    Check(fresh(), "exact current owned weapon receipt");
    for (int reason = 0; reason < 12; ++reason)
    {
        const auto saved = receipt;
        switch (reason)
        {
        case 0:
            receipt.generation = 8;
            break;
        case 1:
            receipt.unit ^= 0x10000;
            break;
        case 2:
            receipt.weapon ^= 0x10000;
            break;
        case 3:
            receipt.space = 4;
            break;
        case 4:
            receipt.at = 1100;
            break;
        case 5:
            receipt.at = 900;
            break;
        case 6:
            receipt.timeNs = 1100000000;
            break;
        case 7:
            receipt.timeNs = 900000000;
            break;
        case 8:
            receipt.leftHanded = true;
            break;
        case 9:
            receipt.slot = 1;
            break;
        case 10:
            receipt.barrel = 1;
            break;
        case 11:
            receipt.serial = 0;
            break;
        }
        Check(!fresh(), "stale/reused/foreign receipt cannot move native shots");
        receipt = saved;
    }
    Publication publication;
    Receipt read{};
    Check(!publication.Read(read) && publication.Publish(receipt) && publication.Read(read) &&
              read.weapon == receipt.weapon,
        "bounded publication retains full weapon identity");
    Halo2VisibleConsumerContext context;
    context.contactFrames[0].serial = context.contactFrames[1].serial = context.muzzleSerial;
    float matrices[kHalo2FirstPersonPaletteCapacity * kHalo2FirstPersonNodeFloats]{};
    Halo2FirstPersonTransform node{};
    node.scale = 2;
    std::memcpy(node.rotation, roll, sizeof(roll));
    std::memcpy(node.translation, position, sizeof(position));
    for (const auto& authored : kMarkers)
    {
        if (authored.title != GameTitle::Halo2)
            continue;
        for (unsigned i = 0; i < authored.nodeCount; ++i)
            Halo2WriteFirstPersonTransform(node, matrices + i * kHalo2FirstPersonNodeFloats);
        for (uint8_t slot = 0; slot < 2; ++slot)
        {
            const auto beforeReticle = reticlePublications;
            Halo2PublishMuzzlePalette(context, slot, authored.identity, matrices, authored.nodeCount);
            Check(reticlePublications == beforeReticle + 1, "each current hand publishes its reticle ray");
            const uint32_t weapon = slot ? context.secondaryWeaponObject : context.weaponObject;
            Ray expected{};
            Check(Transform(authored, node.scale, node.rotation, node.translation, expected),
                "expected marker orientation");
            Check(g_halo2Muzzles.Read(GameTitle::Halo2, 7, context.unitObject, weapon, 3, 1050, 1050000000, slot,
                      authored.barrel, false, read) &&
                      read.ray.position[0] == expected.position[0] && read.ray.up[1] == expected.up[1] &&
                      read.ray.direction[0] == expected.direction[0],
                "production publisher uses each actual final weapon palette and full identity");
            Halo2PublishMuzzlePalette(context, slot, authored.identity, matrices, authored.nodeCount + 1);
            Check(!g_halo2Muzzles.Read(GameTitle::Halo2, 7, context.unitObject, weapon, 3, 1050, 1050000000, slot,
                      authored.barrel, false, read),
                "mismatched palette invalidates previous marker");
        }
    }
    const auto& h2 = kMarkers[0];
    for (unsigned i = 0; i < h2.nodeCount; ++i)
        Halo2WriteFirstPersonTransform(node, matrices + i * kHalo2FirstPersonNodeFloats);
    for (int reason = 0; reason < 10; ++reason)
    {
        context = {};
        Halo2PublishMuzzlePalette(context, 0, h2.identity, matrices, h2.nodeCount);
        auto changed = context;
        const float* data = matrices;
        uint32_t count = h2.nodeCount;
        switch (reason)
        {
        case 0:
            changed.valid = false;
            break;
        case 1:
            changed.user = 1;
            break;
        case 2:
            changed.muzzleGeneration = 8;
            break;
        case 3:
            changed.unitObject = UINT32_MAX;
            break;
        case 4:
            changed.weaponObject = UINT32_MAX;
            break;
        case 5:
            changed.muzzleSpace = 0;
            break;
        case 6:
            changed.muzzleSerial = 0;
            break;
        case 7:
            changed.muzzleTimeNs = 0;
            break;
        case 8:
            data = nullptr;
            break;
        case 9:
            count = kHalo2FirstPersonPaletteCapacity + 1;
            break;
        }
        Halo2PublishMuzzlePalette(changed, 0, h2.identity, data, count);
        Check(!g_halo2Muzzles.Read(GameTitle::Halo2, 7, context.unitObject, context.weaponObject, 3, 1050, 1050000000,
                  0, 0, false, read),
            "invalid committed packet cannot retain previous muzzle authority");
    }
    Store store;
    Palette pair{};
    pair.barrels[0] = receipt;
    Check(!store.Publish(GameTitle::Unknown, 0, pair) && !store.Publish(GameTitle::Halo3, 2, pair),
        "invalid title/slot never indexes storage");
    Check(store.Publish(GameTitle::Halo3, 0, pair) &&
              store.Read(GameTitle::Halo3, 7, receipt.unit, receipt.weapon, 3, 1050, 1050000000, 0, 0, false, read),
        "store reads exact committed barrel");
    Check(!store.Read(GameTitle::Halo2, 7, receipt.unit, receipt.weapon, 3, 1050, 1050000000, 0, 0, false, read) &&
              !store.Read(GameTitle::Halo3, 7, receipt.unit, receipt.weapon, 3, 1050, 1050000000, 1, 0, false, read),
        "titles and weapon slots cannot borrow another publication");

    // Shared marker world-frame write: same bytes as each title's former
    // inline copy (side = up x forward), nothing outside +3C..+6B touched.
    {
        unsigned char record[0x80];
        std::memset(record, 0xAB, sizeof(record));
        const float d[3]{.6f, .8f, 0}, u[3]{0, 0, 1}, p[3]{1.5f, -2.25f, 3.125f};
        WriteMarkerWorldFrame(record, d, u, p);
        const float side[3]{u[1] * d[2] - u[2] * d[1], u[2] * d[0] - u[0] * d[2], u[0] * d[1] - u[1] * d[0]};
        Check(!std::memcmp(record + 0x3C, d, 12) && !std::memcmp(record + 0x48, side, 12) &&
                  !std::memcmp(record + 0x54, u, 12) && !std::memcmp(record + 0x60, p, 12),
            "marker world frame written bit-exact");
        bool untouched = true;
        for (size_t i = 0; i < sizeof(record); ++i)
            if ((i < 0x3C || i >= 0x6C) && record[i] != 0xAB)
                untouched = false;
        Check(untouched, "marker write leaves native node/local/flag bytes alone");
    }
    // Shared query freshness equals the former inline expression on a grid.
    {
        const auto old = [](uint64_t sample, uint32_t sampleGen, uint64_t installed, uint32_t gen, uint64_t now)
        { return !(!sample || sample < installed || sampleGen != gen || now < sample || now - sample > 100); };
        const uint64_t values[]{0, 1, 99, 100, 101, 200, 1000, 1099, 1100, 1101, UINT64_MAX};
        bool same = true;
        for (auto sample : values)
            for (auto installed : values)
                for (auto now : values)
                    for (uint32_t sampleGen : {0u, 7u, 8u})
                        same = same && QuerySampleFresh(sample, sampleGen, installed, 7, now) ==
                                           old(sample, sampleGen, installed, 7, now);
        Check(same, "query freshness matches the former inline gate");
        Check(QuerySampleFresh(1000, 7, 900, 7, 1100) && !QuerySampleFresh(1000, 7, 900, 7, 1101),
            "100 ms window inclusive");
    }
    // Shared palette fill: base receipt kept, barrel/ray set per marker,
    // missing marker / wrong node count / refused node leave the barrel empty.
    {
        const Marker* markerInner = nullptr;
        for (const auto& m : kMarkers)
            if (m.barrel == 0 && !Find(m.title, m.identity, 1))
            {
                markerInner = &m;
                break;
            }
        Check(markerInner != nullptr, "catalog has a single-barrel marker");
        const Receipt base{
            markerInner->title, 7, 0x12340001, 0x56780002, markerInner->identity, 3, 11, 1000, 1000000000, 1, 0, true};
        unsigned calls = 0;
        const auto nodeInner = [&](const Marker& m, Ray& ray)
        {
            ++calls;
            Check(&m == markerInner, "node callback gets the catalog marker");
            ray.position[0] = 42;
            return true;
        };
        Palette filled{};
        FillPalette(filled, markerInner->title, markerInner->identity, markerInner->nodeCount, base, nodeInner);
        const auto& b0 = filled.barrels[0];
        Check(calls == 1 && b0.title == base.title && b0.generation == 7 && b0.unit == base.unit &&
                  b0.weapon == base.weapon && b0.identity == base.identity && b0.space == 3 && b0.serial == 11 &&
                  b0.at == 1000 && b0.timeNs == 1000000000 && b0.slot == 1 && b0.barrel == 0 && b0.leftHanded &&
                  b0.ray.position[0] == 42,
            "barrel receipt = base + barrel + ray");
        Check(filled.barrels[1].title == GameTitle::None && !filled.barrels[1].identity, "absent barrel stays empty");
        Palette wrong{};
        calls = 0;
        FillPalette(wrong, markerInner->title, markerInner->identity, markerInner->nodeCount + 1u, base, nodeInner);
        Check(!calls && wrong.barrels[0].title == GameTitle::None, "node-count mismatch never resolves a node");
        Palette refused{};
        FillPalette(refused, markerInner->title, markerInner->identity, markerInner->nodeCount, base,
            [](const Marker&, Ray&) { return false; });
        Check(refused.barrels[0].title == GameTitle::None, "refused node leaves the barrel empty");
    }
    {
        muzzle_refusal::Once once;
        Check(once.ShouldLog(muzzle_refusal::Reason::OwnerMismatch, 0x110B141E), "first refusal for a gun logs");
        Check(!once.ShouldLog(muzzle_refusal::Reason::OwnerMismatch, 0x110B141E),
            "the same refusal for the same gun is logged once");
        Check(once.ShouldLog(muzzle_refusal::Reason::NoRow, 0x110B141E) &&
                  once.ShouldLog(muzzle_refusal::Reason::OwnerMismatch, 0x15011515),
            "another reason or another gun logs separately");
        unsigned logged = 0;
        for (uint32_t k = 0; k < 200; ++k)
            logged += once.ShouldLog(muzzle_refusal::Reason::Guard, k);
        Check(logged <= 64, "the refusal memory is bounded");
        Check(muzzle_refusal::Name(muzzle_refusal::Reason::LensNoRow)[0] == 'n' &&
                  muzzle_refusal::Name(muzzle_refusal::Reason::None)[0] == 'n',
            "every reason has a name");
    }
    std::printf("PASS: %u authored muzzle transform and lifetime checks\n", checks);
}
