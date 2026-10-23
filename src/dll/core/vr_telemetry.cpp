// vr_telemetry.cpp: the pacing capture and the prepared-frame and aim telemetry, moved unchanged from vr.cpp. What it provides to the other VR units is
// declared in vr_telemetry.h; it reaches their state through vr_internal.h.
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <wincodec.h>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <limits>
#include <type_traits>
#include "menu.h"
#include "../titles/h2/halo2_observer_6dof.h"
#include <../common/authored_reticle_logic.h>
#include "../../common/frame_pacing_logic.h"
#include "vr_internal.h"
#include "vr_frame.h"
#include "vr_compose.h"
#include "vr_controllers.h"
#include "vr_scope.h"
#include "vr_telemetry.h"
#include "vr_format_helpers.h"
#include "vr_inline_ops.h"
#include "../titles/reach/reach_frame_bridge.h"
using namespace reach_frame_bridge;
#include "../titles/h4/halo4_frame_bridge.h"
using namespace halo4_frame_bridge;
#include "../titles/ce/haloce_frame_bridge.h"
using namespace haloce_frame_bridge;
#include "../titles/h2/halo2_frame_bridge.h"
using namespace halo2_frame_bridge;
using namespace vr_core;
#include "../titles/reach/reach_display_worker.h"
using namespace reach_bridge;

namespace
{
    double PacingQpcMs(int64_t end, int64_t start, int64_t frequency)
    {
        if (!end || !start || !frequency)
            return -1.0;
        return static_cast<double>(end - start) * 1000.0 / static_cast<double>(frequency);
    }
} // namespace
namespace vr_core
{
    void LogPacingCapture(const FramePacingWorkerState& worker, const char* completion)
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const uint64_t dropsNow = g_framePacingQueueDrops.load(std::memory_order_relaxed);
        const uint64_t dropsDuring = dropsNow - worker.dropsAtStart;
        const bool exact = worker.exact && dropsDuring == 0;
        const double oldHz = worker.oldPeriodNs > 0 ? 1000000000.0 / static_cast<double>(worker.oldPeriodNs) : 0.0;
        const double newHz = worker.newPeriodNs > 0 ? 1000000000.0 / static_cast<double>(worker.newPeriodNs) : 0.0;
        const size_t postCount =
            worker.captureCount > worker.triggerIndex ? worker.captureCount - worker.triggerIndex - 1 : 0;

        std::string text;
        text.reserve(worker.captureCount * 640 + 2048);
        char line[1536]{};
        snprintf(line, sizeof(line),
            "PACING TRANSITION CAPTURE: %lldns (%.3fHz) -> %lldns "
            "(%.3fHz), triggerSerial=%llu, pre=%zu post=%zu, "
            "completion=%s exact=%d queueDrops=%llu panel=%.3fHz\n",
            static_cast<long long>(worker.oldPeriodNs), oldHz, static_cast<long long>(worker.newPeriodNs), newHz,
            static_cast<unsigned long long>(worker.capture[worker.triggerIndex].serial), worker.triggerIndex, postCount,
            completion, exact ? 1 : 0, static_cast<unsigned long long>(dropsDuring),
            static_cast<double>(g_panelRefreshHz.load(std::memory_order_relaxed)));
        text.append(line);
        text.append("PACING F fields: rel serial epoch edge periodNs appHz predDeltaMs "
                    "beginStepMs presentStepMs waitSeq source conflictSeq dispatchSeq "
                    "dispatchSeen coherent workerWaitMs readyAgeMs eventWaitMs "
                    "dispatchAckMs "
                    "beginMs pairExact resumeMs "
                    "waitStartVsPrevBeginMs waitEndVsPrevBeginMs prepareMs "
                    "gameMs submitBuildMs renderWindowMs endMs preDxgiMs dxgiMs "
                    "afterDxgiMs flags(S/F/T/H/C) layers title xr(wait/begin/end) "
                    "eventWait/nextWait dxgiHr\n");
        text.append("PACING C fields: serial views requests[e0/e1/out] "
                    "fullSolves[e0/e1/out] hits[e0/e1/out] stores[e0/e1/out] "
                    "cacheFull[e0/e1/out] hotLogs[zoom/view/palette/camera/fpDriver]\n");
        text.append("PACING C semantics: views=Halo3 outer stereo transactions; "
                    "fullSolves=successful non-explicit arm-IK misses; "
                    "out=eye -1 (includes scope and other outside-eye work)\n");

