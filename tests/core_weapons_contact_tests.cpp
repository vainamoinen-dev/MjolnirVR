// core_weapons_contact_tests.cpp: weapon identity, anatomical palettes, the contact queue, punches, melee, arm IK and finger input.
// Units 2, 8, 12, 13, 14, 15, 17, 19, 20, 226, 227, 228, 244, 252 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_weapons_contact_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(2);
    {
        weapon_profile::IdentityBridge b;
        const bool gap = b.Update(7, 1000, false) == 7 && b.Update(0, 1100, false) == 7 &&
                         b.Update(0, 2000, false) == 7 && b.Update(7, 2010, false) == 7;
        const bool expires = b.Update(0, 3011, false) == 0;
        b.Reset();
        const bool swap = b.Update(7, 0, false) == 7 && b.Update(9, 10, false) == 9;
        const bool hold =
            b.Update(0, 5000, true) == 9 && b.Update(0, 5500, false) == 9 && b.Update(0, 6501, false) == 0;
        Check(gap && expires && swap && hold,
            "weapon identity bridge: zeros under 1 s keep the gun, longer clear it, a new gun replaces it, a pause holds it");
    }
    CheckUnit(8);
    {
        for (float eyeOffset : {-0.032f, 0.032f})
        {
            Halo4FloatingTransform palette[80]{};
            Halo4FloatingTransform root{}, primary{}, support{};
            root.translation[0] = eyeOffset;
            palette[1].translation[0] = 2 - eyeOffset;
            palette[2].translation[0] = -2 - eyeOffset;
            palette[3].translation[0] = 2.1f - eyeOffset;
            palette[4].translation[0] = -2.1f - eyeOffset;
            palette[65].translation[0] = 7;
            const auto weaponBefore = palette[65];
            Check(RouteLeftHandedFloatingPalette(
                      palette, 70, 1, (1ull << 1) | (1ull << 3), 2, (1ull << 2) | (1ull << 4), root, primary, support),
                "Anatomical palette routing supports a source bank with appended weapon records beyond bit 63");
            Check(std::fabs(palette[1].translation[0] + eyeOffset + 2) < 1.0e-5f &&
                      std::fabs(palette[2].translation[0] + eyeOffset - 2) < 1.0e-5f &&
                      std::memcmp(&weaponBefore, &palette[65], sizeof(weaponBefore)) == 0,
                "Anatomical wrists have identical world ownership in both eyes and appended weapon records stay untouched");
            palette[4].translation[0] = std::numeric_limits<float>::quiet_NaN();
            Halo4FloatingTransform before[80];
            std::memcpy(before, palette, sizeof(palette));
            Check(!RouteLeftHandedFloatingPalette(palette, 70, 1, (1ull << 1) | (1ull << 3), 2,
                      (1ull << 2) | (1ull << 4), root, primary, support) &&
                      std::memcmp(before, palette, sizeof(palette)) == 0,
                "An invalid final finger leaves the complete anatomical palette unmodified");
            Check(!RouteLeftHandedFloatingPalette(palette, 70, 1, 6, 2, 6, root, primary, support),
                "Anatomical routing rejects overlapping source hand masks");
        }
    }

    CheckUnit(12);
    {
        ContactMeleeQueue queue;
        ContactMeleePacket packet{}, received{};
        packet.generation = 7;
        packet.frame.unit = 0x81230009;
        packet.frame.count = contact_melee::kMaxPoints;
        bool preserved = true;
        // Fill without a consumer: reject new work rather than overwrite a
        // queued strike. A duplicate eye is harmless even under backpressure.
        for (uint64_t serial = 1; serial <= 8; ++serial)
        {
            packet.frame.serial = serial;
            packet.frame.points[79].x = static_cast<float>(serial);
            preserved &= queue.Push(packet) == 2;
        }
        preserved &= queue.Push(packet) == 1;
        packet.frame.serial = 9;
        preserved &= queue.Push(packet) == 0;
        for (uint64_t serial = 1; serial <= 8; ++serial)
            preserved &= queue.Pop(received) && received.frame.serial == serial &&
                         received.frame.points[79].x == static_cast<float>(serial) &&
                         received.frame.unit == 0x81230009 && received.generation == 7;
        preserved &= !queue.Pop(received);
        // Exercise many ring wraps and re-use after a title epoch retirement.
        for (uint64_t serial = 9; serial <= 1000; ++serial)
        {
            packet.frame.serial = serial;
            preserved &= queue.Push(packet) == 2 && queue.Pop(received) && received.frame.serial == serial;
        }
        queue.Reset();
        packet.frame.serial = 1;
        packet.generation = 8;
        preserved &= queue.Push(packet) == 2 && queue.Pop(received) && received.generation == 8 && !queue.Pop(received);
        Check(preserved,
            "Native contact queue preserves complete frames, rejects overflow, deduplicates eyes, wraps and resets between epochs");
    }

    CheckUnit(13);
    {
        using namespace contact_melee;
        // A small scene independent of the motion implementation: a target
        // plane at x=.05, optionally occluded by a wall at x=.025.
        struct Scene
        {
            bool wall = false, accept = true;
            unsigned applications = 0;
            uint32_t damaged = UINT32_MAX;
            bool Query(const Sweep& sweep, Hit& hit) noexcept
            {
                const float dx = sweep.end.x - sweep.start.x;
                if (std::abs(dx) < 1e-6f)
                    return false;
                const float t = ((wall ? 0.025f : 0.05f) - sweep.start.x) / dx;
                if (t < 0 || t > 1)
                    return false;
                hit = {0xCAFE0002, {sweep.start.x + t * dx, 0, 0}, {-1, 0, 0}, t, !wall};
                return true;
            }
            bool Apply(uint32_t, const Hit& hit, const Sweep&) noexcept
            {
                if (!accept)
                    return false;
                ++applications;
                damaged = hit.unit;
                return true;
            }
        } scene;
        Frame frame;
        frame.referenceEpoch = 1;
        frame.shape = 1;
        frame.unit = 0x12340001;
        frame.serial = 1;
        frame.timeNs = 1'000'000'000;
        frame.count = 2;
        Hand hands[2];
        for (auto& hand : hands)
            hand.Process(frame, 5, scene);
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x = frame.points[1].x = 0.06f;
        for (auto& hand : hands)
            Check(hand.Process(frame, 5, scene) == ContactResult::Applied,
                "both physical hands can hit the actual NPC in the same time interval");
        Check(scene.applications == 2 && scene.damaged == 0xCAFE0002,
            "multiple hand/gun collider hits produce one native damage submission per hand");
        for (auto& hand : hands)
            hand.Process(frame, 5, scene);
        Check(scene.applications == 2, "duplicate eyes cannot repeat a contact damage submission");
        for (unsigned sample = 0; sample < 10; ++sample)
        {
            frame.serial++;
            frame.timeNs += 10'000'000;
            frame.points[0].x += 0.06f;
            frame.points[1].x += 0.06f;
            for (auto& hand : hands)
                hand.Process(frame, 5, scene);
        }
        Check(scene.applications == 2, "a continuing punch does not repeatedly damage the same NPC");
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x = frame.points[1].x = 0;
        for (auto& hand : hands)
            hand.Process(frame, 5, scene);
        Check(scene.applications == 2, "withdrawing a fist rearms without a backwards duplicate hit");
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x = frame.points[1].x = 0.06f;
        hands[0].Process(frame, 5, scene);
        Check(scene.applications == 3, "a retracted hand can deliver the next boxing punch");
        {
            // physical_melee_cooldown_s: a retracted hand still waits the cooldown.
            Check(RearmNs(0.06f) == kMinRearmNs && RearmNs(0.30f) == 300'000'000 &&
                      RearmNs(std::numeric_limits<float>::quiet_NaN()) == kMinRearmNs,
                "melee cooldown seconds map to rearm time");
            const int64_t rearm = RearmNs(0.30f);
            Hand slow;
            Frame f = frame;
            f.serial += 100;
            f.timeNs += 10'000'000;
            f.points[0].x = f.points[1].x = 0;
            slow.Process(f, 5, scene, rearm);
            const auto step = [&](float x)
            {
                f.serial++;
                f.timeNs += 10'000'000;
                f.points[0].x = f.points[1].x = x;
                return slow.Process(f, 5, scene, rearm);
            };
            const unsigned before = scene.applications;
            Check(step(0.06f) == ContactResult::Applied, "cooldown hand strikes");
            step(0);
            step(0.06f);
            Check(scene.applications == before + 1, "a second strike inside the 0.3 s cooldown is ignored");
            for (int i = 0; i < 30; ++i)
                step(0);
            Check(step(0.06f) == ContactResult::Applied && scene.applications == before + 2,
                "after the 0.3 s cooldown a retracted hand strikes again");
        }
        {
            // A tracking hiccup mid-swing never re-arms the strike.
            const int64_t rearm = RearmNs(0.30f);
            Hand hand;
            Frame f = frame;
            f.serial += 500;
            f.timeNs += 10'000'000;
            const auto at = [&](float x, int64_t dtNs)
            {
                f.serial++;
                f.timeNs += dtNs;
                f.points[0].x = f.points[1].x = x;
                return hand.Process(f, 5, scene, rearm);
            };
            at(0, 10'000'000);
            const unsigned before = scene.applications;
            Check(at(0.06f, 10'000'000) == ContactResult::Applied, "A4-07: the first punch lands");
            at(0, 200'000'000); // a 200 ms tracking gap: the swing restarts
            Check(at(0.06f, 10'000'000) == ContactResult::NoStrike && scene.applications == before + 1,
                "A4-07: a gap then the same push inside the 0.3 s cooldown is still one hit (today two)");
            f.referenceEpoch++; // a new tracking space (recenter)
            at(0.06f, 10'000'000);
            Check(at(0.07f, 10'000'000) == ContactResult::NoStrike && scene.applications == before + 1,
                "A4-07: after a space change the hand must still pull back from where it reappeared");
            for (int i = 0; i < 40; ++i)
                at(0.0f, 10'000'000);
            Check(at(0.06f, 10'000'000) == ContactResult::Applied && scene.applications == before + 2,
                "A4-07: a real pull-back after the cooldown strikes again");
        }

        for (int scenario = 0; scenario < 3; ++scenario)
        {
            Hand hand;
            Scene other;
            other.wall = scenario == 0;
            other.accept = scenario != 1;
            frame.serial++;
            frame.timeNs += 10'000'000;
            frame.points[0].x = frame.points[1].x = 0;
            if (scenario == 2)
                frame.unit = 0xCAFE0002;
            hand.Process(frame, 5, other);
            frame.serial++;
            frame.timeNs += 10'000'000;
            frame.points[0].x = frame.points[1].x = 0.06f;
            const auto result = hand.Process(frame, 5, other);
            Check(!other.applications &&
                      result == (scenario == 1 ? ContactResult::NativeRejected : ContactResult::NoStrike),
                "walls, native rejection and self-contact never turn into fallback melee attacks");
        }

        struct WorldScene
        {
            unsigned applications = 0;
            bool Query(const Sweep&, Hit& hit) noexcept
            {
                hit.unit = UINT32_MAX; // BSP surfaces do not have object handles.
                hit.position = {0.05f, 0, 0};
                hit.normal = {-1, 0, 0};
                hit.fraction = 0.5f;
                hit.kind = HitKind::WorldSurface;
                hit.world.nativeType = 1;
                hit.world.nativeData[0] = 0x120034u;
                hit.world.nativeData[1] = 0x000A0011u;
                hit.world.material = 2;
                hit.world.valid = true;
                return true;
            }
            bool Apply(uint32_t, const Hit& hit, const Sweep&) noexcept
            {
                if (hit.unit != UINT32_MAX || hit.object || hit.kind != HitKind::WorldSurface || !hit.world.valid)
                    return false;
                ++applications;
                return true;
            }
        } world;
        Hand worldHand;
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x = frame.points[1].x = 0;
        worldHand.Process(frame, 5, world);
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x = frame.points[1].x = 0.06f;
        Check(worldHand.Process(frame, 5, world) == ContactResult::Applied && world.applications == 1,
            "explicit native world-surface identity can reach backend without a manufactured object handle");
        Hit unverifiedWorld{};
        unverifiedWorld.kind = HitKind::WorldSurface;
        unverifiedWorld.world.nativeType = 1;
        unverifiedWorld.world.valid = false;
        Check(!IsCandidate(unverifiedWorld, frame.unit),
            "world contacts without a verified surface identity are rejected");
        unverifiedWorld.world.nativeType = 2;
        unverifiedWorld.world.valid = true;
        Check(IsCandidate(unverifiedWorld, frame.unit),
            "shared world-contact contract leaves native type semantics to each title adapter");
        Hit surfaceA{};
        surfaceA.kind = HitKind::WorldSurface;
        surfaceA.position = {0.05f, 0, 0};
        surfaceA.normal = {-1, 0, 0};
        surfaceA.fraction = 0.5f;
        surfaceA.world.nativeType = 2;
        surfaceA.world.nativeData[0] = 0x10203040;
        surfaceA.world.material = 7;
        surfaceA.world.valid = true;
        Hit surfaceB = surfaceA;
        surfaceB.position.x += 0.001f;
        Check(SameWorldSurface(surfaceA, surfaceB),
            "world revalidation accepts the same native surface within query precision");
        surfaceB.world.nativeData[0]++;
        Check(!SameWorldSurface(surfaceA, surfaceB),
            "world revalidation rejects a different BSP/feature identity even at the same point");
    }
    CheckUnit(14);
    {
        using namespace contact_melee;
        Frame frame;
        frame.referenceEpoch = 1;
        frame.shape = 1;
        frame.unit = 0x12340001;
        frame.serial = 1;
        frame.timeNs = 1'000'000'000;
        frame.count = 2;
        frame.rigidMotion = true;
        frame.points[1] = {0.5f, 0, 0};
        Motion motion;
        Sweeps sweeps;
        motion.Advance(frame, 5, sweeps);
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0] = {0.1f, 0, 0};
        frame.points[1] = {0, 0.5f, 0};
        motion.Advance(frame, 5, sweeps);
        Check(!sweeps.count, "reload and finger animation cannot become a punch with stationary controllers");
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.controllerPose.origin.x = 0.06f;
        frame.points[0].x += 0.06f;
        frame.points[1].x += 0.06f;
        motion.Advance(frame, 5, sweeps);
        Check(sweeps.count == 2 && std::abs(sweeps.values[0].speedMetresPerSecond - 6) < 0.001f,
            "physical controller translation still sweeps animated hand and weapon geometry");
        frame.serial++;
        frame.timeNs += 10'000'000;
        constexpr float angle = 0.12f;
        const float q[4] = {0, 0, std::sin(angle / 2), std::cos(angle / 2)};
        const float p[3] = {0.06f, 0, 0};
        Check(frame.controllerPose.SetPose(q, p), "contact controller pose accepts normalized OpenXR rotation");
        frame.points[0] = frame.controllerPose.World({0.1f, 0, 0});
        frame.points[1] = frame.controllerPose.World({0, 0.5f, 0});
        motion.Advance(frame, 5, sweeps);
        Check(sweeps.count == 1 && sweeps.values[0].pointIndex == 1 && sweeps.values[0].speedMetresPerSecond > 5.9f,
            "rigid controller rotation qualifies a long weapon tip without wrist translation");
        const float badQ[4] = {0, 0, 0, 0};
        Check(!frame.controllerPose.SetPose(badQ, p), "zero controller quaternion is rejected");
    }
    CheckUnit(15);
    for (int engine = 0; engine < 4; ++engine)
    {
        using namespace contact_melee;
        struct WeaponOnlyTarget
        {
            unsigned applied = 0, point = 0;
            bool Query(const Sweep& sweep, Hit& hit) noexcept
            {
                // Target is beyond the wrist and only the rotating barrel
                // crosses it. No hand-point contact can satisfy this scene.
                const float dy = sweep.end.y - sweep.start.y;
                if (dy <= 0 || sweep.start.y > 0.04f || sweep.end.y < 0.04f)
                    return false;
                const float t = (0.04f - sweep.start.y) / dy;
                const float x = sweep.start.x + t * (sweep.end.x - sweep.start.x);
                if (x < 0.5f)
                    return false;
                hit = {0xCAFE0002, {x, 0.04f, 0}, {0, -1, 0}, t, true};
                return true;
            }
            bool Apply(uint32_t owner, const Hit& hit, const Sweep& sweep) noexcept
            {
                if (owner != 0x12340001 || hit.unit != 0xCAFE0002)
                    return false;
                ++applied;
                point = sweep.pointIndex;
                return true;
            }
        } target;
        Halo4WeaponCollisionBounds bounds{0x1A04081Cu, {-0.2f, -0.01f, -0.01f}, {0.8f, 0.01f, 0.01f}};
        if (engine)
        {
            const auto layout = engine == 1   ? LegacyBoundsLayout::Halo3
                                : engine == 2 ? LegacyBoundsLayout::Odst
                                              : LegacyBoundsLayout::Reach;
            uint8_t record[52]{};
            record[0] = 1;
            for (int axis = 0; axis < 3; ++axis)
            {
                memcpy(record + 4 + axis * 8, &bounds.minimum[axis], sizeof(float));
                memcpy(record + 8 + axis * 8, &bounds.maximum[axis], sizeof(float));
            }
            Check(
                LegacyDecodeCompressionBounds(layout, std::span(record, LegacyCompressionRecordBytes(layout)), bounds),
                "each legacy engine decodes an unfamiliar weapon for physical melee");
            bounds.runtimeImportChecksum = 0xDEADBEEFu;
        }
        const auto shape = [&]()
        {
            return engine ? LegacyRuntimeWeaponShapeId(42, 37, bounds)
                          : uint64_t(Halo4RuntimeWeaponShapeId(42, bounds));
        };
        const float basis[]{1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
        const float zero[3]{};
        float points[kHalo4WeaponCollisionBoundsSampleCount][3]{};
        Check(Halo4BuildWeaponCollisionBoundsSamples(
                  bounds, 1, basis, zero, zero, zero, points, kHalo4WeaponCollisionBoundsSampleCount) == 14,
            "unknown weapon contributes the same bounds to world contact and melee");
        Frame frame{};
        frame.referenceEpoch = 1;
        frame.unit = 0x12340001;
        frame.shape = shape();
        frame.serial = 1;
        frame.timeNs = 1'000'000'000;
        frame.count = 7 + 14;
        frame.rigidMotion = true;
        for (unsigned i = 0; i < 14; ++i)
            frame.points[7 + i] = {points[i][0], points[i][1], points[i][2]};
        Hand hand;
        hand.Process(frame, 5, target);
        const float angle = 0.12f;
        const float rotation[]{0.f, 0.f, std::sin(angle / 2), std::cos(angle / 2)};
        frame.controllerPose.SetPose(rotation, zero);
        for (unsigned i = 0; i < 14; ++i)
            frame.points[7 + i] = frame.controllerPose.World({points[i][0], points[i][1], points[i][2]});
        ++frame.serial;
        frame.timeNs += 10'000'000;
        Check(hand.Process(frame, 5, target) == ContactResult::Applied && target.applied == 1 && target.point >= 7,
            "rotating unknown weapon deals physical contact damage with stationary hand samples");
        Check(hand.Process(frame, 5, target) == ContactResult::NoStrike && target.applied == 1,
            "second eye does not repeat weapon-only melee damage");
        bounds.maximum[0] = 1.0f;
        frame.shape = shape();
        ++frame.serial;
        frame.timeNs += 10'000'000;
        Check(hand.Process(frame, 5, target) == ContactResult::NoStrike && target.applied == 1,
            "weapon geometry change reseeds melee without manufacturing an impact");
    }
    CheckUnit(17);
    // Analytic trajectories: a 6 m/s fist crosses a thin target even when no
    // sampled endpoint is inside it. Test physical rates and engine scales.
    for (const int hz : {60, 72, 80, 90, 120, 144, 165, 240})
        for (const float scale : {0.25f, 1.0f, 3.0f})
        {
            using namespace contact_melee;
            Motion right, left;
            Frame frame;
            frame.referenceEpoch = 1;
            frame.shape = 1;
            frame.unit = 0x12340001;
            frame.count = 1;
            frame.transform.unitsPerMetre = scale;
            Sweeps sweeps;
            bool crossed = false;
            constexpr float targetX = 0.317f;
            for (int sample = 0; sample <= hz / 5; ++sample)
            {
                frame.timeNs = 1'000'000'000 + int64_t(std::llround(double(sample) * 1e9 / hz));
                frame.serial = uint64_t(sample) + 1;
                frame.points[0].x = float(double(frame.timeNs - 1'000'000'000) * 6e-9);
                const auto result = right.Advance(frame, 5, sweeps);
                if (sample)
                {
                    Check(result == AdvanceResult::Advanced && sweeps.count == 1 &&
                              std::abs(sweeps.values[0].speedMetresPerSecond - 6) < 0.001f,
                        "physical fist speed is independent of refresh rate and world scale");
                    const auto& sweep = sweeps.values[0];
                    crossed |= sweep.start.x <= targetX * scale && sweep.end.x >= targetX * scale;
                }
                Check(right.Advance(frame, 5, sweeps) == AdvanceResult::Duplicate && !sweeps.count,
                    "the second eye cannot produce another melee sweep");
                frame.points[0].x = -frame.points[0].x;
                left.Advance(frame, 5, sweeps);
                Check(!sample || (sweeps.count == 1 && sweeps.values[0].end.x < sweeps.values[0].start.x),
                    "the other hand independently sweeps in the opposite direction");
            }
            Check(crossed, "continuous hand sweep crosses an NPC between sampled endpoints");
        }
    CheckUnit(19);
    {
        using namespace contact_melee;
        Frame frame;
        frame.referenceEpoch = 1;
        frame.shape = 1;
        frame.unit = 0x12340001;
        frame.count = 2;
        frame.serial = 1;
        frame.timeNs = 1'000'000'000;
        frame.points[1] = {0.5f, 0, 0}; // Long gun tip, wrist at the origin.
        Motion motion;
        Sweeps sweeps;
        motion.Advance(frame, 5, sweeps);
        frame.serial++;
        frame.timeNs += 10'000'000;
        const float angle = 0.12f; // 12 rad/s: a stationary wrist, 6 m/s tip.
        frame.points[1] = {0.5f * std::cos(angle), 0.5f * std::sin(angle), 0};
        motion.Advance(frame, 5, sweeps);
        Check(sweeps.count == 1 && sweeps.values[0].pointIndex == 1 && sweeps.values[0].speedMetresPerSecond > 5.9f,
            "rotating the gun can qualify its tip even when wrist translation is zero");
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.transform.origin = {200, 300, 400};
        frame.transform.axis[0] = {0, 1, 0};
        frame.transform.axis[1] = {-1, 0, 0};
        motion.Advance(frame, 5, sweeps);
        Check(!sweeps.count, "game locomotion and turning do not generate physical melee");
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x += 0.06f;
        frame.points[1].x += 0.06f;
        motion.Advance(frame, 5, sweeps);
        Check(sweeps.count == 2 && std::abs(sweeps.values[0].start.x - 200) < 0.001f &&
                  std::abs(sweeps.values[0].start.y - 300) < 0.001f &&
                  std::abs(sweeps.values[0].end.y - 300.06f) < 0.001f,
            "a real punch while moving sweeps in the current world frame only");
        const auto roundTrip = frame.transform.Tracking(frame.transform.World({0.3f, 0.4f, 0.5f}));
        Check(std::abs(roundTrip.x - 0.3f) < 0.0001f && std::abs(roundTrip.y - 0.4f) < 0.0001f &&
                  std::abs(roundTrip.z - 0.5f) < 0.0001f,
            "authored world points recover tracking-space coordinates");
        for (int discontinuity = 0; discontinuity < 5; ++discontinuity)
        {
            frame.serial++;
            frame.timeNs += 10'000'000;
            frame.points[0].x += 0.2f;
            if (discontinuity == 0)
                ++frame.referenceEpoch;
            if (discontinuity == 1)
                ++frame.shape;
            if (discontinuity == 2)
                frame.unit = 0x43210001; // Same index, new salt.
            if (discontinuity == 3)
                frame.timeNs += kMaxSampleGapNs;
            if (discontinuity == 4)
                frame.count = 1;
            Check(motion.Advance(frame, 5, sweeps) == AdvanceResult::Seeded && !sweeps.count,
                "recenter, weapon swap, respawn, tracking gap and collider changes reseed without damage");
        }
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[0].x = std::numeric_limits<float>::quiet_NaN();
        Check(motion.Advance(frame, 5, sweeps) == AdvanceResult::Rejected && !sweeps.count,
            "invalid tracking cannot leave a queued melee sweep");
        frame.points[0] = {};
        Check(motion.Advance(frame, 5, sweeps) == AdvanceResult::Seeded && !sweeps.count,
            "tracking reacquisition cannot hit from an old pose");
        frame.serial++;
        frame.timeNs -= 1;
        Check(motion.Advance(frame, 5, sweeps) == AdvanceResult::Rejected && !sweeps.count,
            "backward tracking timestamps cannot produce velocity");
        frame.transform.axis[0] = {2, 0, 0};
        Check(motion.Advance(frame, 5, sweeps) == AdvanceResult::Rejected,
            "a malformed tracking transform cannot amplify physical motion");
    }
    CheckUnit(20);
    Check(!ShouldApplyArmIk(false, false), "Arm IK remains off when disabled in config");
    Check(!ShouldApplyArmIk(false, true), "Two-hand state cannot enable disabled arm IK");
    Check(ShouldApplyArmIk(true, false), "Configured arm IK returns outside a two-hand grab");
    Check(!ShouldApplyArmIk(true, true), "Two-hand grab suppresses competing arm IK");
    Check(ReachShouldBindVisibleLeftHandToController(false) && !ReachShouldBindVisibleLeftHandToController(true),
        "Reach binds the visible glove to the left controller only in free-hand mode and keeps the authored weapon grip during two-hand aim");
    Check(ReachMayShowControllerTrackedArms(true, true, false, true, true, true) &&
              !ReachMayShowControllerTrackedArms(false, true, false, true, true, true) &&
              !ReachMayShowControllerTrackedArms(true, false, false, true, true, true) &&
              !ReachMayShowControllerTrackedArms(true, true, false, false, true, true) &&
              !ReachMayShowControllerTrackedArms(true, true, false, true, false, true) &&
              !ReachMayShowControllerTrackedArms(true, true, false, true, true, false) &&
              ReachMayShowControllerTrackedArms(true, true, true, true, false, true) &&
              !ReachMayShowControllerTrackedArms(true, true, true, true, false, false),
        "Reach exposes arms after complete solve, using the derived authored support endpoint when attached");
    struct ReachPose2D
    {
        float angle = 0.0f, x = 0.0f, y = 0.0f, scale = 1.0f;
    };
    const auto composeReachPose = [](const ReachPose2D& a, const ReachPose2D& b, ReachPose2D& out)
    {
        const float c = std::cos(a.angle), s = std::sin(a.angle);
        out.angle = a.angle + b.angle;
        out.x = a.x + a.scale * (c * b.x - s * b.y);
        out.y = a.y + a.scale * (s * b.x + c * b.y);
        out.scale = a.scale * b.scale;
        return std::isfinite(out.angle) && std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.scale);
    };
    const auto invertReachPose = [&](const ReachPose2D& value, ReachPose2D& out)
    {
        if (!std::isfinite(value.scale) || std::fabs(value.scale) < 1.0e-5f)
            return false;
        out.angle = -value.angle;
        out.scale = 1.0f / value.scale;
        const float c = std::cos(out.angle), s = std::sin(out.angle);
        out.x = -out.scale * (c * value.x - s * value.y);
        out.y = -out.scale * (s * value.x + c * value.y);
        return std::isfinite(out.x) && std::isfinite(out.y);
    };
    const ReachPose2D reachRoot{0.6f, 2.0f, -1.0f, 1.2f};
    const ReachPose2D reachStockRight{0.2f, 0.3f, 0.1f, 1.0f};
    const ReachPose2D reachDesiredRight{-0.4f, 4.0f, 3.0f, 1.0f};
    const ReachPose2D reachStockSupport{0.5f, -0.2f, 0.8f, 1.0f};
    ReachPose2D reachSupportEndpoint{};
    const bool reachEndpointBuilt = ReachBuildRigidCarriedSupportEndpoint(reachRoot, reachStockRight, reachDesiredRight,
        reachStockSupport, reachSupportEndpoint, composeReachPose, invertReachPose);
    ReachPose2D reachStockRightWorld{}, reachInverseRight{}, reachDelta{};
    ReachPose2D reachStockSupportWorld{}, reachExpectedSupport{};
    const bool reachExpectedBuilt = composeReachPose(reachRoot, reachStockRight, reachStockRightWorld) &&
                                    invertReachPose(reachStockRightWorld, reachInverseRight) &&
                                    composeReachPose(reachDesiredRight, reachInverseRight, reachDelta) &&
                                    composeReachPose(reachRoot, reachStockSupport, reachStockSupportWorld) &&
                                    composeReachPose(reachDelta, reachStockSupportWorld, reachExpectedSupport);
    Check(reachEndpointBuilt && reachExpectedBuilt &&
              std::fabs(reachSupportEndpoint.x - reachExpectedSupport.x) < 1.0e-5f &&
              std::fabs(reachSupportEndpoint.y - reachExpectedSupport.y) < 1.0e-5f &&
              std::fabs(reachSupportEndpoint.angle - reachExpectedSupport.angle) < 1.0e-5f,
        "Reach two-hand support wrist endpoint follows the exact right-hand/gun rigid delta");
    ReachPose2D refusedEndpoint{};
    Check(!ReachBuildRigidCarriedSupportEndpoint(ReachPose2D{}, ReachPose2D{0, 0, 0, 0}, reachDesiredRight,
              reachStockSupport, refusedEndpoint, composeReachPose, invertReachPose),
        "Reach refuses support-arm transaction when source wrist transform cannot be inverted");
    Check(
        ReachFpPaletteVisibility(false, false, true, true, false) == ReachFpVisibilityAction::Keep &&
            ReachFpPaletteVisibility(false, false, true, false, true) == ReachFpVisibilityAction::Keep &&
            ReachFpPaletteVisibility(false, false, false, true, false) ==
                ReachFpVisibilityAction::CollapseAtLeftWrist &&
            ReachFpPaletteVisibility(false, false, false, false, true) ==
                ReachFpVisibilityAction::CollapseAtRightWrist &&
            ReachFpPaletteVisibility(false, false, true, false, false) == ReachFpVisibilityAction::HideUnrelatedBody &&
            ReachFpPaletteVisibility(true, false, false, false, false) == ReachFpVisibilityAction::Keep &&
            ReachFpPaletteVisibility(false, true, false, false, false) == ReachFpVisibilityAction::Keep,
        "Reach visibility keeps both controller-solved arms and hands/held models, while hiding unrelated first-person body nodes");
    CheckUnit(226);
    // A brief real peak between input polls survives equally at 72/90/120/144 Hz.
    for (int hz : {72, 90, 120, 144})
    {
        PhysicalMeleeSpeedHistory history;
        float speed = 0;
        history.Update(1000, true, 6.0f);
        for (uint64_t at = 1000 + 1000 / hz; at <= 1040; at += 1000 / hz)
            history.Update(at, true, 1.0f);
        Check(history.Read(1040, speed) && speed == 6.0f,
            "melee peak survives intervening slower tracking samples without frame-count timing");
        Check(!history.Read(1200, speed), "melee peak expires without new valid tracking");
        history.Update(1201, true, 6.0f);
        history.Update(1202, false, 0);
        Check(!history.Read(1203, speed), "tracking loss clears melee history instead of replaying a swing");
    }
    CheckUnit(227);
    ContactReleaseSmoothing contactSmoothing;
    float wallCorrection[3]{0.020f, 0, 0};
    contactSmoothing.Apply(1000, true, 1.0f, wallCorrection);
    Check(wallCorrection[0] == 0.020f, "first contact blocks immediately");
    wallCorrection[0] = 0.015f;
    contactSmoothing.Apply(1033, true, 1.0f, wallCorrection);
    Check(wallCorrection[0] > 0.015f && wallCorrection[0] <= 0.025f && wallCorrection[1] == 0 && wallCorrection[2] == 0,
        "continuing contact smooths small outward release without weakening native blocking or adding sideways drift");
    wallCorrection[0] = 0.040f;
    contactSmoothing.Apply(1066, true, 1.0f, wallCorrection);
    Check(wallCorrection[0] == 0.040f, "deeper contact is not delayed by smoothing");
    float cornerCorrection[3]{0, 0.01f, 0};
    contactSmoothing.Apply(1099, true, 1.0f, cornerCorrection);
    Check(cornerCorrection[0] == 0 && cornerCorrection[1] == 0.01f,
        "a different contact direction discards the previous surface correction");
    float clearCorrection[3]{};
    Check(contactSmoothing.Apply(1132, false, 1.0f, clearCorrection) && clearCorrection[0] == 0 &&
              clearCorrection[1] > 0 && clearCorrection[1] < 0.01f,
        "a clear query releases bounded presentation clearance without sideways drift");
    clearCorrection[1] = 0;
    Check(!contactSmoothing.Apply(1220, false, 1.0f, clearCorrection) && contactSmoothing.atMs == 0,
        "a clear surface cannot retain presentation clearance past 120 ms");
    CheckUnit(228);
    // The same release step must decay by elapsed time, independently of the
    // render/query cadence and the title's world-units-per-metre conversion.
    for (int hz : {60, 72, 80, 90, 120, 144, 165, 240})
        for (float worldScale : {0.25f, 1.0f, 3.0f})
        {
            ContactReleaseSmoothing smoothing;
            float correction[3]{0.020f * worldScale, 0, 0};
            smoothing.Apply(1000, true, worldScale, correction);
            for (uint64_t sample = 1;; ++sample)
            {
                const uint64_t elapsed = std::min<uint64_t>(100, sample * 1000 / hz);
                correction[0] = 0.015f * worldScale;
                smoothing.Apply(1000 + elapsed, true, worldScale, correction);
                if (elapsed == 100)
                    break;
            }
            const float expected = 0.015f + 0.005f * std::exp(-100.0f / 40.0f);
            Check(std::fabs(correction[0] / worldScale - expected) < 1e-6f,
                "contact release has the same physical decay across cadence and world scale");
            correction[0] = 0.001f * worldScale;
            smoothing.Apply(1101, true, worldScale, correction);
            Check(correction[0] / worldScale <= 0.011001f,
                "contact smoothing never retains more than one centimetre of extra clearance");
            correction[0] = 0.0005f * worldScale;
            smoothing.Apply(1300, true, worldScale, correction);
            Check(std::fabs(correction[0] / worldScale - 0.0005f) < 1e-7f,
                "a tracking or scheduling gap discards stale contact smoothing");

            smoothing.Reset();
            correction[0] = 0.04f * worldScale;
            smoothing.Apply(2000, true, worldScale, correction);
            for (uint64_t sample = 1;; ++sample)
            {
                const uint64_t elapsed = std::min<uint64_t>(80, sample * 1000 / hz);
                correction[0] = 0;
                Check(smoothing.Apply(2000 + elapsed, false, worldScale, correction),
                    "clear-query presentation release remains bounded and time-based");
                if (elapsed == 80)
                    break;
            }
            Check(std::fabs(correction[0] / worldScale - 0.01f * std::exp(-2.0f)) < 1e-6f,
                "final contact release is cadence-invariant and caps its seed at one centimetre");
            correction[0] = 0;
            Check(!smoothing.Apply(2081, false, worldScale * 2, correction),
                "world-scale changes cannot retain clearance measured in the old units");
        }
    CheckUnit(244);
    {
        Halo2ObserverPosePublication firing{};
        firing.generation = 7;
        firing.serial = 100;
        firing.snapshot.valid = firing.snapshot.rightAimValid = firing.snapshot.leftControllerValid = true;
        firing.snapshot.trackingSpaceEpoch = 3;
        firing.snapshot.predictedDisplayTimeNs = 1000000000;
        Check(Halo2DualAimPublicationFresh(firing, 7, 3, 1011111111) &&
                  Halo2DualAimPublicationFresh(firing, 7, 3, 988888889),
            "dual aim accepts adjacent-frame publication ordering without mixing poses");
        Check(!Halo2DualAimPublicationFresh(firing, 8, 3, 1011111111) &&
                  !Halo2DualAimPublicationFresh(firing, 7, 4, 1011111111) &&
                  !Halo2DualAimPublicationFresh(firing, 7, 3, 1100000001) &&
                  !Halo2DualAimPublicationFresh(firing, 7, 3, 0),
            "dual aim rejects old title, recenter/handedness epoch, stale or missing tracking time");
    }

    CheckUnit(252);
    {
        ControllerFingerSample sample{9, 123, 7, false,
            {MakeControllerFingerInput(true, .25f, true, .75f), MakeControllerFingerInput(true, .8f, true, .1f)}};
        ControllerFingerInput hands[2]{};
        Check(SelectControllerFingerInputs(sample, 9, 123, 7, false, true, hands) && hands[0].trigger == .25f &&
                  hands[1].trigger == .8f,
            "finger inputs preserve physical support/primary values");
        sample.leftHanded = true;
        Check(SelectControllerFingerInputs(sample, 9, 123, 7, true, true, hands) && hands[0].grip == .1f &&
                  hands[1].grip == .75f,
            "left-handed finger inputs follow the same roles as frozen hand poses");
        for (unsigned stale = 0; stale < 5; ++stale)
            Check(!SelectControllerFingerInputs(sample, stale == 4 ? 10 : 9, stale == 0 ? 124 : 123, stale == 1 ? 8 : 7,
                      stale != 2, stale != 3, hands) &&
                      !hands[0].valid && !hands[1].valid,
                "stale time, space, handedness or tracking cannot replay finger input");
        Check(!MakeControllerFingerInput(false, .5f, true, .5f).valid &&
                  !MakeControllerFingerInput(true, .5f, false, .5f).valid &&
                  !MakeControllerFingerInput(true, std::numeric_limits<float>::quiet_NaN(), true, .5f).valid &&
                  !MakeControllerFingerInput(true, .5f, true, 2.f).valid,
            "missing or malformed finger axes retain authored poses");
        const auto bounded = MakeControllerFingerInput(true, -.005f, true, 1.005f);
        Check(
            bounded.valid && bounded.trigger == 0 && bounded.grip == 1, "small analog endpoint noise remains bounded");
    }
    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core weapons contact tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
