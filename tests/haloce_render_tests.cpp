// Tests for the CE Anniversary (Saber) camera logic (src/common/titles/ce/haloce_anniversary_logic.h): staged
// eye cameras, monitor and hull cameras and the scope lens camera. Built as halomccvr_ce_render_tests and run
// by ctest. Header-only logic: no game process, no D3D device.
#include "../src/common/titles/ce/haloce_anniversary_logic.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <fstream>

using namespace halo_ce;
int main(int argc, char** argv)
{
    int failures = 0;
    const auto check = [&](bool ok, const char* what)
    {
        if (!ok)
        {
            std::fprintf(stderr, "CE: %s\n", what);
            ++failures;
        }
    };
    const auto near = [](float a, float b) { return std::fabs(a - b) < 0.00001f; };
    if (argc == 4 && std::strcmp(argv[1], "--native-gameplay-bridge-fixture") == 0)
    {
        std::ifstream source(argv[2], std::ios::binary);
        std::ofstream destination(argv[3], std::ios::binary);
        uint32_t count{};
        if (!source.read(reinterpret_cast<char*>(&count), sizeof(count)) || !count || count > 1000 ||
            !destination.write(reinterpret_cast<const char*>(&count), sizeof(count)))
            return 1;
        for (uint32_t index = 0; index < count; ++index)
        {
            SaberCamera saber{};
            Vec3 offset{};
            float bias{};
            Camera mapped{}, native{};
            if (!source.read(reinterpret_cast<char*>(&saber), sizeof(saber)) ||
                !source.read(reinterpret_cast<char*>(&offset), sizeof(offset)) ||
                !source.read(reinterpret_cast<char*>(&bias), sizeof(bias)) || !NativeCameraFromSaber(saber, mapped) ||
                !RecoverNativeCameraFromSaberBridge(mapped, offset, bias, native) ||
                !destination.write(reinterpret_cast<const char*>(&native), sizeof(native)))
                return 1;
        }
        return 0;
    }
    Camera stock{};
    stock.position = {10, 20, 30};
    stock.forward = {1, 0, 0};
    stock.up = {0, 0, 1};
    stock.viewport = stock.window = {0, 0, 1000, 1000};
    stock.nearPlane = 0.01f;
    stock.farPlane = 1000;
    stock.verticalFov = 1.2f;
    stock.nativeFlags = 0x1234;
    stock.nativePlane[0] = 42;
    Window window{};
    window.player = 0;
    window.render = window.raster = stock;
    check(ValidPrimary(window), "normal primary window admitted");
    window.isUi = 1;
    check(!ValidPrimary(window), "UI window rejected");
    window.isUi = 0;
    window.player = 1;
    check(!ValidPrimary(window), "split-screen secondary rejected");
    window.player = 0;
    ++window.raster.viewport.left;
    check(!ValidPrimary(window), "mismatched camera viewports rejected");
    Tracking tracking{};
    tracking.serial = 7;
    tracking.spaceEpoch = 3;
    tracking.generation = 2;
    tracking.eyes[0].offset = {-0.032f, 0, 0};
    tracking.eyes[1].offset = {0.032f, 0, 0};
    for (Eye& eye : tracking.eyes)
    {
        eye.fov[0] = -0.85f;
        eye.fov[1] = 0.95f;
        eye.fov[2] = 0.8f;
        eye.fov[3] = -0.9f;
    }
    Reference reference{};
    reference.spaceEpoch = 3;
    reference.generation = 2;
    Cover cover{};
    check(BuildCover(tracking, stock.viewport, cover), "asymmetric runtime FOV covered");
    check(near(cover.halfX, 0.95f) && near(cover.halfY, 0.95f), "square raster covers widest eye edge");
    Camera left{}, right{};
    check(BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left) &&
              BuildEye(stock, tracking, reference, 1, 0.33f, true, cover, right),
        "both eyes generated");
    check(near(left.position.y, 20 + 0.032f * 0.33f) && near(right.position.y, 20 - 0.032f * 0.33f),
        "IPD follows CE forward/up basis");
    check(near(left.position.x, 10) && near(left.position.z, 30), "IPD has no forward or vertical drift");
    check(near(left.forward.x, 1) && near(left.up.z, 1), "identity tracking preserves native orientation");
    check(left.nativeFlags == stock.nativeFlags && Same(left.viewport, stock.viewport) &&
              left.nearPlane == stock.nearPlane && left.farPlane == stock.farPlane && left.nativePlane[0] == 42,
        "non-owned camera fields preserved");
    tracking.headPosition = {0, 1, -2};
    check(BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left) && near(left.position.x, 10.66f) &&
              near(left.position.z, 30.33f),
        "metres translate in native axes");
    check(BuildEye(stock, tracking, reference, 0, 0.33f, false, cover, left) && near(left.position.x, 10) &&
              near(left.position.z, 30),
        "positional off preserves binocular separation only");
    tracking.headPosition = {};
    const float s = std::sqrt(0.5f);
    tracking.headOrientation = tracking.eyes[0].orientation = {0, s, 0, s};
    check(BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left) && near(left.forward.x, 0) &&
              near(left.forward.y, 1),
        "90-degree physical head turn has correct sign");
    reference.orientation = tracking.headOrientation;
    check(BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left) && near(left.forward.x, 1) &&
              near(left.forward.y, 0),
        "recenter removes initial head rotation");
    const Camera saved = left;
    ++tracking.generation;
    check(!BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left) &&
              std::memcmp(&left, &saved, sizeof(left)) == 0,
        "previous title generation reference rejected");
    --tracking.generation;
    tracking.spaceEpoch = 4;
    check(!BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left) &&
              std::memcmp(&left, &saved, sizeof(left)) == 0,
        "space reset rejects without partial output");
    tracking.spaceEpoch = 3;
    tracking.eyes[0].offset.x = std::numeric_limits<float>::quiet_NaN();
    check(!BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left), "NaN eye rejected");
    tracking.eyes[0].offset = {0.6f, 0, 0};
    check(!BuildEye(stock, tracking, reference, 0, 0.33f, true, cover, left), "implausible eye offset rejected");
    check(!BuildEye(stock, tracking, reference, 2, 0.33f, true, cover, left), "invalid eye index rejected");
    check(
        !ExactPair(0, 0, 0) && !ExactPair(7, 7, 6) && ExactPair(7, 7, 7), "only complete current serial pair admitted");
    SaberPose saber{};
    check(BuildSaberPose(stock, {100, 200, 300}, 0.25f, saber), "verified CE to Saber bridge pose");
    check(near(saber.matrix[0], 0) && near(saber.matrix[1], 0) && near(saber.matrix[2], 1) &&
              near(saber.matrix[4], 0) && near(saber.matrix[5], 1) && near(saber.matrix[6], 0) &&
              near(saber.matrix[8], 1) && near(saber.matrix[9], 0) && near(saber.matrix[10], 0),
        "Saber axes match CE bridge cross product and signed axis permutation");
    check(near(saber.matrix[12], 130.73f) && near(saber.matrix[13], 291.44f) && near(saber.matrix[14], 239.04f) &&
              saber.matrix[15] == 1,
        "bridge scale, world offset and forward bias applied once");
    const SaberPose savedSaber = saber;
    check(!BuildSaberPose(stock, {0, std::numeric_limits<float>::infinity(), 0}, 0, saber) &&
              std::memcmp(&saber, &savedSaber, sizeof(saber)) == 0,
        "invalid bridge origin rejected without partial pose");
    SaberCamera saberStock{};
    BuildSaberPose(stock, {}, 0, saberStock.pose);
    saberStock.viewportWidth = saberStock.viewportHeight = 1000;
    saberStock.nearPlane = stock.nearPlane * kSaberUnitsPerNativeUnit;
    saberStock.farPlane = stock.farPlane * kSaberUnitsPerNativeUnit;
    saberStock.verticalFovDegrees = stock.verticalFov * 57.29577951308232f;
    saberStock.derived160[100] = 0xab;
    Camera roundTrip{};
    check(NativeCameraFromSaber(saberStock, roundTrip) && near(roundTrip.position.x, stock.position.x) &&
              near(roundTrip.position.y, stock.position.y) && near(roundTrip.position.z, stock.position.z),
        "Saber camera round trip preserves position");
    for (Vec3 offset : {Vec3{}, Vec3{100, 200, 300}})
        for (float bias : {0.0f, .25f, -.5f})
        {
            SaberCamera shifted = saberStock;
            check(BuildSaberPose(stock, offset, bias, shifted.pose), "native bridge fixture builds");
            Camera mapped{}, recovered{};
            check(NativeCameraFromSaber(shifted, mapped) &&
                      RecoverNativeCameraFromSaberBridge(mapped, offset, bias, recovered) &&
                      near(recovered.position.x, stock.position.x) && near(recovered.position.y, stock.position.y) &&
                      near(recovered.position.z, stock.position.z),
                "gameplay recovery removes native bridge offset and bias exactly once");
            mapped.position = recovered.position;
            check(std::memcmp(&mapped, &recovered, sizeof(mapped)) == 0, "gameplay recovery changes only position");
        }
    Camera invalidRecovery = stock;
    check(!RecoverNativeCameraFromSaberBridge(
              stock, {0, 0, std::numeric_limits<float>::infinity()}, 0, invalidRecovery) &&
              std::memcmp(&stock, &invalidRecovery, sizeof(stock)) == 0,
        "invalid bridge offset preserves output");
    tracking.headOrientation = {};
    tracking.eyes[0].orientation = {};
    tracking.eyes[0].offset = {-0.032f, 0, 0};
    reference.orientation = {};
    SaberCamera saberEye{};
    check(StageSaberEye(saberStock, tracking, reference, 0, 0.33f, true, saberEye, cover) &&
              saberEye.derived160[100] == 0xab && saberEye.nearPlane == saberStock.nearPlane &&
              saberEye.farPlane == saberStock.farPlane,
        "staged Anniversary eye preserves every non-owned field");
    {
        // A monitor-frame camera: the hull pose as given with the requested
        // symmetric FOV (kind 1), the head view narrowed (kind 2), nothing else.
        Tracking monitor = tracking;
        SaberCamera m{};
        Cover mc{};
        monitor.scopeTanHalfVertical = 0.2f;
        check(!StageSaberMonitor(saberStock, monitor, reference, 0.33f, true, m, mc), "no monitor kind: refused");
        monitor.monitorFrame = 1;
        monitor.scopeHullPosition = {5, 6, 1};
        monitor.scopeHullForward = {0, 1, 0};
        monitor.scopeHullUp = {0, 0, 1};
        check(StageSaberMonitor(saberStock, monitor, reference, 0.33f, true, m, mc) &&
                  std::fabs(mc.halfY - std::atan(0.2f)) < 1e-5f &&
                  std::fabs(m.verticalFovDegrees - 2 * std::atan(0.2f) * 57.29577951308232f) < 1e-3f &&
                  m.nearPlane == saberStock.nearPlane && m.farPlane == saberStock.farPlane && m.derived160[100] == 0xab,
            "hull monitor camera: requested FOV, stock near/far and non-owned fields kept");
        Camera back{};
        check(NativeCameraFromSaber(m, back) && std::fabs(back.forward.y - 1) < 1e-4f &&
                  std::fabs(back.position.x - 5) < 1e-3f,
            "hull monitor camera: native pose as requested");
        monitor.monitorFrame = 2;
        check(StageSaberMonitor(saberStock, monitor, reference, 0.33f, true, m, mc) &&
                  std::fabs(mc.halfY - std::atan(0.2f)) < 1e-5f,
            "head-zoom monitor camera: narrowed FOV");
        // The gun camera (kind 3) from the lens receipt, even with no scope request
        // and a stale hull request in the sample.
        monitor.monitorFrame = 3;
        monitor.scopeTanHalfVertical = 0.2f;
        monitor.scopeWanted = false;
        monitor.scopeHullCamera = true;
        monitor.scopeReceiptValid = true;
        monitor.scopeReceiptPosition = {7, 8, 2};
        monitor.scopeReceiptForward = {1, 0, 0};
        monitor.scopeReceiptUp = {0, 0, 1};
        monitor.scopeForwardM = 0;
        check(StageSaberMonitor(saberStock, monitor, reference, 0.33f, true, m, mc) &&
                  std::fabs(mc.halfY - std::atan(0.2f)) < 1e-5f && m.nearPlane == saberStock.nearPlane &&
                  m.derived160[100] == 0xab,
            "gun monitor camera: requested FOV, stock near/far and non-owned fields kept");
        check(NativeCameraFromSaber(m, back) && std::fabs(back.forward.x - 1) < 1e-4f &&
                  std::fabs(back.position.x - 7) < 1e-3f && std::fabs(back.position.y - 8) < 1e-3f,
            "gun monitor camera: at the lens receipt, along the gun");
        monitor.scopeReceiptForward = {0, 0, 0};
        Tracking noAim = monitor;
        noAim.controllers.primaryAim.valid = false;
        check(!StageSaberMonitor(saberStock, noAim, reference, 0.33f, true, m, mc),
            "gun monitor camera: no receipt and no aim is refused");
        monitor.scopeTanHalfVertical = 1.5f;
        check(!StageSaberMonitor(saberStock, monitor, reference, 0.33f, true, m, mc),
            "an out-of-range zoom tangent is refused");
    }
    check(near(saberEye.pose.matrix[14], -20 * 3.048f - 0.032f * 0.33f * 3.048f),
        "Saber binocular translation uses independently verified scale and axes");
    {
        // H3 parity: recenter owns yaw/position; physical pitch and roll remain
        // absolute against world up, independent of the stock look pitch.
        const auto nearVector = [](Vec3 a, Vec3 b) { return Dot(a - b, a - b) < 0.000001f; };
        const Quat headYaw{0, std::sin(.35f), 0, std::cos(.35f)};
        for (float nativePitch : {-.8f, 0.0f, .6f})
            for (float referencePitch : {-.55f, 0.0f, .45f})
                for (float referenceRoll : {-.3f, 0.0f, .4f})
                {
                    Camera source = stock;
                    source.forward = {std::cos(nativePitch), 0, std::sin(nativePitch)};
                    source.up = {-std::sin(nativePitch), 0, std::cos(nativePitch)};
                    Tracking sample = tracking;
                    Reference origin = reference;
                    origin.orientation =
                        Multiply(headYaw, Multiply({std::sin(referencePitch / 2), 0, 0, std::cos(referencePitch / 2)},
                                              {0, 0, std::sin(referenceRoll / 2), std::cos(referenceRoll / 2)}));
                    sample.headOrientation = headYaw;
                    sample.headPosition = origin.position + Vec3{0, .25f, 0};
                    for (int eye = 0; eye < 2; ++eye)
                    {
                        sample.eyes[eye].orientation = headYaw;
                        sample.eyes[eye].offset = Rotate(headYaw, {eye ? .032f : -.032f, 0, 0});
                        Camera classic{};
                        check(BuildEye(source, sample, origin, eye, .33f, true, cover, classic) &&
                                  nearVector(classic.forward, {1, 0, 0}) && nearVector(classic.up, {0, 0, 1}) &&
                                  near(classic.position.z, source.position.z + .25f * .33f) &&
                                  near(classic.position.x, source.position.x),
                            "tilted recenter and native pitch cannot tilt the level world or physical height");
                        SaberCamera nativeSaber = saberStock, anniversary{};
                        Camera anniversaryCamera{};
                        Cover anniversaryCover{};
                        check(BuildSaberPose(source, {}, 0, nativeSaber.pose) &&
                                  StageSaberEye(
                                      nativeSaber, sample, origin, eye, .33f, true, anniversary, anniversaryCover) &&
                                  NativeCameraFromSaber(anniversary, anniversaryCamera) &&
                                  nearVector(anniversaryCamera.forward, classic.forward) &&
                                  nearVector(anniversaryCamera.up, classic.up) &&
                                  nearVector(anniversaryCamera.position, classic.position),
                            "Original and Anniversary use the same level recenter frame in both eyes");
                    }
                    // Recenter while still tilted must retain physical tilt,
                    // rather than baking the tilted pose into a neutral room.
                    sample.headOrientation = origin.orientation;
                    sample.eyes[0].orientation = origin.orientation;
                    Camera physical{};
                    const Quat tilt = Multiply({std::sin(referencePitch / 2), 0, 0, std::cos(referencePitch / 2)},
                        {0, 0, std::sin(referenceRoll / 2), std::cos(referenceRoll / 2)});
                    check(BuildEye(source, sample, origin, 0, .33f, false, cover, physical) &&
                              nearVector(physical.forward, ToNative(stock, Rotate(tilt, {0, 0, -1}))) &&
                              nearVector(physical.up, ToNative(stock, Rotate(tilt, {0, 1, 0}))),
                        "recenter preserves current physical pitch and roll instead of zeroing them");
                }
    }
    {
        // The same horizontal frame must also preserve binocular separation
        // through native bank, near-vertical look, and the exact pitch poles.
        constexpr float halfPi = 1.5707963267948966f;
        const auto nearVector = [](Vec3 a, Vec3 b) { return Dot(a - b, a - b) < 0.000001f; };
        for (float nativeYaw : {-2.4f, .7f})
            for (float nativePitch : {-halfPi, -halfPi + .00001f, -.8f, .6f, halfPi - .00001f, halfPi})
                for (float headPitch : {-halfPi, -halfPi + .001f, -.6f, .5f, halfPi - .001f, halfPi})
                {
                    Camera source = stock;
                    const float cy = std::cos(nativeYaw), sy = std::sin(nativeYaw);
                    const float cp = std::cos(nativePitch), sp = std::sin(nativePitch);
                    source.forward = {cp * cy, cp * sy, sp};
                    source.up = {-sp * cy, -sp * sy, cp};
                    Tracking sample = tracking;
                    Reference origin = reference;
                    const Quat yaw{0, std::sin(-.4f), 0, std::cos(-.4f)};
                    origin.orientation = Multiply(yaw, {std::sin(.25f), 0, 0, std::cos(.25f)});
                    sample.headOrientation =
                        Multiply(yaw, Multiply({std::sin(headPitch / 2), 0, 0, std::cos(headPitch / 2)},
                                          {0, 0, std::sin(.2f), std::cos(.2f)}));
                    sample.headPosition = origin.position;
                    Camera eyes[2]{};
                    for (int eye = 0; eye < 2; ++eye)
                    {
                        sample.eyes[eye].orientation = sample.headOrientation;
                        sample.eyes[eye].offset = Rotate(sample.headOrientation, {eye ? .032f : -.032f, 0, 0});
                        check(BuildEye(source, sample, origin, eye, .33f, true, cover, eyes[eye]),
                            "physical and native pitch poles retain a finite tracked eye");
                    }
                    const Vec3 separation = eyes[1].position - eyes[0].position;
                    check(nearVector((eyes[0].position + eyes[1].position) * .5f, source.position) &&
                              std::fabs(std::sqrt(Dot(separation, separation)) - .064f * .33f) < .00001f &&
                              std::fabs(Dot(separation, eyes[0].forward)) < .00001f &&
                              std::fabs(Dot(separation, eyes[0].up)) < .00001f &&
                              nearVector(eyes[0].forward, eyes[1].forward) && nearVector(eyes[0].up, eyes[1].up),
                        "rolled IPD stays symmetric and perpendicular to physical eye orientation at all pitches");
                    if (std::fabs(cp) > .01f)
                    {
                        // Authored bank cannot bank the room or its displacement.
                        source.up = Rotate({source.forward.x * std::sin(.3f), source.forward.y * std::sin(.3f),
                                               source.forward.z * std::sin(.3f), std::cos(.3f)},
                            source.up);
                        Camera banked{};
                        check(BuildEye(source, sample, origin, 0, .33f, true, cover, banked) &&
                                  nearVector(banked.position, eyes[0].position) &&
                                  nearVector(banked.forward, eyes[0].forward) && nearVector(banked.up, eyes[0].up),
                            "native bank does not alter world-locked eye pose");
                    }
                    // At exact vertical HMD reference there is no forward yaw.
                    // Its right-axis fallback must remain finite and q/-q invariant.
                    origin.orientation = sample.headOrientation;
                    Camera pole{};
                    Quat inverse{};
                    check(BuildTrackingFrame(source, origin, pole, inverse) && Valid(pole) && Valid(inverse),
                        "recenter at physical pitch pole has a finite horizontal fallback");
                    origin.orientation = {
                        -origin.orientation.x, -origin.orientation.y, -origin.orientation.z, -origin.orientation.w};
                    Camera negative{};
                    Quat negativeInverse{};
                    check(BuildTrackingFrame(source, origin, negative, negativeInverse) &&
                              nearVector(pole.forward, negative.forward) &&
                              nearVector(Rotate(inverse, {0, 0, -1}), Rotate(negativeInverse, {0, 0, -1})),
                        "equivalent quaternion signs cannot change recentered heading");
                }
        Camera sentinel = stock;
        Quat inverse{.1f, .2f, .3f, .4f};
        const Quat savedInverse = inverse;
        Reference invalid = reference;
        invalid.orientation.w = std::numeric_limits<float>::quiet_NaN();
        check(!BuildTrackingFrame(stock, invalid, sentinel, inverse) &&
                  std::memcmp(&sentinel, &stock, sizeof(stock)) == 0 &&
                  std::memcmp(&inverse, &savedInverse, sizeof(inverse)) == 0,
            "invalid tracking frame leaves both outputs untouched");
    }
    const SaberCamera savedEye = saberEye;
    saberStock.viewportWidth = std::numeric_limits<float>::infinity();
    check(!StageSaberEye(saberStock, tracking, reference, 0, 0.33f, true, saberEye, cover) &&
              std::memcmp(&saberEye, &savedEye, sizeof(saberEye)) == 0,
        "invalid raster rejected before float-to-integer conversion or partial writes");
    {
        // The hull camera (cockpit screen / monitor view) is taken as given,
        // with the requested vertical FOV, and refused when it is not finite.
        Tracking hull{};
        hull.scopeWanted = true;
        hull.scopeHullCamera = true;
        hull.scopeTanHalfVertical = 0.5f;
        hull.scopeHullPosition = {10, 20, 3};
        hull.scopeHullForward = {0, 1, 0};
        hull.scopeHullUp = {0, 0, 1};
        Reference ref{};
        Camera camera{};
        check(BuildScopeCamera(stock, hull, ref, 0.33f, true, camera) && camera.position.y == 20 &&
                  camera.forward.y == 1 && std::fabs(camera.verticalFov - 2.0f * std::atan(0.5f)) < 1e-5f,
            "hull camera: pose and FOV as requested");
        hull.scopeHullForward = {NAN, 0, 0};
        check(!BuildScopeCamera(stock, hull, ref, 0.33f, true, camera), "hull camera: a non-finite pose is refused");
    }
    {
        // A fresh lens receipt places the camera at the gun's lens node plus the
        // clearance along the gun, looking along it; a degenerate one falls back.
        Tracking lens{};
        lens.scopeWanted = true;
        lens.scopeTanHalfVertical = 0.1f;
        lens.scopeForwardM = 0.12f;
        lens.scopeReceiptValid = true;
        lens.scopeReceiptPosition = {5, 6, 7};
        lens.scopeReceiptForward = {0, 2, 0};
        lens.scopeReceiptUp = {0, 0.3f, 1};
        Reference ref{};
        Camera camera{};
        bool fromReceipt = false;
        check(BuildScopeCamera(stock, lens, ref, 0.33f, true, camera, &fromReceipt) && fromReceipt &&
                  std::fabs(camera.position.y - (6 + 0.12f * 0.33f)) < 1e-5f && camera.position.x == 5 &&
                  camera.forward.y == 1 && std::fabs(camera.up.z - 1) < 1e-5f && std::fabs(camera.up.y) < 1e-5f &&
                  std::fabs(camera.verticalFov - 2.0f * std::atan(0.1f)) < 1e-5f,
            "lens receipt: camera at the gun's lens node, along the gun, up made perpendicular");
        lens.scopeReceiptForward = {0, 0, 0};
        fromReceipt = true;
        (void)BuildScopeCamera(stock, lens, ref, 0.33f, true, camera, &fromReceipt);
        check(!fromReceipt, "lens receipt: a zero direction is never used");
    }
    {
        // Classic scope camera (third pass): posed at the aiming hand.
        Tracking scoped{};
        scoped.serial = 7;
        scoped.spaceEpoch = 3;
        scoped.generation = 2;
        Reference ref{};
        ref.spaceEpoch = 3;
        ref.generation = 2;
        Camera camera{};
        check(!BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera), "no scope requested: refused");
        scoped.scopeWanted = true;
        scoped.scopeTanHalfVertical = 0.1f;
        check(!BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera), "no aim pose: refused");
        scoped.controllers.primaryAim.valid = true;
        scoped.controllers.primaryAim.position = {0.2f, -0.3f, -0.4f};
        check(BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera), "scope camera built");
        check(near(camera.verticalFov, 2.0f * std::atan(0.1f)), "zoomed vertical FOV");
        check(near(camera.forward.x, 1) && near(camera.up.z, 1), "identity aim looks along the native heading");
        // XR +x right -> native right (-y for forward +x, up +z); XR -z forward -> native +x.
        check(near(camera.position.x, 10 + 0.4f * 0.33f) && near(camera.position.y, 20 - 0.2f * 0.33f) &&
                  near(camera.position.z, 30 - 0.3f * 0.33f),
            "hand offset in native axes");
        check(camera.nativeFlags == stock.nativeFlags && Same(camera.viewport, stock.viewport) &&
                  camera.nearPlane == stock.nearPlane && camera.farPlane == stock.farPlane,
            "native fields kept");
        scoped.scopeForwardM = 0.5f;
        check(BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera) &&
                  near(camera.position.x, 10 + (0.4f + 0.5f) * 0.33f) && near(camera.position.y, 20 - 0.2f * 0.33f),
            "scope camera sits ahead of the hand along the aim");
        // Camera at the lens: the kit glass offset (aim frame) then the clearance along the aim.
        scoped.scopeLensKnown = true;
        scoped.scopeLensLocal = {0.0f, 0.2f, -0.06f};
        scoped.scopeForwardM = 0.12f;
        check(BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera) &&
                  near(camera.position.x, 10 + (0.4f + 0.06f + 0.12f) * 0.33f) &&
                  near(camera.position.z, 30 + (-0.3f + 0.2f) * 0.33f),
            "lens camera: kit glass offset plus clearance");
        scoped.scopeLensLocal = {NAN, 0, 0};
        check(BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera) &&
                  near(camera.position.x, 10 + (0.4f + 0.12f) * 0.33f),
            "lens camera: a bad offset falls back to the hand");
        scoped.scopeLensKnown = false;
        scoped.scopeLensLocal = {};
        scoped.scopeForwardM = 0.0f;
        const float h = std::sqrt(0.5f);
        scoped.controllers.primaryAim.orientation = {0, h, 0, h}; // 90 degrees left (about +Y)
        check(BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera) && near(camera.forward.x, 0) &&
                  near(camera.forward.y, 1),
            "aim turn has the eye's sign");
        // Visor window variant: the HEAD's view, no aim pose needed.
        scoped.scopeTanHalfVertical = 0.1f;
        scoped.scopeHeadView = true;
        scoped.controllers.primaryAim.valid = false;
        scoped.headOrientation = {0, 0, 0, 1};
        scoped.headPosition = {0.0f, 0.0f, 0.0f};
        check(BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera) && near(camera.forward.x, 1) &&
                  near(camera.position.x, 10) && near(camera.verticalFov, 2.0f * std::atan(0.1f)),
            "head view ignores the hand: looks along the head, at the head");
        scoped.scopeHeadView = false;
        check(!BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera), "hand view still needs the aim pose");
        scoped.controllers.primaryAim.valid = true;
        scoped.scopeTanHalfVertical = 1.5f;
        check(!BuildScopeCamera(stock, scoped, ref, 0.33f, true, camera), "implausible FOV refused");
    }
    return failures ? 1 : 0;
}
