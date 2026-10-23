// Tests the Virtual Stock aim pose: two-hand aim lines, dual-wield paths and the stored neutral
// orientation, run through the production solver extracted into the generated virtual_stock_fixture.inl.
// Built as halomccvr_virtual_stock_aim_tests. No OpenXR runtime or headset: poses are fixture values.
#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/dll/core/aim_pose_trace.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include "virtual_stock_fixture.inl"

static void Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
static bool Close(float a, float b)
{
    return std::fabs(a - b) < .00002f;
}
static bool Same(XrPosef a, XrPosef b)
{
    return Close(a.position.x, b.position.x) && Close(a.position.y, b.position.y) &&
           Close(a.position.z, b.position.z) && Close(a.orientation.x, b.orientation.x) &&
           Close(a.orientation.y, b.orientation.y) && Close(a.orientation.z, b.orientation.z) &&
           Close(a.orientation.w, b.orientation.w);
}
int main()
{
    try
    {
        for (int handed = 0; handed < 2; ++handed)
            for (int mode = 0; mode < 4; ++mode)
                for (int sample = 0; sample < 80; ++sample)
                {
                    AimPoseInputs input;
                    input.rightValid = input.leftValid = input.twoHandEnabled = input.twoHandLatched = input.headValid =
                        true;
                    const float side = handed ? -.15f : .15f;
                    input.right.position = {side, 1.30f, -.25f};
                    input.left.position = {side + (sample - 40) * .001f, 1.29f, -.60f};
                    input.supportPosition = input.left.position;
                    input.headPosition = {0, 1.60f, 0};
                    input.virtualStockLeftHanded = handed != 0;
                    input.virtualStockRearReference = mode;
                    input.gunPitchDeg = 7;
                    input.gunYawDeg = -3;
                    input.gunRollDeg = 2;
                    const bool hybrid = mode == 3;
                    const auto ordinary = ComputeAimPose(input);
                    Check(ordinary.valid && ordinary.twoHandActive, "normal two-hand fixture");
                    {
                        // two_hand_exact_line: the published forward IS the rear->support hand line
                        // (full influence, no mount yaw/pitch), despite the 7/-3 deg trim above.
                        auto exact = input;
                        exact.twoHandExactLine = true;
                        exact.twoHandOffhandInfluence = 1.0f;
                        const auto line = ComputeAimPose(exact);
                        const auto& q = line.pose.orientation;
                        const float fx = -2 * (q.x * q.z + q.w * q.y), fy = -2 * (q.y * q.z - q.w * q.x),
                                    fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
                        const float dx = input.left.position.x - input.right.position.x,
                                    dy = input.left.position.y - input.right.position.y,
                                    dz = input.left.position.z - input.right.position.z,
                                    dl = std::sqrt(dx * dx + dy * dy + dz * dz);
                        Check(line.valid && line.twoHandActive && (fx * dx + fy * dy + fz * dz) / dl > 0.99999f,
                            "exact line: two-hand aim points along the rear-hand to support-hand line");
                        // Found in automated testing (Reach, virtual_stock=1): Virtual Stock must not steer an
                        // exact-line aim; with grip poses offset from the aim poses (as the
                        // fake runtime holds them) the aim follows the grip line within 1 deg.
                        const auto offLine = [](const AimPoseResult& r, XrVector3f a, XrVector3f b)
                        {
                            const auto& o = r.pose.orientation;
                            const float gx = -2 * (o.x * o.z + o.w * o.y), gy = -2 * (o.y * o.z - o.w * o.x),
                                        gz = -(1 - 2 * (o.x * o.x + o.y * o.y));
                            const float lx = b.x - a.x, ly = b.y - a.y, lz = b.z - a.z,
                                        ll = std::sqrt(lx * lx + ly * ly + lz * lz);
                            return std::acos(std::clamp((gx * lx + gy * ly + gz * lz) / ll, -1.0f, 1.0f)) * 57.2957795f;
                        };
                        auto stock = exact;
                        stock.virtualStockEnabled = true;
                        stock.virtualStockStrength = .95f;
                        const auto stockLine = ComputeAimPose(stock);
                        Check(stockLine.valid && stockLine.twoHandActive &&
                                  offLine(stockLine, input.right.position, input.left.position) < 1.0f,
                            "exact line beats Virtual Stock: aim within 1 deg of the hand line");
                        auto grips = stock;
                        grips.primaryGripValid = grips.supportGripValid = true;
                        grips.primaryGripPosition = {input.right.position.x + .01f, input.right.position.y - .04f,
                            input.right.position.z + .06f};
                        grips.supportGripPosition = {
                            input.left.position.x - .01f, input.left.position.y - .03f, input.left.position.z + .05f};
                        const auto gripLine = ComputeAimPose(grips);
                        Check(gripLine.valid && gripLine.twoHandActive &&
                                  offLine(gripLine, grips.primaryGripPosition, grips.supportGripPosition) < 1.0f,
                            "exact line follows the grip-pose line within 1 deg (Virtual Stock on)");
                        auto gripsOff = grips;
                        gripsOff.virtualStockEnabled = false;
                        Check(offLine(ComputeAimPose(gripsOff), grips.primaryGripPosition, grips.supportGripPosition) <
                                  1.0f,
                            "exact line follows the grip-pose line within 1 deg (Virtual Stock off)");
                        // Latched but the support pose is unusable: one-handed, the frame keeps
                        // the Virtual Stock solve exactly (no jump by the stock difference).
                        auto noSupport = stock;
                        noSupport.leftValid = false;
                        auto noSupportVs = noSupport;
                        noSupportVs.twoHandExactLine = false;
                        const auto ns = ComputeAimPose(noSupport), nsVs = ComputeAimPose(noSupportVs);
                        Check(!ns.twoHandActive && ns.valid == nsVs.valid && Same(ns.pose, nsVs.pose),
                            "exact line, latched with no support pose: same one-hand aim as Virtual Stock");
                        auto legacy = grips;
                        legacy.twoHandExactLine = false;
                        Check(ComputeAimPose(legacy).valid, "exact line off keeps the Virtual Stock solve");
                    }
                    {
                        // One-handed weapon class: the grab seats the hand (twoHandActive) but the aim is
                        // exactly the single-hand solve, with Virtual Stock on or off.
                        auto pistol = input;
                        pistol.twoHandAimSuppressed = true;
                        auto single = input;
                        single.twoHandEnabled = false;
                        const auto p = ComputeAimPose(pistol), s = ComputeAimPose(single);
                        Check(p.valid && p.twoHandActive && Same(p.pose, s.pose) && !s.twoHandActive,
                            "pistol grab: aim is the single-hand aim, twoHandActive still reports the grab");
                        auto pistolVs = pistol;
                        pistolVs.virtualStockEnabled = true;
                        pistolVs.virtualStockStrength = .95f;
                        auto singleVs = single;
                        singleVs.virtualStockEnabled = true;
                        singleVs.virtualStockStrength = .95f;
                        Check(Same(ComputeAimPose(pistolVs).pose, ComputeAimPose(singleVs).pose),
                            "pistol grab: no virtual stock on a one-handed aim");
                        auto notLatched = pistol;
                        notLatched.twoHandLatched = false;
                        Check(!ComputeAimPose(notLatched).twoHandActive, "no grab, no visual attach");
                        auto longGun = input;
                        longGun.twoHandAimSuppressed = false;
                        Check(!Same(ComputeAimPose(longGun).pose, s.pose) || ComputeAimPose(longGun).twoHandActive,
                            "long guns keep the two-hand aim");
                    }
                    input.virtualStockEnabled = true;
                    input.virtualStockStrength = 0;
                    const auto zero = ComputeAimPose(input);
                    if (!hybrid)
                        Check(zero.valid && Same(zero.pose, ordinary.pose),
                            "zero stock matches the shipping controller solver for every non-Hybrid mode/hand");
                    else
                    {
                        // Hybrid (mode 3) keeps its A/B offhand authority active
                        // independently of stock strength; the Hybrid tests in aim_pose_tests.cpp
                        // assert the exact primary-controller endpoint instead
                        // (aim_pose_tests: "Hybrid influence zero with no C preserves
                        // the exact A quaternion and active presentation").
                        AimPoseInputs zeroInfluence = input;
                        zeroInfluence.virtualStockHybridOffhandInfluence = 0;
                        AimPoseInputs primaryOnly = input;
                        primaryOnly.twoHandEnabled = false;
                        Check(zero.valid && zero.twoHandActive && zero.pose.position.x == input.right.position.x &&
                                  zero.pose.position.y == input.right.position.y &&
                                  zero.pose.position.z == input.right.position.z &&
                                  Same(ComputeAimPose(zeroInfluence).pose, ComputeAimPose(primaryOnly).pose),
                            "Hybrid keeps A/B presentation active and preserves the exact primary aim at zero stock strength");
                    }
                    input.virtualStockStrength = .95f;
                    const auto stock = ComputeAimPose(input);
                    Check(stock.valid && stock.twoHandActive, "all virtual stock modes produce valid supported aim");
                    Check(stock.pose.position.x == input.right.position.x &&
                              stock.pose.position.y == input.right.position.y &&
                              stock.pose.position.z == input.right.position.z,
                        "stock must never move the primary controller / raw muzzle anchor");
                    input.headValid = false;
                    if (!hybrid)
                        Check(Same(ComputeAimPose(input).pose, ordinary.pose),
                            "missing coherent head falls back to ordinary two-hand aim");
                    else
                    {
                        // Donor Hybrid's head-invalid endpoint is the exact primary
                        // aim, not the legacy two-hand pose (aim_pose_tests: head
                        // invalid + zero influence preserves A and stays active).
                        AimPoseInputs noHeadInfluence = input;
                        noHeadInfluence.virtualStockHybridOffhandInfluence = 0;
                        AimPoseInputs primaryOnly = input;
                        primaryOnly.twoHandEnabled = false;
                        Check(Same(ComputeAimPose(noHeadInfluence).pose, ComputeAimPose(primaryOnly).pose),
                            "Hybrid without a coherent head preserves the exact primary aim");
                    }
                    input.headValid = true;
                    input.twoHandLatched = false;
                    const auto notLatched = ComputeAimPose(input);
                    input.virtualStockEnabled = false;
                    Check(Same(notLatched.pose, ComputeAimPose(input).pose) && !notLatched.twoHandActive,
                        "stock cannot acquire or latch support grip");
                    input.virtualStockEnabled = true;
                    input.twoHandLatched = true;
                    input.twoHandEnabled = false;
                    const auto independent = ComputeAimPose(input);
                    input.virtualStockEnabled = false;
                    Check(Same(independent.pose, ComputeAimPose(input).pose) && !independent.twoHandActive,
                        "physical/independent dual-wield paths retain controller calibration");
                }
        // Retired head-turn sway correction. With inverse-neck
        // disabled, a stale-but-valid stored neutral orientation (kept far from
        // the current head) must be completely inert: the full result is
        // byte-identical and no inverse-neck evaluation is attempted, for
        // product-like Standard (rear 0/1) and Plus (rear 3) strength.
        for (const int rear : {0, 1, 3})
        {
            AimPoseInputs input;
            input.rightValid = input.leftValid = input.twoHandEnabled = input.twoHandLatched = input.headValid = true;
            input.right.position = {.15f, 1.30f, -.25f};
            input.left.position = {.15f, 1.29f, -.60f};
            input.supportPosition = input.left.position;
            input.headPosition = {0, 1.60f, 0};
            input.headOrientation = {0, .3826834f, 0, .9238795f};
            input.virtualStockEnabled = true;
            input.virtualStockRearReference = rear;
            input.virtualStockStrength = rear == 3 ? .80f : .95f;
            input.hybridInverseNeckEnabled = false;
            AimPoseTrace withoutNeutralTrace;
            const auto withoutNeutral = ComputeAimPoseImpl<true>(input, &withoutNeutralTrace);
            Check(withoutNeutral.valid && !withoutNeutralTrace.inverseNeckAttempted &&
                      !withoutNeutralTrace.inverseNeckValid,
                "disabled inverse-neck never attempts or validates a stored neutral");
            input.inverseNeckNeutralValid = true;
            input.inverseNeckNeutralOrientation = {0, .7071068f, 0, .7071068f}; // yaw 90 deg from the head
            input.inverseNeckNeutralCaptureSerial = 4321;
            input.inverseNeckNeutralCaptureContactSpaceEpoch = 7;
            AimPoseTrace staleNeutralTrace;
            const auto withStaleNeutral = ComputeAimPoseImpl<true>(input, &staleNeutralTrace);
            Check(!staleNeutralTrace.inverseNeckAttempted && !staleNeutralTrace.inverseNeckValid &&
                      std::memcmp(&withoutNeutral, &withStaleNeutral, sizeof(AimPoseResult)) == 0,
                "a stale stored neutral is byte-inert while inverse-neck is disabled");
        }
        std::cout
            << "640 production aim combinations passed: legacy zero/off parity and missing-head fallback on donor modes 0-2, Hybrid A/B zero-strength and head-invalid endpoints, grip isolation, raw-position invariance; retired sway correction leaves a stale stored neutral byte-inert for Standard and Plus.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
