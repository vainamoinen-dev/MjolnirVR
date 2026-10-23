// core_halo4_contact_bounds_tests.cpp: Halo 4 anatomical wrists, physical contact, one-hand fallback and weapon bounds.
// Units 9, 10, 62, 230 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_halo4_contact_bounds_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(9);
    {
        Halo4FloatingTransform primary{}, support{}, right{}, left{};
        primary.translation[0] = 2;
        support.translation[0] = -2;
        support.rotation[0] = support.rotation[4] = -1;
        right.translation[0] = 2;
        right.scale = 1.5f;
        left.translation[0] = -2;
        left.scale = 0.5f;
        const auto beforeRight = right, beforeLeft = left;
        Check(Halo4RouteLeftHandedWristTargets(primary, support, right, left) && right.translation[0] == -2 &&
                  left.translation[0] == 2 && right.scale == 0.5f && left.scale == 1.5f && right.rotation[0] == -1 &&
                  left.rotation[0] == -1,
            "H4 anatomical wrist routing keeps role grips/scales and each hand's carrier-relative orientation");
        Check(Halo4RouteLeftHandedWristTargets(support, primary, right, left) &&
                  std::memcmp(&right, &beforeRight, sizeof(right)) == 0 &&
                  std::memcmp(&left, &beforeLeft, sizeof(left)) == 0,
            "Anatomical wrist routing is reversible and introduces no reflected bases");
        support.rotation[0] = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4RouteLeftHandedWristTargets(primary, support, right, left) &&
                  std::memcmp(&right, &beforeRight, sizeof(right)) == 0 &&
                  std::memcmp(&left, &beforeLeft, sizeof(left)) == 0,
            "Invalid anatomical carrier leaves both wrist targets unchanged");
    }

    CheckUnit(10);
    {
        using namespace contact_melee;
        bool mapping = true, timing = true, actorExcluded = true, reset = true;
        for (int hz : {60, 72, 90, 120, 144, 240})
            for (float scale : {0.164f, 0.328f, 0.656f})
                for (bool mirrored : {false, true})
                {
                    Halo4ControllerWorldPoseInput input{};
                    input.worldScale = scale;
                    input.mirrored = mirrored;
                    input.gameYawReference = 0.7f;
                    input.headYawReference = 0.4f;
                    input.bodyOrigin[0] = 30;
                    input.bodyOrigin[1] = -40;
                    input.controllerPosition[0] = 0.2f;
                    input.controllerPosition[1] = 1.2f;
                    input.controllerPosition[2] = -0.3f;
                    const auto prepare = [&](uint64_t serial, Frame& frame, bool valid = true)
                    {
                        return Halo4PreparePhysicalContactFrame(input, valid, serial,
                            1'000'000'000 + int64_t(serial - 1) * 1'000'000'000 / hz, 4, 7, 0x12340001, 17, 1, frame);
                    };
                    Frame frame{};
                    mapping &= prepare(1, frame);
                    Halo4ControllerWorldPose visible{};
                    mapping &= Halo4BuildControllerWorldPose(input, visible);
                    TrackingToWorld reloadTransform{};
                    mapping &= Halo4BuildReloadTrackingTransform(input, reloadTransform);
                    const Point raw{
                        input.controllerPosition[0], input.controllerPosition[1], input.controllerPosition[2]};
                    const auto reloadWorld = reloadTransform.World(raw);
                    const auto restored = reloadTransform.Tracking(reloadWorld);
                    mapping &= std::abs(reloadWorld.x - visible.position[0]) < .00002f &&
                               std::abs(reloadWorld.y - visible.position[1]) < .00002f &&
                               std::abs(reloadWorld.z - visible.position[2]) < .00002f &&
                               std::abs(restored.x - raw.x) < .0001f && std::abs(restored.y - raw.y) < .0001f &&
                               std::abs(restored.z - raw.z) < .0001f;
                    // The reticle ray. A muzzle ray on the visible gun (its
                    // world position, Blam forward/up columns) projected with the XR
                    // transform lands on the controller's XR position and -Z; through the
                    // body-relative contact transform it does not (an earlier bug).
                    if (!mirrored)
                    {
                        const float q[4]{0.1f, 0.25f, -0.05f, 0.96f};
                        float qn[4]{};
                        Halo4ControllerWorldPoseInput turned = input;
                        if (Halo4NormalizeQuaternion(q, qn))
                            std::memcpy(turned.controllerOrientation, qn, sizeof(qn));
                        Halo4ControllerWorldPose gun{};
                        mapping &= Halo4BuildControllerWorldPose(turned, gun);
                        weapon_muzzle::Receipt muzzle{
                            GameTitle::Halo4, 7, 0x120001, 0x250005, 99, 11, 25, 500, 9000000, 0, 0, false, {}};
                        for (int i = 0; i < 3; ++i)
                        {
                            muzzle.ray.position[i] = gun.position[i];
                            muzzle.ray.direction[i] = gun.basis[i];
                            muzzle.ray.up[i] = gun.basis[6 + i];
                        }
                        const float minusZ[3]{0, 0, -1};
                        float xrForward[3]{};
                        Halo4RotateVectorByQuaternion(qn, minusZ, xrForward);
                        dual_reticle::ProjectedRay xr{}, body{};
                        mapping &= dual_reticle::Project(muzzle, reloadTransform, xr) &&
                                   dual_reticle::Project(muzzle, frame.transform, body);
                        float onRay = 0, offRay = 0;
                        for (int i = 0; i < 3; ++i)
                        {
                            onRay = std::max(onRay, std::abs(xr.tracking.direction[i] - xrForward[i]));
                            offRay = std::max(offRay, std::abs(body.tracking.direction[i] - xrForward[i]));
                        }
                        mapping &= onRay < .001f && offRay > .1f &&
                                   std::abs(xr.tracking.position[0] - raw.x) < .0001f &&
                                   std::abs(xr.tracking.position[1] - raw.y) < .0001f &&
                                   std::abs(xr.tracking.position[2] - raw.z) < .0001f;
                    }
                    auto world = frame.transform.World(frame.controllerPose.origin);
                    mapping &= std::abs(world.x - visible.position[0]) < 0.00001f &&
                               std::abs(world.y - visible.position[1]) < 0.00001f &&
                               std::abs(world.z - visible.position[2]) < 0.00001f;
                    const uint64_t epoch = frame.referenceEpoch;
                    frame.count = 1;
                    frame.points[0] = frame.controllerPose.origin;
                    Motion motion;
                    Sweeps sweeps;
                    motion.Advance(frame, 5, sweeps);
                    input.controllerPosition[0] += 6.0f / hz;
                    prepare(2, frame);
                    frame.count = 1;
                    frame.points[0] = frame.controllerPose.origin;
                    motion.Advance(frame, 5, sweeps);
                    timing &= sweeps.count == 1 && std::abs(sweeps.values[0].speedMetresPerSecond - 6) < 0.001f;
                    input.bodyOrigin[0] += 20;
                    input.gameYawReference += 0.9f;
                    prepare(3, frame);
                    frame.count = 1;
                    frame.points[0] = frame.controllerPose.origin;
                    motion.Advance(frame, 5, sweeps);
                    actorExcluded &= !sweeps.count && frame.referenceEpoch == epoch;
                    input.headYawReference += 0.5f;
                    prepare(4, frame);
                    frame.count = 1;
                    frame.points[0] = frame.controllerPose.origin;
                    motion.Advance(frame, 5, sweeps);
                    reset &= !sweeps.count && frame.referenceEpoch != epoch;
                    reset &= !prepare(5, frame, false) && !frame.referenceEpoch;
                    motion.Advance(frame, 5, sweeps);
                    input.controllerPosition[0] += 1;
                    prepare(6, frame);
                    frame.count = 1;
                    frame.points[0] = frame.controllerPose.origin;
                    motion.Advance(frame, 5, sweeps);
                    reset &= !sweeps.count;
                }
        Check(mapping, "H4 physical contact matches its accepted mirrored/unmirrored carrier position");
        Check(timing, "H4 physical swing speed is stable at 60-240 Hz and three world scales");
        Check(actorExcluded, "H4 walking and body turning cannot create physical strikes");
        Check(reset, "H4 recenter and tracking reacquisition discard discontinuous motion");
    }
    CheckUnit(62);
    // The prepared snapshot's owner-safe ordinary one-hand fallback is an
    // immutable per-serial field consumed lock-free from the double buffer. A
    // snapshot that never filled it must never look like a valid one-hand
    // pose, and a copied record must carry it bit-exactly.
    {
        Halo4VrRenderSnapshot fallback{};
        Check(!fallback.oneHandRightAimValid && fallback.oneHandRightAimOrientation[0] == 0.0f &&
                  fallback.oneHandRightAimOrientation[1] == 0.0f && fallback.oneHandRightAimOrientation[2] == 0.0f &&
                  fallback.oneHandRightAimOrientation[3] == 1.0f,
            "an unfilled Halo 4 one-hand fallback is not a usable pose");
        Halo4VrRenderSnapshot filled{};
        filled.oneHandRightAimValid = true;
        filled.oneHandRightAimOrientation[0] = 0.25f;
        filled.oneHandRightAimOrientation[3] = 0.9682458f;
        const Halo4VrRenderSnapshot copy = filled;
        Check(copy.oneHandRightAimValid && copy.oneHandRightAimOrientation[0] == 0.25f &&
                  copy.oneHandRightAimOrientation[3] == 0.9682458f,
            "the Halo 4 prepared one-hand fallback survives the snapshot copy");
    }

    CheckUnit(230);
    // Live-model decoding must work without a catalog checksum and distinguish
    // changed model geometry even if an import checksum was reused by a mod.
    {
        std::array<uint8_t, kHalo4RuntimeCompressionRecordBytes> record{};
        const auto writeFloat = [&](size_t offset, float value)
        { memcpy(record.data() + offset, &value, sizeof(value)); };
        record[0] = 1;
        writeFloat(4, -0.2f);
        writeFloat(8, 0.8f);
        writeFloat(12, -0.1f);
        writeFloat(16, 0.1f);
        writeFloat(20, -0.3f);
        writeFloat(24, 0.2f);
        Halo4WeaponCollisionBounds decoded{};
        Check(Halo4DecodeRuntimeCompressionBounds(record, decoded) && decoded.minimum[0] == -0.2f &&
                  decoded.maximum[2] == 0.2f,
            "H4 live custom weapon bounds decode without a stock checksum");
        decoded.runtimeImportChecksum = 0x1A04081Cu; // absent in reported build's catalog
        const auto identity = Halo4RuntimeWeaponShapeId(42, decoded);
        Check(identity != Halo4RuntimeWeaponShapeId(43, decoded),
            "H4 equipped tag changes reseed collision even for identical bounds");
        decoded.maximum[0] += 0.1f;
        Check(identity != Halo4RuntimeWeaponShapeId(42, decoded),
            "H4 edited model bounds reseed collision with a reused import checksum");
        const auto before = decoded;
        writeFloat(16, NAN);
        Check(!Halo4DecodeRuntimeCompressionBounds(record, decoded) && memcmp(&before, &decoded, sizeof(decoded)) == 0,
            "H4 malformed live bounds refuse atomically");
        Check(!Halo4DecodeRuntimeCompressionBounds(std::span<const uint8_t>(record.data(), record.size() - 1), decoded),
            "H4 live compression requires a complete record");
        record.fill(0);
        record[0] = 5;
        writeFloat(4, 1.0f);
        writeFloat(8, 0.2f);
        writeFloat(12, 0.5f);
        writeFloat(20, -0.2f);
        writeFloat(24, -0.1f);
        writeFloat(28, -0.3f);
        Check(Halo4DecodeRuntimeCompressionBounds(record, decoded) && std::fabs(decoded.maximum[0] - 0.8f) < 1e-6f &&
                  std::fabs(decoded.maximum[2] - 0.2f) < 1e-6f,
            "H4 optimized compression uses extent and minimum XYZ union");
        writeFloat(4, -1.0f);
        Check(
            !Halo4DecodeRuntimeCompressionBounds(record, decoded), "H4 optimized compression rejects negative extent");
        record.fill(0);
        record[0] = 1;
        Check(!Halo4DecodeRuntimeCompressionBounds(record, decoded),
            "H4 zero-volume placeholder is not a weapon collision shape");
        record[0] = 0;
        Check(!Halo4DecodeRuntimeCompressionBounds(record, decoded),
            "H4 uncompressed placeholder does not supply invented bounds");
    }
    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core halo4 contact bounds tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
