// Reach CHUD (HUD) crosshair checks built only from official HREK evidence: widget layout matching and draw decisions.
// Used by src/dll/core/game.cpp and vr.cpp; tested in tests/core_tests.cpp.
// Pure logic: no Windows calls, no globals, no logging.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Halo: Reach CHUD evidence in this file comes only from the official HREK
// executables and tags. It deliberately contains no MCC retail RVA, image
// layout, or inferred cross-title field.
inline constexpr int8_t kReachChudCrosshairScriptingClass = 2;

// Unique in both official optimized HREK engine variants:
//   reach_tag_play.exe SHA-256
//     450DFFE824DDE4C9866E4448491B8B41D82995DC93159260A4DEF07D059E732E
//     chud_draw_widget RVA 0x56B15C, body size 0x424
//     exact body SHA-256
//     81EBDE1BB1CF9337C01BA861B0CAF70980EBF6871DE079334B5BFB77ABA8978E
//   sapien_play.exe SHA-256
//     1FDA21569B38C189EC88124C1A682DCCED8FBEE11ACFD4D2605F46663B26175B
//     chud_draw_widget RVA 0x8265F4, body size 0x483
//     exact body SHA-256
//     C0EA71FA6BD0D26CA2EBAAED58FA182FE0CD8288274D3459271AD62CA2B9099E
//
// Both functions use the five-argument ABI
//   (output user, descriptor, widget index, alternate path, draw state)
// and read the signed scripting-class byte from descriptor + 4.
// Official full-body call-graph audits also prove neither function recursively
// invokes chud_draw_widget, so the prepared capture cannot be re-entered by the
// stock optimized transaction.
inline constexpr char kReachHrekChudDrawWidgetAob[] = "48 8B C4 44 89 48 20 48 89 50 10 55 56 57 41 54 41 55 "
                                                      "41 56 41 57 48 8D 68 A9 48 81 EC C0 00 00 00";