        for (size_t i = 0; i < worker.captureCount; ++i)
        {
            const FramePacingRecord& record = worker.capture[i];
            const FramePacingRecord* previous = i ? &worker.capture[i - 1] : nullptr;
            const bool contiguous =
                previous && previous->sessionEpoch == record.sessionEpoch && previous->serial + 1 == record.serial;
            const bool edge =
                contiguous && IsMaterialFramePeriodTransition(static_cast<uint64_t>(previous->predictedDisplayPeriodNs),
                                  static_cast<uint64_t>(record.predictedDisplayPeriodNs));
            const double predictedDeltaMs =
                contiguous
                    ? static_cast<double>(record.predictedDisplayTimeNs - previous->predictedDisplayTimeNs) / 1000000.0
                    : -1.0;
            const double beginStepMs =
                contiguous ? PacingQpcMs(record.beginEndQpc, previous->beginEndQpc, frequency.QuadPart) : -1.0;
            const double presentStepMs =
                contiguous ? PacingQpcMs(record.presentStartQpc, previous->presentStartQpc, frequency.QuadPart) : -1.0;
            const bool exactPair = contiguous && IsExactWaitPair(*previous, record);
            const double resumeMs =
                exactPair ? PacingQpcMs(record.waitCallStartQpc, previous->consumedSignalQpc, frequency.QuadPart)
                          : -1.0;
            const double waitStartVsPreviousBeginMs =
                exactPair ? PacingQpcMs(record.waitCallStartQpc, previous->beginStartQpc, frequency.QuadPart) : -1.0;
            const double waitEndVsPreviousBeginMs =
                exactPair ? PacingQpcMs(record.waitCallEndQpc, previous->beginEndQpc, frequency.QuadPart) : -1.0;
            const long long relative = static_cast<long long>(i) - static_cast<long long>(worker.triggerIndex);
            const double appHz = record.predictedDisplayPeriodNs > 0
                                     ? 1000000000.0 / static_cast<double>(record.predictedDisplayPeriodNs)
                                     : 0.0;

            snprintf(line, sizeof(line),
                "PACING F rel=%+lld serial=%llu epoch=%u edge=%d "
                "periodNs=%lld appHz=%.3f predDeltaMs=%.3f "
                "beginStepMs=%.3f presentStepMs=%.3f waitSeq=%llu "
                "source=W conflictSeq=%llu dispatchSeq=%llu dispatchSeen=%d "
                "coherent=%d workerWaitMs=%.3f readyAgeMs=%.3f "
                "eventWaitMs=%.3f dispatchAckMs=%.3f beginMs=%.3f "
                "pairExact=%d resumeMs=%.3f waitStartVsPrevBeginMs=%.3f "
                "waitEndVsPrevBeginMs=%.3f prepareMs=%.3f gameMs=%.3f "
                "submitBuildMs=%.3f renderWindowMs=%.3f endMs=%.3f "
                "preDxgiMs=%.3f dxgiMs=%.3f afterDxgiMs=%.3f "
                "flags=%d/%d/%d/%d/%d layers=%u title=%u "
                "xr=%d/%d/%d event=%lu/%lu dxgi=0x%08X\n",
                relative, static_cast<unsigned long long>(record.serial), record.sessionEpoch, edge ? 1 : 0,
                static_cast<long long>(record.predictedDisplayPeriodNs), appHz, predictedDeltaMs, beginStepMs,
                presentStepMs, static_cast<unsigned long long>(record.waitSequence),
                static_cast<unsigned long long>(record.overlappingWorkerWaitSequence),
                static_cast<unsigned long long>(record.nextWaitDispatchSequence),
                record.nextWaitDispatchObservedBeforeBegin ? 1 : 0, record.waitPacketCoherent ? 1 : 0,
                PacingQpcMs(record.waitCallEndQpc, record.waitCallStartQpc, frequency.QuadPart),
                PacingQpcMs(record.renderWaitStartQpc, record.waitReadyQpc, frequency.QuadPart),
                PacingQpcMs(record.renderWaitEndQpc, record.renderWaitStartQpc, frequency.QuadPart),
                PacingQpcMs(record.nextWaitDispatchGateEndQpc, record.nextWaitDispatchGateStartQpc, frequency.QuadPart),
                PacingQpcMs(record.beginEndQpc, record.beginStartQpc, frequency.QuadPart), exactPair ? 1 : 0, resumeMs,
                waitStartVsPreviousBeginMs, waitEndVsPreviousBeginMs,
                PacingQpcMs(record.prepareEndQpc, record.beginEndQpc, frequency.QuadPart),
                PacingQpcMs(record.beforePresentQpc, record.prepareEndQpc, frequency.QuadPart),
                PacingQpcMs(record.endStartQpc, record.beforePresentQpc, frequency.QuadPart),
                PacingQpcMs(record.endStartQpc, record.beginEndQpc, frequency.QuadPart),
                PacingQpcMs(record.endEndQpc, record.endStartQpc, frequency.QuadPart),
                PacingQpcMs(record.presentStartQpc, record.endEndQpc, frequency.QuadPart),
                PacingQpcMs(record.presentEndQpc, record.presentStartQpc, frequency.QuadPart),
                PacingQpcMs(record.afterPresentQpc, record.presentEndQpc, frequency.QuadPart),
                record.shouldRender ? 1 : 0, record.focused ? 1 : 0, record.stereo ? 1 : 0, record.headTracking ? 1 : 0,
                record.scopeActive ? 1 : 0, record.layerCount, static_cast<unsigned>(record.title), record.waitResult,
                record.beginResult, record.endResult, static_cast<unsigned long>(record.eventWaitResult),
                static_cast<unsigned long>(record.nextWaitEventResult), static_cast<unsigned>(record.presentResult));
            text.append(line);

            const auto& perf = record.perf;
            snprintf(line, sizeof(line),
                "PACING C serial=%llu views=%llu requests=%llu/%llu/%llu "
                "fullSolves=%llu/%llu/%llu hits=%llu/%llu/%llu "
                "stores=%llu/%llu/%llu cacheFull=%llu/%llu/%llu "
                "hotLogs=%llu/%llu/%llu/%llu/%llu\n",
                static_cast<unsigned long long>(record.serial), static_cast<unsigned long long>(perf.viewRenders),
                static_cast<unsigned long long>(perf.fpPaletteRequests[0]),
                static_cast<unsigned long long>(perf.fpPaletteRequests[1]),
                static_cast<unsigned long long>(perf.fpPaletteRequests[2]),
                static_cast<unsigned long long>(perf.fpPaletteFullSolves[0]),
                static_cast<unsigned long long>(perf.fpPaletteFullSolves[1]),
                static_cast<unsigned long long>(perf.fpPaletteFullSolves[2]),
                static_cast<unsigned long long>(perf.fpPaletteCacheHits[0]),
                static_cast<unsigned long long>(perf.fpPaletteCacheHits[1]),
                static_cast<unsigned long long>(perf.fpPaletteCacheHits[2]),
                static_cast<unsigned long long>(perf.fpPaletteCacheStores[0]),
                static_cast<unsigned long long>(perf.fpPaletteCacheStores[1]),
                static_cast<unsigned long long>(perf.fpPaletteCacheStores[2]),
                static_cast<unsigned long long>(perf.fpPaletteCacheFull[0]),
                static_cast<unsigned long long>(perf.fpPaletteCacheFull[1]),
                static_cast<unsigned long long>(perf.fpPaletteCacheFull[2]),
                static_cast<unsigned long long>(perf.zoomLogWrites),
                static_cast<unsigned long long>(perf.viewRateLogWrites),
                static_cast<unsigned long long>(perf.paletteRateLogWrites),
                static_cast<unsigned long long>(perf.cameraRateLogWrites),
                static_cast<unsigned long long>(perf.fpDriverRateLogWrites));
            text.append(line);
        }
        LOG("%s", text.c_str());
    }
    // SteamVR fronts every streamed headset, so "runtime: SteamVR/OpenXR" with a
    // system name of "oculus" is what Meta Link (wired or Air) and ALVR BOTH
    // report -- two different pipelines, different failure modes, and nothing in
    // the log to tell them apart. On 2026-07-28 that cost a real finding: a
    // session that spawned upside down could not be attributed to either, and
    // the log had already rotated away before it could be re-run.
    //
    // The streaming host is its own process, so name the ones that are running.
    // Deliberately "present", not "in use": Meta's OVRServer runs whenever the
    // Meta software is installed and started, even while you stream over
    // something else. One name here plus the headset line settles a session;
    // several means ask which was used. Every name below was read off a live
    // process or install on the development machine -- none are guessed, and a
    // streamer with no distinct process of its own (Steam Link streams from
    // Steam itself) is deliberately absent rather than matched on a guess.
    void LogStreamingHosts()
    {
        struct StreamingHost
        {
            const wchar_t* exe;
            const char* name;
        };
        static constexpr StreamingHost kHosts[] = {
            {L"ALVR Dashboard.exe", "ALVR"},
            {L"VirtualDesktop.Streamer.exe", "Virtual Desktop"},
            {L"OVRServer_x64.exe", "Meta Link / Air Link (Oculus runtime)"},
        };
        constexpr size_t kHostCount = sizeof(kHosts) / sizeof(kHosts[0]);

        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            LOG("streaming host: could not enumerate processes (%lu), so this "
                "session cannot be told apart from another on the same runtime",
                static_cast<unsigned long>(GetLastError()));
            return;
        }
        bool found[kHostCount] = {};
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry))
        {
            do
            {
                for (size_t i = 0; i < kHostCount; ++i)
                    if (_wcsicmp(entry.szExeFile, kHosts[i].exe) == 0)
                        found[i] = true;
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);

        char list[256] = {};
        for (size_t i = 0; i < kHostCount; ++i)
        {
            if (!found[i])
                continue;
            if (list[0])
                strcat_s(list, ", ");
            strcat_s(list, kHosts[i].name);
        }
        if (list[0])
            LOG("streaming host present: %s", list);
        else
            LOG("streaming host present: none recognised (a tethered headset, "
                "or a streamer with no process of its own)");
    }
} // namespace vr_core
namespace
{
    // Cross-thread Lab diagnostics for the UI. Same seqlock shape as
    // AimContinuityPublication: the frame thread publishes, any thread reads
    // without a lock. Tranche 2B publishes the full Lab observation for every
    // Lab-applicable frame (including one-hand solves) plus the temporal
    // presentation state.
    struct TwoHandLabDiagnosticsPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> serial{0};
        std::atomic<uint8_t> valid{0};
        std::atomic<uint8_t> labEnabledStored{0};
        std::atomic<uint8_t> labActiveThisFrame{0};
        std::atomic<uint8_t> requestedAnchor{0};
        std::atomic<uint8_t> resolvedAnchor{0};
        std::atomic<uint8_t> fallback{0};
        std::atomic<uint8_t> primaryGripValid{0};
        std::atomic<uint8_t> supportGripValid{0};
        std::atomic<uint8_t> bValid{0};
        std::atomic<float> agreement{0.0f};
        std::atomic<uint8_t> agreementMode{0};
        std::atomic<float> confidence{0.0f};
        std::atomic<float> requestedInfluence{0.0f};
        std::atomic<float> effectiveInfluence{0.0f};
        std::atomic<uint8_t> temporalMode{0};
        std::atomic<uint8_t> temporalActive{0};
        std::atomic<float> errorDeg{0.0f};
        // Selected pivot positions (additive): the pair the live
        // selection consumed. Meaningful only while pivotsValid is set.
        std::atomic<uint8_t> pivotsValid{0};
        std::atomic<float> primaryPivotX{0.0f};
        std::atomic<float> primaryPivotY{0.0f};
        std::atomic<float> primaryPivotZ{0.0f};
        std::atomic<float> supportPivotX{0.0f};
        std::atomic<float> supportPivotY{0.0f};
        std::atomic<float> supportPivotZ{0.0f};
    };
    constinit TwoHandLabDiagnosticsPublication g_twoHandLabDiagnostics;
} // namespace
namespace vr_core
{
    void PublishTwoHandLabDiagnostics(
        uint64_t serial, bool valid, const two_hand_lab::Diagnostics& diagnostics) noexcept
    {
        auto& published = g_twoHandLabDiagnostics;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.serial.store(serial, std::memory_order_relaxed);
        published.valid.store(valid ? 1u : 0u, std::memory_order_relaxed);
        published.labEnabledStored.store(diagnostics.labEnabledStored ? 1u : 0u, std::memory_order_relaxed);
        published.labActiveThisFrame.store(diagnostics.labActiveThisFrame ? 1u : 0u, std::memory_order_relaxed);
        published.requestedAnchor.store(static_cast<uint8_t>(diagnostics.requestedAnchor), std::memory_order_relaxed);
        published.resolvedAnchor.store(static_cast<uint8_t>(diagnostics.resolvedAnchor), std::memory_order_relaxed);
        published.fallback.store(static_cast<uint8_t>(diagnostics.fallback), std::memory_order_relaxed);
        published.primaryGripValid.store(diagnostics.primaryGripValid ? 1u : 0u, std::memory_order_relaxed);
        published.supportGripValid.store(diagnostics.supportGripValid ? 1u : 0u, std::memory_order_relaxed);
        published.bValid.store(diagnostics.bValid ? 1u : 0u, std::memory_order_relaxed);
        published.agreement.store(diagnostics.agreement, std::memory_order_relaxed);
        published.agreementMode.store(static_cast<uint8_t>(diagnostics.agreementMode), std::memory_order_relaxed);
        published.confidence.store(diagnostics.confidence, std::memory_order_relaxed);
        published.requestedInfluence.store(diagnostics.requestedInfluence, std::memory_order_relaxed);
        published.effectiveInfluence.store(diagnostics.effectiveInfluence, std::memory_order_relaxed);
        published.temporalMode.store(static_cast<uint8_t>(diagnostics.temporalMode), std::memory_order_relaxed);
        published.temporalActive.store(diagnostics.temporalActive ? 1u : 0u, std::memory_order_relaxed);
        published.errorDeg.store(diagnostics.errorDeg, std::memory_order_relaxed);
        published.pivotsValid.store(diagnostics.pivotsValid ? 1u : 0u, std::memory_order_relaxed);
        published.primaryPivotX.store(diagnostics.primaryPivot.x, std::memory_order_relaxed);
        published.primaryPivotY.store(diagnostics.primaryPivot.y, std::memory_order_relaxed);
        published.primaryPivotZ.store(diagnostics.primaryPivot.z, std::memory_order_relaxed);
        published.supportPivotX.store(diagnostics.supportPivot.x, std::memory_order_relaxed);
        published.supportPivotY.store(diagnostics.supportPivot.y, std::memory_order_relaxed);
        published.supportPivotZ.store(diagnostics.supportPivot.z, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace vr_core
namespace
{
    bool ReadTwoHandLabDiagnostics(two_hand_lab::Diagnostics& out) noexcept
    {
        out = two_hand_lab::Diagnostics{};
        auto& published = g_twoHandLabDiagnostics;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const uint64_t serial = published.serial.load(std::memory_order_relaxed);
            const bool valid = published.valid.load(std::memory_order_relaxed) != 0;
            two_hand_lab::Diagnostics candidate{};
            candidate.labEnabledStored = published.labEnabledStored.load(std::memory_order_relaxed) != 0;
            candidate.labActiveThisFrame = published.labActiveThisFrame.load(std::memory_order_relaxed) != 0;
            candidate.requestedAnchor =
                NormalizeTwoHandLabAnchor(published.requestedAnchor.load(std::memory_order_relaxed));
            // ResolvedAnchor shares the Production/AA/AG/GA/GG declaration
            // order with AnchorMode, so the same normalizer applies.
            candidate.resolvedAnchor = static_cast<two_hand_lab::ResolvedAnchor>(
                NormalizeTwoHandLabAnchor(published.resolvedAnchor.load(std::memory_order_relaxed)));
            const int fallbackRaw = published.fallback.load(std::memory_order_relaxed);
            candidate.fallback = (fallbackRaw >= 0 && fallbackRaw <= 3)
                                     ? static_cast<two_hand_lab::FallbackReason>(fallbackRaw)
                                     : two_hand_lab::FallbackReason::None;
            candidate.primaryGripValid = published.primaryGripValid.load(std::memory_order_relaxed) != 0;
            candidate.supportGripValid = published.supportGripValid.load(std::memory_order_relaxed) != 0;
            candidate.bValid = published.bValid.load(std::memory_order_relaxed) != 0;
            candidate.agreement = published.agreement.load(std::memory_order_relaxed);
            candidate.agreementMode =
                NormalizeTwoHandLabAgreement(published.agreementMode.load(std::memory_order_relaxed));
            candidate.confidence = published.confidence.load(std::memory_order_relaxed);
            candidate.requestedInfluence = published.requestedInfluence.load(std::memory_order_relaxed);
            candidate.effectiveInfluence = published.effectiveInfluence.load(std::memory_order_relaxed);
            candidate.temporalMode =
                NormalizeTwoHandLabTemporal(published.temporalMode.load(std::memory_order_relaxed));
            candidate.temporalActive = published.temporalActive.load(std::memory_order_relaxed) != 0;
            candidate.errorDeg = published.errorDeg.load(std::memory_order_relaxed);
            candidate.pivotsValid = published.pivotsValid.load(std::memory_order_relaxed) != 0;
            candidate.primaryPivot.x = published.primaryPivotX.load(std::memory_order_relaxed);
            candidate.primaryPivot.y = published.primaryPivotY.load(std::memory_order_relaxed);
            candidate.primaryPivot.z = published.primaryPivotZ.load(std::memory_order_relaxed);
            candidate.supportPivot.x = published.supportPivotX.load(std::memory_order_relaxed);
            candidate.supportPivot.y = published.supportPivotY.load(std::memory_order_relaxed);
            candidate.supportPivot.z = published.supportPivotZ.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            if (!serial || !valid)
                return false;
            out = candidate;
            return true;
        }
        return false;
    }
    TelemetryVec3 ToTelemetryVec3(const XrVector3f& value) noexcept
    {
        return {value.x, value.y, value.z};
    }

    TelemetryQuat ToTelemetryQuat(const XrQuaternionf& value) noexcept
    {
        return {value.x, value.y, value.z, value.w};
    }

    TelemetryPose ToTelemetryPose(const XrPosef& value) noexcept
    {
        return {ToTelemetryQuat(value.orientation), ToTelemetryVec3(value.position)};
    }

    TelemetryEffectiveSettings ToTelemetrySettings(const AimPoseInputs& inputs) noexcept
    {
        TelemetryEffectiveSettings settings{};
        settings.twoHandEnabled = inputs.twoHandEnabled;
        settings.twoHandLatched = inputs.twoHandLatched;
        settings.twoHandToggle = inputs.twoHandToggle;
        settings.virtualStockEnabled = inputs.virtualStockEnabled;
        settings.virtualStockRearReference = inputs.virtualStockRearReference;
        settings.virtualStockStrength = inputs.virtualStockStrength;
        settings.hybridDiagnosticOverride = static_cast<uint8_t>(
            NormalizeHybridDiagnosticOverride(static_cast<uint8_t>(inputs.hybridDiagnosticOverride)));
        settings.hybridOffhandInfluence = inputs.virtualStockHybridOffhandInfluence;
        settings.hybridAdsReference = inputs.virtualStockHybridAdsReference;
        settings.hybridSeatFullM = inputs.virtualStockHybridSeatFullM;
        settings.hybridSeatReleaseM = inputs.virtualStockHybridSeatReleaseM;
        settings.horizontalReleaseEnabled = inputs.hybridHorizontalRearReleaseEnabled;
        settings.horizontalReleaseFullM = inputs.hybridHorizontalRearReleaseFullM;
        settings.horizontalReleaseReleaseM = inputs.hybridHorizontalRearReleaseReleaseM;
        settings.inverseNeckEnabled = inputs.hybridInverseNeckEnabled;
        settings.inverseNeckStrength = inputs.hybridInverseNeckStrength;
        settings.inverseNeckForwardM = inputs.hybridInverseNeckForwardM;
        settings.inverseNeckUpM = inputs.hybridInverseNeckUpM;
        settings.inverseNeckLateralM = inputs.hybridInverseNeckLateralM;
        settings.rearHeightM = inputs.virtualStockRearHeightM;
        settings.shoulderBackM = inputs.virtualStockShoulderBackM;
        settings.shoulderSideM = inputs.virtualStockShoulderSideM;
        settings.chestHeightM = inputs.virtualStockChestHeightM;
        settings.chestBackM = inputs.virtualStockChestBackM;
        settings.chestSideM = inputs.virtualStockChestSideM;
        settings.proximityRelease = inputs.virtualStockProximityRelease;
        settings.proximityFullM = inputs.virtualStockProximityFullM;
        settings.proximityReleaseM = inputs.virtualStockProximityReleaseM;
        settings.gunYawDeg = inputs.gunYawDeg;
        settings.gunPitchDeg = inputs.gunPitchDeg;
        settings.gunRollDeg = inputs.gunRollDeg;
        settings.supportGripPoseEnabled = inputs.supportGripPoseEnabled;
        settings.supportEndpointUsedGrip = inputs.supportEndpointUsedGrip;
        settings.leftHanded = inputs.virtualStockLeftHanded;
        // Effective (production-consumed) reticle/steering knobs: clamped
        // exactly like the production uses (cf. the reticle block), so the
        // frame explains its own configuration context.
        settings.aimStabilization = std::clamp(g_config.aim_stabilization, 0.0f, 0.95f);
        settings.crosshairDistanceM = std::clamp(g_config.crosshair_distance_m, 2.0f, 50.0f);
        settings.crosshairSizeDeg = g_config.crosshair_size_deg;
        settings.crosshair = g_config.crosshair;
        settings.killReticle = g_config.kill_reticle;
        return settings;
    }

    TelemetryAimResult ToTelemetryAimResult(const AimPoseResult& result) noexcept
    {
        TelemetryAimResult telemetry{};
        telemetry.valid = result.valid;
        telemetry.twoHandActive = result.twoHandActive;
        telemetry.rejectedExtreme = result.rejectedExtreme;
        telemetry.rejectedAgreement = result.rejectedAgreement;
        if (result.valid)
        {
            telemetry.pose = ToTelemetryPose(result.pose);
            telemetry.forward = ToTelemetryVec3(Rotate(result.pose.orientation, {0.0f, 0.0f, -1.0f}));
        }
        return telemetry;
    }

    TelemetryControlResult ToTelemetryControlResult(VirtualStockTestProfile profile, const AimPoseInputs& inputs,
        const AimPoseResult& result, const AimPoseTrace& trace) noexcept
    {
        TelemetryControlResult control{};
        control.profileId = static_cast<uint8_t>(profile);
        control.effectiveSettings = ToTelemetrySettings(inputs);
        control.aim = ToTelemetryAimResult(result);
        control.path = static_cast<uint8_t>(trace.path);
        control.requestedTarget = static_cast<uint8_t>(trace.requestedTarget);
        control.actualTarget = static_cast<uint8_t>(trace.actualTarget);
        control.shoulderToHeadFallback = trace.shoulderToHeadFallback;
        control.fixedTargetValid = trace.fixedTargetValid;
        control.fixedTarget = {trace.fixedTarget.x, trace.fixedTarget.y, trace.fixedTarget.z};
        control.fixedRearDistanceValid = trace.fixedRearDistanceValid;
        control.fixedRearToTargetDistanceM = trace.fixedRearToTargetDistanceM;
        control.fixedProximityEnabled = trace.fixedProximityEnabled;
        control.fixedProximityCalculated = trace.fixedProximityCalculated;
        control.fixedProximityInfluence = trace.fixedProximityInfluence;
        control.fixedConfiguredStrength = trace.fixedConfiguredStrength;
        control.fixedEffectiveStrength = trace.fixedEffectiveStrength;
        control.fixedDirectionValid = trace.fixedDirectionValid;
        control.exactAEndpointSelected = trace.exactAEndpointSelected;
        control.orientationRebuildAttempted = trace.orientationRebuildAttempted;
        control.orientationRebuildSucceeded = trace.orientationRebuildSucceeded;
        return control;
    }

    // Grab/release aim continuity telemetry for this prepared serial. Fills the
    // transition family from the layer state and the seam's own same-frame
    // output; raw live aim evidence (aim_trace.final_direction, canonical_aim)
    // is untouched by this function. `leftValid` is the support validity of the
    // exact assembly this frame's canonical solve consumed, so the reported
    // input-smoothing consumption can never disagree with the geometry the
    // solve applied (see ApplyPreparedTwoHandInputSmoothing).
    void FillAimContinuityTelemetry(TelemetryFrame& frame, bool leftValid) noexcept
    {
        const virtual_stock::AimContinuityDiagnostics diagnostics =
            ReadAimContinuityDiagnostics(g_aimContinuityLayer.transition);
        // Product stock mode this frame: 1 = Plus (rear reference 3),
        // 0 = Standard. The mode is only meaningful while Virtual Stock is
        // enabled; an invalid mode is reported as 0, never as a stale or
        // guessed value.
        frame.transitionStockModeValid = g_config.virtual_stock;
        frame.transitionStockMode = frame.transitionStockModeValid && VirtualStockUsesPlusMode(g_config) ? 1u : 0u;
        frame.transitionActive = diagnostics.active;
        frame.transitionPhase = static_cast<uint8_t>(diagnostics.phase);
        frame.transitionEdgeKind = static_cast<uint8_t>(diagnostics.edge_kind);
        frame.transitionAnchorSource = static_cast<uint8_t>(diagnostics.anchor_source);
        frame.transitionInitialCorrectionDeg =
            std::isfinite(diagnostics.initial_correction_deg) ? diagnostics.initial_correction_deg : 0.0f;
        frame.transitionRemainingCorrectionDeg =
            std::isfinite(diagnostics.remaining_correction_deg) ? diagnostics.remaining_correction_deg : 0.0f;
        frame.transitionElapsedMs =
            std::isfinite(diagnostics.transition_elapsed_ms) ? diagnostics.transition_elapsed_ms : 0.0f;
        frame.transitionAdvanceCount = g_aimContinuityLayer.advanceCount;
        frame.transitionLastPreparedSerial = diagnostics.last_prepared_serial;
        // The retired two_hand_transition_smoothing key no longer resolves:
        // "configured" reports the effective (always-on) product truth while
        // the field itself stays emitted as the analyser's family marker.
        frame.twoHandTransitionSmoothingConfigured = TwoHandTransitionContinuityEnabled();
        frame.twoHandTransitionActive = TwoHandTransitionContinuityEnabled() &&
                                        !frame.effectiveSettings.virtualStockEnabled && diagnostics.active &&
                                        g_aimContinuityOutput.serial == g_preparedFrame.serial;
        const TwoHandInputSmoothingPreparedOutput& smoothing = g_twoHandInputSmoothingOutput;
        const bool smoothingCurrent =
            smoothing.serial == g_preparedFrame.serial && smoothing.active && smoothing.sampleValid;
        // Frozen per-serial strength/mix from the packet this frame prepared;
        // 0 when the packet belongs to another serial (feature absent).
        const bool smoothingSerialCurrent = smoothing.serial == g_preparedFrame.serial;
        frame.twoHandSmoothingStrength = smoothingSerialCurrent ? smoothing.strength : 0.0f;
        // configured means the player asked for a non-zero strength. applied
        // additionally requires this frame's eligible two-hand path (Virtual
        // Stock on or off) to have consumed the strength this serial; the
        // terms are the same eligibility rule the frame's own apply gate used
        // (latched two-hand assembly with a valid support controller), so
        // applied never claims a consumption the solve did not make.
        frame.twoHandSmoothingConfigured = frame.twoHandSmoothingStrength > 0.0f;
        frame.twoHandSmoothingApplied = smoothingCurrent && frame.twoHandSmoothingStrength > 0.0f &&
                                        frame.effectiveSettings.twoHandEnabled &&
                                        frame.effectiveSettings.twoHandLatched && leftValid;
        if (smoothingCurrent)
        {
            frame.twoHandSmoothingAlpha = smoothing.alpha;
            frame.twoHandSmoothingPrimaryOrientationErrorDeg = smoothing.orientationErrorDeg;
            frame.twoHandSmoothingPrimaryPositionErrorM = smoothing.primaryPositionErrorM;
            frame.twoHandSmoothingSupportPositionErrorM = smoothing.supportPositionErrorM;
        }

        // Everything below is only meaningful when the layer actually ran for
        // this prepared serial. Absence is recorded as invalid, never as a
        // zero/identity value that could be mistaken for an observation.
        const AimContinuityPreparedOutput& output = g_aimContinuityOutput;
        if (output.serial != g_preparedFrame.serial)
            return;
        frame.transitionAppliedSerial = output.serial;
        const auto forward = [](virtual_stock::Quat4 orientation, TelemetryVec3& out)
        {
            const virtual_stock::Point3 direction = virtual_stock::RotatePoint(orientation, {0.0f, 0.0f, -1.0f});
            if (!virtual_stock::Finite(direction))
                return false;
            out = {direction.x, direction.y, direction.z};
            return true;
        };
        if (output.liveValid)
        {
            frame.transitionLiveCalibratedForwardValid = forward(output.live, frame.transitionLiveCalibratedForward);
        }
        if (output.oneHandValid)
        {
            frame.transitionOneHandAnchorValid = forward(output.oneHand, frame.transitionOneHandAnchorForward);
        }
        if (output.presentedValid)
        {
            frame.transitionPresentedForwardValid = forward(output.presented, frame.transitionPresentedForward);
        }
    }

    // Two-Hand Lab telemetry for this prepared serial. Sources: the frame's
    // canonical solve trace Lab payload (stateless observation) plus the Lab
    // temporal layer's same-thread output (presented orientation) — never
    // recomputed geometry in the recorder, no live-global reads from the
    // recorder thread (this runs on the frame thread right after the seam),
    // no new OpenXR sampling. Raw live aim evidence
    // (aim_trace.final_direction, canonical_aim) is untouched by this
    // function, and existing raw fields stay intact.
    void FillTwoHandLabTelemetry(TelemetryFrame& frame, const AimPoseInputs& inputs) noexcept
    {
        const two_hand_lab::Diagnostics& lab = frame.aimTrace.lab;
        const bool enabled = frame.aimTrace.labValid;
        frame.twoHandLabEnabled = enabled;
        if (enabled)
        {
            frame.twoHandLabAnchorRequested = static_cast<uint8_t>(lab.requestedAnchor);
            frame.twoHandLabAnchorResolved = static_cast<uint8_t>(lab.resolvedAnchor);
            frame.twoHandLabAnchorFallback = static_cast<uint8_t>(lab.fallback);
            frame.twoHandLabOffhandInfluence = std::isfinite(lab.requestedInfluence) ? lab.requestedInfluence : 0.0f;
            frame.twoHandLabAgreementMode = static_cast<uint8_t>(lab.agreementMode);
            frame.twoHandLabAgreement = std::isfinite(lab.agreement) ? lab.agreement : 0.0f;
            frame.twoHandLabAgreementConfidence = std::isfinite(lab.confidence) ? lab.confidence : 0.0f;
            frame.twoHandLabEffectiveInfluence = std::isfinite(lab.effectiveInfluence) ? lab.effectiveInfluence : 0.0f;
            frame.twoHandLabTemporalMode = static_cast<uint8_t>(lab.temporalMode);
            frame.twoHandLabPrimaryPivotValid = lab.pivotsValid;
            frame.twoHandLabSupportPivotValid = lab.pivotsValid;
            if (lab.pivotsValid)
            {
                frame.twoHandLabPrimaryPivot = {lab.primaryPivot.x, lab.primaryPivot.y, lab.primaryPivot.z};
                frame.twoHandLabSupportPivot = {lab.supportPivot.x, lab.supportPivot.y, lab.supportPivot.z};
            }
            // Stateless direction: the recorded output of the Lab-active
            // canonical solve itself (already computed above), valid exactly
            // when that solve was valid.
            if (frame.canonicalAim.valid)
            {
                frame.twoHandLabStatelessDirectionValid = true;
                frame.twoHandLabStatelessDirection = frame.canonicalAim.forward;
            }
        }
        // Presented direction: the temporal layer's same-thread output for
        // this exact serial, gated on the generation of the solve the
        // telemetry inputs were assembled with. A settings toggle between
        // the seam and this capture leaves the presented direction invalid
        // rather than describing the wrong solve.
        const TwoHandLabTemporalPreparedOutput& temporal = g_twoHandLabTemporalOutput;
        if (enabled && temporal.serial == frame.preparedSerial &&
            temporal.labGeneration == inputs.twoHandLabGeneration && temporal.temporalActive && temporal.presentedValid)
        {
            const virtual_stock::Point3 direction = virtual_stock::RotatePoint(temporal.presented, {0.0f, 0.0f, -1.0f});
            if (virtual_stock::Finite(direction))
            {
                frame.twoHandLabPresentedDirectionValid = true;
                frame.twoHandLabPresentedDirection = {direction.x, direction.y, direction.z};
                frame.twoHandLabTemporalActive = true;
                frame.twoHandLabTemporalErrorDeg = std::isfinite(temporal.errorDeg) ? temporal.errorDeg : 0.0f;
            }
        }
    }

    // Persistent support grip (PG) frame telemetry for this prepared serial:
    // the FROZEN solve-time provenance of the frame-local canonical assembly
    // (the AimPoseInputs the solve below consumed), plus the config knob and
    // the pure title gate as read for this same frame. This function re-reads
    // only those two scalars and copies everything else from `inputs`; it
    // never re-reads mutable PG relationship state, never runs a
    // qualification and never touches the durable writer or any PG getter, so
    // it cannot observe a relationship newer than the one this frame solved
    // with.
    // absent-observation semantics: with the feature off, the qualification
    // short-circuits and the copied fields are the frozen zero/false values,
    // so the family never reports a stale or fabricated receipt.
    void FillPersistentSupportGripTelemetry(
        TelemetryFrame& frame, const AimPoseInputs& inputs, GameTitle activeTitle) noexcept
    {
        frame.persistentSupportGripConfigured = g_config.persistent_support_grip;
        frame.persistentSupportGripApplicable = support_grip::PersistentSupportGripApplies(activeTitle);
        frame.supportRelationshipReadable = inputs.supportRelationshipReadable;
        frame.supportRelationshipEngaged = inputs.supportRelationshipEngaged;
        frame.supportEpoch = inputs.supportEpoch;
        // The PERMISSION bit of this assembly's qualification (the same value
        // steering retention is sourced from). Deliberately not
        // `canonical.supportTrusted`: that receipt is narrowed by what the
        // solve actually consumed and would conflate permission with
        // consumption.
        frame.supportSolveTrusted = inputs.supportMayConsume;
        frame.supportForceOneHand = inputs.supportForceOneHand;
        frame.supportSolveSerial = inputs.supportSolveSerial;
    }
    // An engine forward is only feedback when it is finite AND carries a
    // direction: a zero vector is unusable as aim, so it records invalid
    // (with zero payload) rather than a valid non-direction. Positions use
    // TelemetryFinite3 only; the origin is a legitimate position.
    bool TelemetryDirectionUsable(const float value[3]) noexcept
    {
        if (!TelemetryFinite3(value))
            return false;
        const double lengthSquared = static_cast<double>(value[0]) * value[0] +
                                     static_cast<double>(value[1]) * value[1] +
                                     static_cast<double>(value[2]) * value[2];
        return lengthSquared > 1e-12;
    }

    void FillPresentedAimTelemetry(TelemetryFrame& frame, const AimPoseInputs& inputs, const AimPoseResult& canonical,
        bool stockHeadValid) noexcept
    {
        frame.presentedAimValid = false;
        frame.presentedAimForward = TelemetryVec3{0.0f, 0.0f, 0.0f};
        // The getter's own gate: without a right pose there is no presented
        // output (and the two-hand activity indicator is preserved there).
        if (!canonical.updateTwoHandActivity)
            return;
        XrQuaternionf presented{0.0f, 0.0f, 0.0f, 1.0f};
        if (!ComposePresentedAimForFrameThread(canonical.pose.orientation, frame.preparedSerial, stockHeadValid,
                inputs.supportEndpointUsedGrip, inputs.twoHandLabGeneration, inputs.virtualStockEnabled, presented))
            return;
        const virtual_stock::Point3 direction = virtual_stock::RotatePoint(
            virtual_stock::Quat4{presented.x, presented.y, presented.z, presented.w}, {0.0f, 0.0f, -1.0f});
        if (!virtual_stock::Finite(direction))
            return;
        // The getter's final success gate, mirrored exactly
        // (VR_GetAimPoseWithSupportProvenance: `if (!aim.valid) return false;`).
        // A solve whose orientation failed finishAimPose's normalisation is not
        // a provenance-valid ray, so the initialised zero payload above stands;
        // never a fabricated presented ray the family's own rule forbids.
        if (!canonical.valid)
            return;
        frame.presentedAimForward = TelemetryVec3{direction.x, direction.y, direction.z};
        frame.presentedAimValid = true;
    }
    void FillEngineAimAndCameraTelemetry(TelemetryFrame& frame) noexcept
    {
        // Invalid-payload convention for the new families: validity flags are
        // authoritative, serials/epochs read 0 when none, and invalid vectors
        // read zero (never stale, never non-finite, so the JSON stays finite
        // and the validator can prove absence). Sources name the attributed
        // publication even when its sample is absent (except titles with no
        // publication at all, which read None).
        frame.engineAimValid = false;
        frame.engineAimSource = 0;
        frame.engineAimForward = TelemetryVec3{0.0f, 0.0f, 0.0f};
        frame.engineAimSerial = 0;
        frame.engineAimSampleMs = 0;
        frame.engineAimPitchValid = false;
        frame.engineAimPitchDeg = 0.0f;
        frame.engineCameraBaseValid = false;
        frame.engineCameraBasePosition = TelemetryVec3{0.0f, 0.0f, 0.0f};
        frame.engineCameraEyeValid = false;
        frame.engineCameraEyePosition = TelemetryVec3{0.0f, 0.0f, 0.0f};
        frame.engineCameraSource = 0;
        frame.engineCameraSerial = 0;
        frame.worldScaleValid = false;
        frame.worldScale = 0.0f;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        if (title == GameTitle::Halo3 || title == GameTitle::Halo3ODST)
        {
            // Shared H3/ODST singletons (C-002): title-qualify at read and
            // label which title's camera copy wrote them. Latest-only, no
            // serial: a concurrent writer can mix components across frames,
            // exactly as the in-game consumers accept.
            frame.engineAimSource = static_cast<uint8_t>(
                title == GameTitle::Halo3 ? TelemetryEngineAimSource::H3Shared : TelemetryEngineAimSource::OdstShared);
            frame.engineCameraSource = static_cast<uint8_t>(
                title == GameTitle::Halo3 ? TelemetryEngineCameraSource::H3 : TelemetryEngineCameraSource::Odst);
            if (TitleAdapter_GetGeneration(title) == 0)
                return;
            float aimForward[3]{};
            if (Game_TelemetryReadSharedAim(aimForward) && TelemetryDirectionUsable(aimForward))
            {
                frame.engineAimValid = true;
                frame.engineAimForward = TelemetryVec3{aimForward[0], aimForward[1], aimForward[2]};
            }
            bool baseValid = false, eyeValid = false;
            float base[3]{}, eye[3]{};
            if (Game_TelemetryReadSharedCamera(baseValid, base, eyeValid, eye))
            {
                frame.engineCameraBaseValid = baseValid && TelemetryFinite3(base);
                if (frame.engineCameraBaseValid)
                    frame.engineCameraBasePosition = TelemetryVec3{base[0], base[1], base[2]};
                frame.engineCameraEyeValid = eyeValid && TelemetryFinite3(eye);
                if (frame.engineCameraEyeValid)
                    frame.engineCameraEyePosition = TelemetryVec3{eye[0], eye[1], eye[2]};
            }
            float scale = 0.0f;
            if (title == GameTitle::Halo3)
            {
                if (Game_TelemetryReadWorldScale(scale))
                {
                    frame.worldScaleValid = true;
                    frame.worldScale = scale;
                }
            }
            else
            {
                // ODST scale authority is the fixed constant (game.cpp writes
                // no live world scale for ODST); 1/3.048 matches
                // kOdstWorldUnitsPerMeter (game.cpp).
                frame.worldScaleValid = true;
                frame.worldScale = 1.0f / 3.048f;
            }
        }
        else if (title == GameTitle::HaloReach)
        {
            frame.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::ReachOnFootCompactFallback);
            frame.engineCameraSource = static_cast<uint8_t>(TelemetryEngineCameraSource::Reach);
            // Seated native truth first (source enum + generation + sampleMs
            // travel); the shared compact fallback below is labelled as such.
            // Never a blind xyz: an unadmitted read records invalid with the
            // fallback source, never a guessed vector.
            float seated[3]{};
            uint8_t seatedSource = 0;
            uint32_t seatedGeneration = 0;
            uint64_t seatedSampleMs = 0;
            if (Game_TelemetryReadReachSeatedAim(seated, seatedSource, seatedGeneration, seatedSampleMs) &&
                TelemetryDirectionUsable(seated) &&
                (seatedSource == static_cast<uint8_t>(ReachAimFeedbackSource::SeatedUnitAim) ||
                    seatedSource == static_cast<uint8_t>(ReachAimFeedbackSource::SeatedCompactFallback)))
            {
                frame.engineAimValid = true;
                frame.engineAimSource =
                    static_cast<uint8_t>(seatedSource == static_cast<uint8_t>(ReachAimFeedbackSource::SeatedUnitAim)
                                             ? TelemetryEngineAimSource::ReachSeatedUnitAim
                                             : TelemetryEngineAimSource::ReachSeatedCompactFallback);
                frame.engineAimForward = TelemetryVec3{seated[0], seated[1], seated[2]};
                frame.engineAimSampleMs = seatedSampleMs;
            }
            else if (TitleAdapter_GetGeneration(GameTitle::HaloReach) != 0)
            {
                float compact[3]{};
                if (Game_TelemetryReadSharedAim(compact) && TelemetryDirectionUsable(compact))
                {
                    frame.engineAimValid = true;
                    frame.engineAimForward = TelemetryVec3{compact[0], compact[1], compact[2]};
                }
            }
            float eye[3]{};
            uint64_t eyeSerial = 0;
            if (Game_TelemetryReadReachCompletedEye(eye, eyeSerial))
            {
                frame.engineCameraEyeValid = true;
                frame.engineCameraEyePosition = TelemetryVec3{eye[0], eye[1], eye[2]};
                frame.engineCameraSerial = eyeSerial;
            }
            // Reach scale authority is the fixed constant (game.cpp writes no
            // live world scale for Reach); 1/3.048 matches kOdstWorldUnitsPer-
            // Meter (game.cpp) and kReachWorldUnitsPerMeter
            // (reach_render_logic.h).
            frame.worldScaleValid = true;
            frame.worldScale = kReachWorldUnitsPerMeter;
        }
        else if (title == GameTitle::Halo4)
        {
            frame.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H4Observer);
            // T-3: the stereo transaction publishes its pristine observer
            // position with its own serial. A successful read labels the
            // source H4Observer and fills the eye; otherwise the source stays
            // the explicit absent label (before the first owned frame, after
            // teardown) and the eye stays invalid/zero, never a stale
            // position.
            frame.engineCameraSource = static_cast<uint8_t>(TelemetryEngineCameraSource::H4Absent);
            float aimForward[3]{};
            bool pitchValid = false;
            float pitchRadians = 0.0f;
            uint64_t pitchSerial = 0;
            if (Game_TelemetryReadHalo4EngineAim(aimForward, pitchValid, pitchRadians, pitchSerial) &&
                TelemetryDirectionUsable(aimForward))
            {
                // The publication carries radians (asinf of the observer
                // forward Z); the wire format is degrees.
                const float pitchDeg = pitchRadians * 57.29578f;
                if (std::isfinite(pitchDeg))
                {
                    frame.engineAimValid = true;
                    frame.engineAimForward = TelemetryVec3{aimForward[0], aimForward[1], aimForward[2]};
                    frame.engineAimSerial = pitchSerial;
                    frame.engineAimPitchValid = true;
                    frame.engineAimPitchDeg = pitchDeg;
                }
            }
            // T-3 camera: the stereo transaction's pristine observer position.
            // Base stays the engine's pre-lean base only where a title
            // publishes one; Halo 4 publishes the observer eye alone.
            float stockEye[3]{};
            uint64_t stockEyeSerial = 0;
            if (Game_TelemetryReadHalo4Camera(stockEye, stockEyeSerial))
            {
                frame.engineCameraSource = static_cast<uint8_t>(TelemetryEngineCameraSource::H4Observer);
                frame.engineCameraEyeValid = true;
                frame.engineCameraEyePosition = TelemetryVec3{stockEye[0], stockEye[1], stockEye[2]};
                frame.engineCameraSerial = stockEyeSerial;
            }
            float scale = 0.0f;
            if (Game_TelemetryReadWorldScale(scale))
            {
                frame.worldScaleValid = true;
                frame.worldScale = scale;
            }
        }
        else if (title == GameTitle::Halo2)
        {
            // No g_aimFwd writer exists for Halo 2: the observer publication
            // (stock = the engine's own camera, tracked = VR-composed) is the
            // feedback. Unreadable here means absent, never a guessed vector.
            // The underlying Halo2Observer6Dof_ReadPublishedPose is version-
            // bracketed and counts an unstable read in its g_publicationTornReads
            // diagnostic (a counter with no consumers today); a torn read is an
            // accepted side observation and fails open here as absent.
            frame.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H2Absent);
            frame.engineCameraSource = static_cast<uint8_t>(TelemetryEngineCameraSource::H2);
            const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo2);
            Halo2ObserverPosePublication publication{};
            if (generation != 0 && Halo2Observer6Dof_ReadPublishedPose(publication) &&
                publication.generation == generation && publication.serial != 0 &&
                TelemetryDirectionUsable(publication.stock.forward))
            {
                frame.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H2Observer);
                frame.engineAimValid = true;
                frame.engineAimForward = TelemetryVec3{
                    publication.stock.forward[0], publication.stock.forward[1], publication.stock.forward[2]};
                frame.engineAimSerial = publication.serial;
                if (TelemetryFinite3(publication.stock.position))
                {
                    frame.engineCameraEyeValid = true;
                    frame.engineCameraEyePosition = TelemetryVec3{
                        publication.stock.position[0], publication.stock.position[1], publication.stock.position[2]};
                    frame.engineCameraSerial = publication.serial;
                }
            }
            frame.worldScaleValid = true;
            frame.worldScale = kHalo2WorldUnitsPerMeter;
        }
        // Halo CE and any other title: no publication, sources stay None.
    }
} // namespace
namespace vr_core
{
    void CapturePreparedFrameTelemetry(
        const XrFrameState& frameState, bool upcomingPadFresh, bool upcomingViewsValid, bool upcomingHeadValid) noexcept
    {
        if (!Telemetry_BeginFrame(g_preparedFrame.serial))
            return;

        LARGE_INTEGER captureBegin{};
        QueryPerformanceCounter(&captureBegin);
        TelemetryFrame frame{};
        frame.preparedSerial = g_preparedFrame.serial;
        frame.predictedDisplayTime = frameState.predictedDisplayTime;
        frame.predictedDisplayPeriod = frameState.predictedDisplayPeriod;
        frame.captureBeginQpc = captureBegin.QuadPart;
        frame.contactSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
        frame.contactSpaceChangeAtNs = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        // One title read for this frame: the frame identity and the PG
        // applicability gate below must name the same active title.
        const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
        frame.activeTitle = static_cast<uint8_t>(activeTitle);
        frame.sessionState = static_cast<int32_t>(g_sessionState);
        frame.shouldRender = frameState.shouldRender == XR_TRUE;
        frame.upcomingViewsValid = upcomingViewsValid;
        frame.locatedViewCount = upcomingViewsValid ? static_cast<uint32_t>(g_views.size()) : 0;
        frame.focused = g_sessionState == XR_SESSION_STATE_FOCUSED;
        frame.stereoEnabled = g_stereoEnabled.load(std::memory_order_relaxed);
        frame.menuOpen = Menu_IsOpen();

        const bool primaryFresh = upcomingPadFresh && g_rightAimPoseValid;
        const bool supportFresh = upcomingPadFresh && g_leftAimPoseValid;
        const bool supportGripFresh =
            upcomingPadFresh && g_supportGripPoseValid && g_supportGripPoseFresh.load(std::memory_order_acquire);
        const bool stockHeadValid =
            upcomingPadFresh && upcomingHeadValid && g_stockAimFresh.load(std::memory_order_acquire);
        AimPoseInputs inputs = CurrentStockAimPoseInputs(primaryFresh, g_rightAimPose, supportFresh, g_leftAimPose,
            stockHeadValid, g_headPose.position, g_headPose.orientation, supportGripFresh, g_supportGripPosePosition,
            primaryFresh && g_primaryGripPoseValid, g_primaryGripPosePosition);
        ApplyPreparedTwoHandInputSmoothing(inputs, frame.preparedSerial);
        const AimPoseResult canonical = ComputeAimPose(inputs, &frame.aimTrace);

        frame.testProfileId = static_cast<uint8_t>(inputs.testProfileUsed);
        frame.testProfileCustom = inputs.testProfileUsed == VirtualStockTestProfile::Custom;
        frame.effectiveSettings = ToTelemetrySettings(inputs);
        // Free two-hand (VS-OFF) product offhand directional authority for this
        // frame, sourced from the same frame-local assembly the canonical solve
        // consumed (`inputs`, stamped once by CurrentStockAimPoseInputs from
        // `two_hand_offhand_influence`), never a live re-read of the config
        // slider at capture. Recorded as the solver consumes it: non-finite
        // reads 0, otherwise clamp(0,1). Virtual Stock solves never read the
        // product value and a Lab-active solve uses its own Lab authority.
        frame.twoHandOffhandInfluence = std::isfinite(inputs.twoHandOffhandInfluence)
                                            ? std::clamp(inputs.twoHandOffhandInfluence, 0.0f, 1.0f)
                                            : 0.0f;
        frame.canonicalAim = ToTelemetryAimResult(canonical);
        FillAimContinuityTelemetry(frame, inputs.leftValid);
        FillTwoHandLabTelemetry(frame, inputs);
        FillPersistentSupportGripTelemetry(frame, inputs, activeTitle);
        FillPresentedAimTelemetry(frame, inputs, canonical, stockHeadValid);
        FillPresentedReticleTelemetry(frame);
        FillEngineAimAndCameraTelemetry(frame);
        frame.dualActive = SecondaryWeaponPresentationActive();

        const auto solveControl = [&](VirtualStockTestProfile profile, TelemetryControlResult& output)
        {
            const AimPoseInputs controlInputs = AimPoseInputsForProfile(inputs, profile);
            AimPoseTrace controlTrace{};
            const AimPoseResult controlAim = ComputeAimPose(controlInputs, &controlTrace);
            output = ToTelemetryControlResult(profile, controlInputs, controlAim, controlTrace);
        };
        solveControl(kVsOffControlProfile, frame.cfVsOff);
        solveControl(kFixedHeadControlProfile, frame.cfFixedHead);
        solveControl(kFixedShoulderControlProfile, frame.cfFixedShoulder);

        frame.semanticPrimaryValid = primaryFresh;
        if (primaryFresh)
        {
            frame.semanticPrimaryAim = ToTelemetryPose(g_rightAimPose);
            frame.semanticPrimaryForward = ToTelemetryVec3(Rotate(g_rightAimPose.orientation, {0.0f, 0.0f, -1.0f}));
        }
        frame.semanticSupportValid = supportFresh;
        if (supportFresh)
            frame.semanticSupportAim = ToTelemetryPose(g_leftAimPose);
        frame.supportEndpointValid = supportFresh;
        frame.supportEndpointUsedGrip = inputs.supportEndpointUsedGrip;
        if (frame.supportEndpointValid)
            frame.supportEndpoint = ToTelemetryVec3(inputs.supportPosition);

        frame.physicalLeftAimValid = upcomingPadFresh && g_physicalControllerValid[0];
        if (frame.physicalLeftAimValid)
            frame.physicalLeftAim = ToTelemetryPose(g_physicalControllerPose[0]);
        frame.physicalRightAimValid = upcomingPadFresh && g_physicalControllerValid[1];
        if (frame.physicalRightAimValid)
            frame.physicalRightAim = ToTelemetryPose(g_physicalControllerPose[1]);

        frame.supportGripValid = supportGripFresh;
        if (supportGripFresh)
            frame.supportGripPosition = ToTelemetryVec3(g_supportGripPosePosition);
        frame.semanticPrimaryGripPositionValid = upcomingPadFresh && g_primaryGripPoseValid;
        if (frame.semanticPrimaryGripPositionValid)
        {
            frame.semanticPrimaryGripPosition = ToTelemetryVec3(g_primaryGripPosePosition);
        }

        frame.headSampleValid = upcomingHeadValid;
        frame.stockHeadValid = stockHeadValid;
        frame.headsetSmoothing = std::clamp(g_config.headset_smoothing, 0.0f, 0.10f);
        if (upcomingHeadValid)
            frame.semanticHmd = ToTelemetryPose(g_headPose);

        if (upcomingViewsValid && g_views.size() == 2)
        {
            for (int eye = 0; eye < 2; ++eye)
            {
                frame.views[eye].pose = ToTelemetryPose(g_views[eye].pose);
                frame.views[eye].fovLeft = g_views[eye].fov.angleLeft;
                frame.views[eye].fovRight = g_views[eye].fov.angleRight;
                frame.views[eye].fovUp = g_views[eye].fov.angleUp;
                frame.views[eye].fovDown = g_views[eye].fov.angleDown;
            }
        }

        frame.semanticPrimaryVelocityValid = upcomingPadFresh && g_rightAimLinearVelocityValid;
        if (frame.semanticPrimaryVelocityValid)
        {
            frame.semanticPrimaryVelocity = ToTelemetryVec3(g_rightAimLinearVelocity);
            frame.semanticPrimaryVelocityAtMs = g_rightAimLinearVelocityAtMs;
        }
        frame.semanticSupportVelocityValid = upcomingPadFresh && g_leftAimLinearVelocityValid;
        if (frame.semanticSupportVelocityValid)
        {
            frame.semanticSupportVelocity = ToTelemetryVec3(g_leftAimLinearVelocity);
            frame.semanticSupportVelocityAtMs = g_leftAimLinearVelocityAtMs;
        }

        if (upcomingPadFresh && g_padState.valid)
        {
            frame.pad.valid = true;
            frame.pad.moveX = g_padState.moveX;
            frame.pad.moveY = g_padState.moveY;
            frame.pad.turnX = g_padState.turnX;
            frame.pad.turnY = g_padState.turnY;
            frame.pad.trigL = g_padState.trigL;
            frame.pad.trigR = g_padState.trigR;
            frame.pad.gripL = g_padState.gripL;
            frame.pad.gripR = g_padState.gripR;
            frame.pad.a = g_padState.a;
            frame.pad.b = g_padState.b;
            frame.pad.x = g_padState.x;
            frame.pad.y = g_padState.y;
            frame.pad.clickL = g_padState.clickL;
            frame.pad.clickR = g_padState.clickR;
            frame.pad.menu = g_padState.menu;
            frame.pad.thumbrestDpad = g_padState.thumbrestDpad;
            frame.pad.dpadX = g_padState.dpadX;
            frame.pad.dpadY = g_padState.dpadY;
            frame.pad.exclusiveInput = g_padState.exclusiveInput;
            frame.pad.weaponButtons = g_padState.weaponButtons;
            frame.pad.weaponPulseUntilMs = g_padState.weaponPulseUntilMs;
            frame.pad.weaponGeneration = g_padState.weaponGeneration;
        }

        Telemetry_PublishFrame(frame);
    }
    void ProbeCaptureWeaponTitle(GameTitle title) noexcept
    {
        const uint8_t permission = Game_DiagnosticCaptureProbePermission(title);
        if (permission != 0)
        {
            const WeaponOrderEventStatus status = permission == 1   ? WeaponOrderEventStatus::NotAttemptedNoSafeThread
                                                  : permission == 2 ? WeaponOrderEventStatus::NotAttemptedThreadMismatch
                                                                    : WeaponOrderEventStatus::GuardRejected;
            Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeResult),
                static_cast<uint8_t>(status), static_cast<uint8_t>(title), TitleAdapter_GetGeneration(title),
                g_preparedFrame.serial, UINT32_MAX, UINT32_MAX, 0, permission);
            return;
        }
        const uint64_t beginSeq =
            Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeBegin),
                static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation), static_cast<uint8_t>(title),
                TitleAdapter_GetGeneration(title), g_preparedFrame.serial, UINT32_MAX, UINT32_MAX, 0, 0);
        uint32_t unit = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
        const bool readOk = Game_DiagnosticReadPrimaryWeapon(title, unit, weapon, detail);
        PublishCaptureProbeResult(title, beginSeq, readOk, unit, weapon, detail);
    }
} // namespace vr_core
namespace two_hand_lab_runtime
{
    bool GetLastDiagnostics(two_hand_lab::Diagnostics& out) noexcept
    {
        return ReadTwoHandLabDiagnostics(out);
    }
} // namespace two_hand_lab_runtime
void VR_GetNativeHudRouteStats(
    unsigned& completedPhaseScopes, unsigned& provenOmMatches, unsigned& exactCopyScopes, unsigned& copySubstitutions)
{
    completedPhaseScopes = g_nativeHudPhaseScopes.load(std::memory_order_acquire);
    provenOmMatches = g_nativeHudProvenOmMatches.load(std::memory_order_relaxed);
    exactCopyScopes = g_nativeHudExactCopyScopes.load(std::memory_order_relaxed);
    copySubstitutions = g_nativeHudCopySubstitutions.load(std::memory_order_relaxed);
}
namespace vr_core
{
    // FSR diagnostic (fsr_probe, off by default). Log-only: observe slot 0 of every
    // OMSetRenderTargets bind and record each DISTINCT (width, height, format,
    // bind-flags) SCENE-SCALE render target, plus the viewport and current raster
    // eye at that bind. When MCC's built-in FSR is toggled, a newly logged tuple
    // shows whether FSR renders the scene into a smaller target (proven: it does)
    // and, via the raster-eye column, whether that target is bound inside the
    // per-eye render loop. Runs on the render thread only, allocation-free and
    // lock-free: the table is a fixed static seen only from that thread.
    //
    // Two guards keep the log bounded (the first version dumped 1.16M lines / 145MB
    // because MCC binds hundreds of tiny transient post-fx/shadow targets and the
    // 12-slot table saturated instantly): skip anything narrower than ~40% of the
    // backbuffer (below any real FSR render scale, which bottoms out at 50%), and
    // dedup into a 64-slot table.
    void ProbeFsrTargets(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* input)
    {
        if (!context || !count || !input)
            return;
        // Normally an opt-in FSR investigation aid. It ALSO self-arms whenever a
        // per-eye redirect scope is active but no scene-colour target has ever
        // been learned - that exact combination IS the discovery failure, and this
        // census is the only thing that can name a new title's real scene target.
        // Self-arming matters because a player should never need a config flag to
        // prove a mod bug: the answer has to arrive from a normal run.
        // Bounded three ways: only while discovery is failing, only for
        // scene-scale targets, and only 96 distinct shapes in the process.
        const int rasterEye = g_rasterEye.load(std::memory_order_relaxed);
        const bool discoveryFailing = rasterEye >= 0 && !g_sceneColorRtv;
        // Keep censusing for a few eyes AFTER Halo 4 latches. Stopping at the
        // latch is what made the previous run undiagnosable: exactly one line was
        // logged, the one target we then wrongly picked, and the rest of the
        // pipeline was never described. g_h4LearningEyes only leaves zero for
        // Halo 4, so this window cannot arm for any other title.
        const bool halo4CensusWindow = rasterEye >= 0 && g_h4LearningEyes > 0 && g_h4LearningEyes <= 6;
        if (!g_config.fsr_probe && !discoveryFailing && !halo4CensusWindow)
            return;

        // When self-armed, walk every bound slot rather than slot 0 alone: a
        // deferred renderer binds an MRT set, and assuming the scene lands on
        // slot 0 is precisely the assumption that failed.
        const UINT slots = (discoveryFailing || halo4CensusWindow) ? count : 1u;
        const UINT bbW = g_gameBackbufferDescValid ? g_gameBackbufferDesc.Width : 0;
        const UINT bbH = g_gameBackbufferDescValid ? g_gameBackbufferDesc.Height : 0;
        const UINT minWidth = bbW ? (bbW * 2) / 5 : 800u;

        for (UINT slot = 0; slot < slots; ++slot)
        {
            if (!input[slot])
                continue;
            ID3D11Resource* resource = nullptr;
            input[slot]->GetResource(&resource);
            if (!resource)
                continue;
            ID3D11Texture2D* tex = nullptr;
            D3D11_TEXTURE2D_DESC desc{};
            const bool isTexture =
                SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex)));
            if (isTexture)
                tex->GetDesc(&desc);
            if (tex)
                tex->Release();
            resource->Release();
            if (!isTexture)
                continue;

            // Only scene-scale targets are interesting. Anything below ~40% of the
            // backbuffer width is a shadow/post-fx buffer, not a candidate scene
            // render (FSR's lowest tier is 50%). Fail-open if the backbuffer size
            // is not known yet: keep a fixed 800px floor.
            if (desc.Width < minWidth)
                continue;

            // Dedup on (slot, size, format, bind, rasterEye) so each distinct
            // large target is logged once PER slot and eye-context. The raster eye
            // tells us whether a target is bound inside the per-eye redirect scope
            // (0/1) or outside it (-1).
            struct SeenTarget
            {
                UINT slot, w, h, format, bind;
                int eye;
            };
            static SeenTarget seen[96]{};
            static unsigned seenCount = 0;
            const UINT descFormat = static_cast<UINT>(desc.Format);
            bool already = false;
            for (unsigned i = 0; i < seenCount && !already; ++i)
                already = seen[i].slot == slot && seen[i].w == desc.Width && seen[i].h == desc.Height &&
                          seen[i].format == descFormat && seen[i].bind == desc.BindFlags && seen[i].eye == rasterEye;
            if (already)
                continue;

            D3D11_VIEWPORT vp{};
            UINT vpCount = 1;
            context->RSGetViewports(&vpCount, &vp);

            // The RTV's own view format matters as much as the resource format: a
            // TYPELESS resource is only usable through its typed view, and that is
            // what the eye cache has to be created as.
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
            input[slot]->GetDesc(&viewDesc);

            LOG("SCENEPROBE: slot%u RT %ux%u fmt=%u viewfmt=%u bind=0x%X samples=%u "
                "| viewport %.0fx%.0f at (%.0f,%.0f) | backbuffer %ux%u "
                "| rasterEye=%d | rtCount=%u",
                slot, desc.Width, desc.Height, descFormat, static_cast<UINT>(viewDesc.Format), desc.BindFlags,
                desc.SampleDesc.Count, vpCount ? vp.Width : 0.0f, vpCount ? vp.Height : 0.0f,
                vpCount ? vp.TopLeftX : 0.0f, vpCount ? vp.TopLeftY : 0.0f, bbW, bbH, rasterEye, count);

            if (seenCount < 96)
                seen[seenCount++] = {slot, desc.Width, desc.Height, descFormat, desc.BindFlags, rasterEye};
        }
    }
} // namespace vr_core