inline constexpr std::array<uint8_t, 33>
    // clang-format off
    kReachHrekChudDrawWidgetEntryBytes{
        0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10,
        0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0x68, 0xA9, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
// clang-format on

inline constexpr size_t kReachTagPlayChudDrawWidgetBodySize = 0x424;
inline constexpr size_t kReachTagPlayChudDescriptorMoveOffset = 0x45;
inline constexpr size_t kReachTagPlayChudFifthArgumentLoadOffset = 0x4A;
inline constexpr size_t kReachTagPlayChudClassReadOffset = 0x2E1;

inline constexpr size_t kReachSapienPlayChudDrawWidgetBodySize = 0x483;
inline constexpr size_t kReachSapienPlayChudDescriptorMoveOffset = 0x45;
inline constexpr size_t kReachSapienPlayChudFifthArgumentLoadOffset = 0x4B;
inline constexpr size_t kReachSapienPlayChudClassReadOffset = 0x33C;

template <size_t N>
inline bool ReachChudBytesMatchAt(
    std::span<const uint8_t> body, size_t offset, const std::array<uint8_t, N>& expected) noexcept
{
    if (offset > body.size() || expected.size() > body.size() - offset)
        return false;
    for (size_t i = 0; i < expected.size(); ++i)
    {
        if (body[offset + i] != expected[i])
            return false;
    }
    return true;
}

inline bool ReachHrekChudDrawWidgetLayoutMatches(std::span<const uint8_t> body) noexcept
{
    if (!ReachChudBytesMatchAt(body, 0, kReachHrekChudDrawWidgetEntryBytes))
    {
        return false;
    }

    // The rel32 after each class read is intentionally not compared: official
    // link layouts differ. Everything that proves ownership is fixed:
    // RDX (argument 2) is retained in the same nonvolatile register later read
    // at +4, and the fifth argument is loaded from the Windows x64 stack slot.
    if (body.size() == kReachTagPlayChudDrawWidgetBodySize)
    {
        return ReachChudBytesMatchAt(
                   body, kReachTagPlayChudDescriptorMoveOffset, std::array<uint8_t, 3>{0x4C, 0x8B, 0xF2}) &&
               ReachChudBytesMatchAt(
                   body, kReachTagPlayChudFifthArgumentLoadOffset, std::array<uint8_t, 4>{0x4C, 0x8B, 0x4D, 0x7F}) &&
               ReachChudBytesMatchAt(body, kReachTagPlayChudClassReadOffset,
                   std::array<uint8_t, 6>{0x41, 0x0F, 0xBE, 0x56, 0x04, 0xE8}) &&
               ReachChudBytesMatchAt(
                   body, kReachTagPlayChudClassReadOffset + 10, std::array<uint8_t, 3>{0x48, 0x8B, 0xD0});
    }

    if (body.size() == kReachSapienPlayChudDrawWidgetBodySize)
    {
        return ReachChudBytesMatchAt(
                   body, kReachSapienPlayChudDescriptorMoveOffset, std::array<uint8_t, 3>{0x4C, 0x8B, 0xFA}) &&
               ReachChudBytesMatchAt(
                   body, kReachSapienPlayChudFifthArgumentLoadOffset, std::array<uint8_t, 4>{0x4C, 0x8B, 0x4D, 0x7F}) &&
               ReachChudBytesMatchAt(body, kReachSapienPlayChudClassReadOffset,
                   std::array<uint8_t, 6>{0x41, 0x0F, 0xBE, 0x57, 0x04, 0xE8}) &&
               ReachChudBytesMatchAt(
                   body, kReachSapienPlayChudClassReadOffset + 10, std::array<uint8_t, 3>{0x48, 0x8B, 0xD0});
    }

    return false;
}

enum class ReachChudCrosshairAction : uint8_t
{
    DrawStock,
    Suppress,
    CaptureAuthored,
    RejectTransaction,
};

inline ReachChudCrosshairAction ReachDecideChudCrosshairAction(bool ownsStereoTransaction, bool descriptorReadable,
    int8_t scriptingClass, bool crosshairEnabled, bool killNativeReticle, int stereoEye, bool rightEyeFirst) noexcept
{
    if (!ownsStereoTransaction)
        return ReachChudCrosshairAction::DrawStock;
    if (!descriptorReadable)
        return ReachChudCrosshairAction::RejectTransaction;
    if (scriptingClass != kReachChudCrosshairScriptingClass)
        return ReachChudCrosshairAction::DrawStock;
    if (stereoEye < 0 || stereoEye > 1)
        return ReachChudCrosshairAction::RejectTransaction;
    if (!crosshairEnabled)
        return ReachChudCrosshairAction::Suppress;
    if (!killNativeReticle)
        return ReachChudCrosshairAction::DrawStock;

    const int captureEye = rightEyeFirst ? 1 : 0;
    return stereoEye == captureEye ? ReachChudCrosshairAction::CaptureAuthored : ReachChudCrosshairAction::Suppress;
}

// The configured capture eye is allowed to see no class-2 widget at all: Reach
// intentionally omits the crosshair in some authored gameplay states. Once
// either eye does emit class 2, however, the exact stereo pair is incomplete
// until the authored draw has completed on the configured capture eye.
inline bool ReachAuthoredCrosshairPairComplete(
    bool authoredCrosshairRequired, bool class2Seen, bool authoredCaptureCompleted) noexcept
{
    return !authoredCrosshairRequired || !class2Seen || authoredCaptureCompleted;
}

// Reach's world projection and authored crosshair are one compositor
// transaction. A non-Reach title retains the accepted shared composition path;
// Reach may queue its projection only when both eye resolves reached and
// released the XR swapchain, authored upload has not failed, and the exact
// title generation still owns the transaction after those uploads.
inline bool ReachCanSubmitCompleteProjection(
    bool reachTitle, bool stereoUploadComplete, bool authoredUploadFailed, bool liveReachOwnerAfterUpload) noexcept
{
    return !reachTitle || (stereoUploadComplete && !authoredUploadFailed && liveReachOwnerAfterUpload);
}
