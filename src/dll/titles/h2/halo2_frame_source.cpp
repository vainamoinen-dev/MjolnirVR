// halo2_frame_source.cpp: the Halo 2 pieces of the VR frame path that lived in vr.cpp (moved unchanged). vr.cpp
// calls them through halo2_frame_bridge.h; they reach vr.cpp's frame state through vr_internal.h and
// vr_frame.h.
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../core/vr_internal.h"
#include "../../core/vr_frame.h"
#include "halo2_frame_bridge.h"
#include "../../core/game.h"
#include "../../core/title_adapter.h"
#include "../../../common/log.h"
#include "../../../common/config.h"
using namespace vr_core;
using namespace halo2_frame_bridge;

namespace
{
    // One eye is complete when the engine's FINISHED frame for it has been
    // copied into the eye cache exactly once, after render_view returned.
    constexpr uint32_t kHalo2SynchronousRequiredCaptures = 1;
} // namespace
namespace halo2_frame_bridge
{
    // 0 = no pair, high-bit(serial) = the render hook owns an unpublished pair,
    // serial = both eyes and their FOVs are complete and compositor-admissible.
    constinit std::atomic<uint64_t> g_halo2SynchronousPairToken{};
    constinit std::atomic<uint32_t> g_halo2SynchronousPairGeneration{};
    constinit std::atomic<uint32_t> g_halo2SynchronousPairResourceEpoch{};
    constinit std::atomic<uint64_t> g_halo2SynchronousTargetSerial{};
    constinit std::atomic<ID3D11RenderTargetView*> g_halo2SynchronousFinalRtv{};
    constinit std::atomic<ID3D11RenderTargetView*> g_halo2SynchronousSceneRtv{};
} // namespace halo2_frame_bridge
namespace
{
    // The last complete pair the compositor accepted, as rendered: poses,
    // covers, serial and time. Render-thread-owned (SubmitPreparedFrame).
    struct Halo2LastPair
    {
        bool valid = false;
        uint32_t generation = 0;
        uint64_t serial = 0;
        uint64_t tickMs = 0;
        XrPosef pose[2]{};
        float halfX[2]{};
        float halfY[2]{};
    };
    constinit Halo2LastPair g_halo2LastPair{};
    constexpr uint64_t kHalo2RepeatPairMaxAgeMs = 150;
} // namespace
namespace halo2_frame_bridge
{
    constinit std::atomic<uint64_t> g_halo2XrPairsRepeated{};
} // namespace halo2_frame_bridge
namespace
{
    // Draw census counters (see VR_Halo2NoteDraw).
    constinit std::atomic<uint64_t> g_halo2DrawsInsideEye[2]{};
    constinit std::atomic<uint64_t> g_halo2DrawsOutsideEye{0};
    constinit std::atomic<uint64_t> g_halo2SynchronousEyeSerial[2]{};
    constinit std::atomic<uint32_t> g_halo2SynchronousEyeGeneration[2]{};
    constinit std::atomic<uint32_t> g_halo2SynchronousEyeResourceEpoch[2]{};
    constinit std::atomic<float> g_halo2SynchronousEyeHalfFovX[2]{};
    constinit std::atomic<float> g_halo2SynchronousEyeHalfFovY[2]{};
    // The absolute OpenXR view pose each eye image was rendered
    // from, when the renderer named one (position xyz, orientation xyzw).
    constinit std::atomic<float> g_halo2SynchronousEyePose[2][7]{};
    constinit std::atomic<bool> g_halo2SynchronousEyePoseOwned[2]{};
    constinit std::atomic<uint64_t> g_halo2XrPairsPosedFromObserver{0};
} // namespace
namespace halo2_frame_bridge
{
    constinit std::atomic<uint64_t> g_halo2SynchronousCaptureSerial[2]{};
    constinit std::atomic<uint32_t> g_halo2SynchronousCaptureCount[2]{};
    constinit std::atomic<uint32_t> g_halo2SynchronousResourceEpoch{1};
} // namespace halo2_frame_bridge
namespace
{
    // Resource-free presentation history. The even sequence is the publication
    // anchor; odd means a writer owns the payload. Unlike pair/cache state this
    // exact integer-only stamp deliberately survives reset until a newer
    // process-monotonic prepared serial replaces it.
    constinit std::atomic<uint64_t> g_halo2SynchronousPresentationSequence{};
    constinit std::atomic<uint64_t> g_halo2SynchronousPresentationSerial{};
    constinit std::atomic<uint32_t> g_halo2SynchronousPresentationGeneration{};
    constinit std::atomic<uint32_t> g_halo2SynchronousPresentationDisposition{};
} // namespace
namespace halo2_frame_bridge
{
    constinit std::atomic<uint64_t> g_halo2XrPairsSubmitted{};
    constinit std::atomic<uint64_t> g_halo2XrPairsDropped{};
    constinit std::atomic<uint32_t> g_halo2LastDropReason{};
} // namespace halo2_frame_bridge
namespace
{
    constinit std::atomic<bool> g_halo2FirstPairAccepted{};
} // namespace
namespace halo2_frame_bridge
{
    constinit thread_local Halo2SynchronousEyeScope g_halo2SynchronousEyeScope{};
} // namespace halo2_frame_bridge
namespace
{
    // Capture-source learning. 0 = final-output slot 0x197EE58,
    // 1 = primary scene slot 0x197EE60, 2 = last target bound in the eye.
    constinit std::atomic<int> g_halo2CaptureSource{0};
    constinit ID3D11Texture2D* g_halo2ProbeCache[2][2]{}; // [probe slot][eye]
    constinit bool g_halo2ProbeHasImage[2][2]{};
    // Which candidate each probe slot copied, the pointer it copied,
    // how many candidates the eye offered, and where the rotation stands.
    // [probe slot][eye]: the candidate index AND the RTV copied, so the
    // 2 s comparison only compares two copies of the SAME target.
    constinit std::atomic<int> g_halo2ProbeCandidate[2][2]{{{-1}, {-1}}, {{-1}, {-1}}};
    constinit std::atomic<uintptr_t> g_halo2ProbeCandidatePtr[2][2]{};
    constinit std::atomic<uint64_t> g_halo2CaptureSourceFallbacks{0};
    // The probe used to demand the backbuffer's exact shape, so the
    // engine's own scene target (3788x2732, typeless) was never sampled -
    // every probe of it read "not sampled". A small centre box copied with
    // CopySubresourceRegion works for ANY shape and any 32-bit format.
    constexpr unsigned kHalo2ProbeBoxSize = 256;
    constinit ID3D11Texture2D* g_halo2ProbeBox[2][2]{};
    constinit DXGI_FORMAT g_halo2ProbeBoxFormat[2][2]{};
    constinit bool g_halo2ProbeBoxHasImage[2][2]{};
    constinit std::atomic<bool> g_halo2PrePairCreateFailed{false};
    constinit std::atomic<int> g_halo2CaptureCandidateCount{2};
    constinit std::atomic<int> g_halo2ProbeRotation{0};
    // The capture source as it stood BEFORE the pair's
    // first eye rendered, copied on request (one copy every pixel check, not
    // every frame). Comparing it with the two eye copies says whether the
    // engine's eye pass changed the target at all (capture-side problem) or
    // changed it to the same picture twice (camera-side problem).
    constinit ID3D11Texture2D* g_halo2PrePairCache = nullptr;
} // namespace
namespace halo2_frame_bridge
{
    constinit ID3D11Texture2D* g_halo2ScopeSource = nullptr;
} // namespace halo2_frame_bridge
namespace
{
    constinit ID3D11RenderTargetView* g_halo2ScopeSourceRtv = nullptr; // identity only; source owns lifetime
} // namespace
namespace halo2_frame_bridge
{
    constinit D3D11_TEXTURE2D_DESC g_halo2ScopeSourceDesc{};
} // namespace halo2_frame_bridge
namespace
{
    constinit uint32_t g_halo2ScopeGeneration = 0;
    constinit uint64_t g_halo2ScopeSerial = 0;
} // namespace
namespace halo2_frame_bridge
{
    constinit bool g_halo2ScopeCopyActive = false, g_halo2ScopeSourceBound = false;
} // namespace halo2_frame_bridge
namespace
{
    // Diagnostic: why the last Begin refused (0 none, 1 no context/cache/source,
    // 2 generation/serial, 3 title, 4 size/format) and how the last End ended
    // (0 none, 1 copied, 2 pass not completed, 3 source target never bound).
    constinit std::atomic<int> g_halo2ScopeBeginRefusal{0}, g_halo2ScopeOutcome{0};
    constinit std::atomic<bool> g_halo2PrePairWanted{false};
    // A one-shot eye-picture request (tick count it is due at).
    constinit std::atomic<uint64_t> g_halo2EyeDumpDueMs{0};
    // Full-resolution GPU->CPU validation is bounded capture-source
    // discovery, never steady-state telemetry. A source fallback rearms it.
    constinit std::atomic<bool> g_halo2HeavyValidationRetired{false};
    constinit std::atomic<int> g_halo2HeavyValidationSource{-1};
    constinit std::atomic<unsigned> g_halo2HeavyValidationChecks{0};
    constinit std::atomic<bool> g_halo2PrePairHasImage{false};
    constinit std::atomic<uint64_t> g_halo2PrePairSerial{0};
    constinit D3D11_TEXTURE2D_DESC g_halo2ProbeDesc{};
    constexpr uint32_t kHalo2RenderSnapshotWriter = 0x80000000u;
    constexpr uint32_t kHalo2RenderSnapshotInvalid = 0xFFFFFFFFu;
    constinit Halo2VrRenderSnapshot g_halo2RenderSnapshots[2]{};
    constinit std::atomic<uint32_t> g_halo2RenderSnapshotStates[2]{};
    constinit std::atomic<uint32_t> g_halo2RenderSnapshotIndex{kHalo2RenderSnapshotInvalid};
    static_assert(std::is_trivially_copyable_v<Halo2VrRenderSnapshot>);
} // namespace
namespace halo2_frame_bridge
{
    // Invalidate-before-write publication. Readers accept the three duration
    // fields only when both serial reads agree with the exact prepared frame.
    constinit std::atomic<uint64_t> g_halo2PreparedCadenceSerial{};
} // namespace halo2_frame_bridge
namespace
{
    constinit std::atomic<uint64_t> g_halo2PreparedTargetPeriodNs{};
    constinit std::atomic<uint64_t> g_halo2PreparedDisplayDeltaNs{};
    // Where do the classic draws happen relative to the per-eye
    // render_view scopes? Zero inside and thousands outside means the world
    // is submitted AFTER render_view returns, which is what identical eye
    // copies plus a stock-FOV picture look like.
    void ReportHalo2DrawCensus()
    {
        static uint64_t lastInside[2]{};
        static uint64_t lastOutside = 0;
        const uint64_t inside0 = g_halo2DrawsInsideEye[0].load(std::memory_order_relaxed);
        const uint64_t inside1 = g_halo2DrawsInsideEye[1].load(std::memory_order_relaxed);
        const uint64_t outside = g_halo2DrawsOutsideEye.load(std::memory_order_relaxed);
        const uint64_t dInside0 = inside0 - lastInside[0];
        const uint64_t dInside1 = inside1 - lastInside[1];
        const uint64_t dOutside = outside - lastOutside;
        lastInside[0] = inside0;
        lastInside[1] = inside1;
        lastOutside = outside;
        if (!dInside0 && !dInside1 && !dOutside)
            return;
        LOG("Halo 2 draw census (last 2s): DrawIndexed/Draw inside eye 0 = "
            "%llu, inside eye 1 = %llu, outside any eye scope = %llu",
            static_cast<unsigned long long>(dInside0), static_cast<unsigned long long>(dInside1),
            static_cast<unsigned long long>(dOutside));
    }

    // Halo 2 acceptance is "two different images from two different cameras",
    // not "our hooks ran". This full-resolution synchronous readback is
    // bounded to capture-source discovery, then retires it. A source fallback
    // or an explicit diagnostic request rearms it.
    void ReportHalo2DrawCensus();
    // Forget a learned Halo 2 capture source (manual VR recovery, so "Retry VR" can clear a bad lock).
    void ResetHalo2CaptureSourceLocked(const char* reason)
    {
        const int was = g_halo2CaptureSource.exchange(0, std::memory_order_relaxed);
        g_halo2ProbeRotation.store(0, std::memory_order_relaxed);
        g_halo2HeavyValidationSource.store(0, std::memory_order_relaxed);
        g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
        g_halo2HeavyValidationRetired.store(false, std::memory_order_release);
        LOG("Halo 2 capture source reset to the final-output slot (%s; was #%d)", reason, was);
    }
} // namespace
namespace halo2_frame_bridge
{
    void ValidateHalo2EyePairPeriodic()
    {
        static ID3D11Texture2D* staging[2]{};
        static D3D11_TEXTURE2D_DESC stagingDesc{};
        static uint64_t lastCheckMs = 0;
        static unsigned identicalRuns = 0;
        const uint64_t nowMs = GetTickCount64();
        // Track how long the runtime mode has been stable (see the source-switch decision below).
        static RuntimeMode lastValidationMode = RuntimeMode::Shell;
        static uint64_t validationModeSinceMs = 0;
        {
            const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
            if (mode != lastValidationMode)
            {
                lastValidationMode = mode;
                validationModeSinceMs = nowMs;
            }
        }
        const bool steadyGameplay =
            (lastValidationMode == RuntimeMode::Gameplay || lastValidationMode == RuntimeMode::Vehicle ||
                lastValidationMode == RuntimeMode::Turret) &&
            nowMs - validationModeSinceMs >= 3000;
        const int sourceBefore = g_halo2CaptureSource.load(std::memory_order_relaxed);
        const int trackedSource = g_halo2HeavyValidationSource.load(std::memory_order_relaxed);
        if (sourceBefore != trackedSource)
        {
            g_halo2HeavyValidationSource.store(sourceBefore, std::memory_order_relaxed);
            g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
            g_halo2HeavyValidationRetired.store(false, std::memory_order_release);
        }
        const bool explicitDumpPending = g_halo2EyeDumpDueMs.load(std::memory_order_acquire) != 0;
        if (g_halo2HeavyValidationRetired.load(std::memory_order_acquire) && !explicitDumpPending)
        {
            return;
        }
        if (nowMs - lastCheckMs < 500)
            return;
        lastCheckMs = nowMs;
        ReportHalo2DrawCensus();
        if (!g_device || !g_context || !g_eyeHasImage[0] || !g_eyeHasImage[1] || !g_eyeCache[0] || !g_eyeCache[1])
        {
            return;
        }
        if (g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            g_eyeCacheDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        {
            static bool loggedFormat = false;
            if (!loggedFormat)
            {
                loggedFormat = true;
                LOG("Halo 2 eye-pair pixel check: unsupported eye format %u; "
                    "the check is disabled for this shape",
                    static_cast<unsigned>(g_eyeCacheDesc.Format));
            }
            return;
        }
        if (!staging[0] || !staging[1] || stagingDesc.Width != g_eyeCacheDesc.Width ||
            stagingDesc.Height != g_eyeCacheDesc.Height || stagingDesc.Format != g_eyeCacheDesc.Format)
        {
            for (auto*& texture : staging)
            {
                if (texture)
                    texture->Release();
                texture = nullptr;
            }
            D3D11_TEXTURE2D_DESC desc = g_eyeCacheDesc;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging[0])) ||
                FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging[1])))
            {
                for (auto*& texture : staging)
                {
                    if (texture)
                        texture->Release();
                    texture = nullptr;
                }
                LOG("Halo 2 eye-pair pixel check: staging allocation failed "
                    "for %ux%u",
                    desc.Width, desc.Height);
                return;
            }
            stagingDesc = desc;
        }

        g_context->CopyResource(staging[0], g_eyeCache[0]);
        g_context->CopyResource(staging[1], g_eyeCache[1]);
        D3D11_MAPPED_SUBRESOURCE mapped[2]{};
        const HRESULT hr0 = g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]);
        const HRESULT hr1 = g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]);
        // The probe and pre-pair comparisons below unmap and re-map these;
        // the final Unmap follows the LAST map result, not the first.
        bool stagingMapped[2] = {SUCCEEDED(hr0), SUCCEEDED(hr1)};
        if (SUCCEEDED(hr0) && SUCCEEDED(hr1))
        {
            unsigned long long rgbDelta = 0;
            unsigned samples = 0, changed = 0, lit = 0;
            const unsigned step = 16;
            for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
            {
                const auto* left = static_cast<const unsigned char*>(mapped[0].pData) + y * mapped[0].RowPitch;
                const auto* right = static_cast<const unsigned char*>(mapped[1].pData) + y * mapped[1].RowPitch;
                for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                {
                    const unsigned o = x * 4;
                    unsigned pixelDelta = 0;
                    unsigned sum = 0;
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const int d = (int)left[o + c] - (int)right[o + c];
                        pixelDelta += (unsigned)(d < 0 ? -d : d);
                        sum += left[o + c];
                    }
                    rgbDelta += pixelDelta;
                    if (pixelDelta > 12)
                        ++changed;
                    if (sum > 24)
                        ++lit;
                    ++samples;
                }
            }
            const double meanChannelDelta = samples ? (double)rgbDelta / (samples * 3.0) : 0.0;
            const double changedPercent = samples ? (100.0 * changed / samples) : 0.0;
            const double litPercent = samples ? (100.0 * lit / samples) : 0.0;
            // The first-person weapon's disparity, measured. The
            // lower-right box (where the gun is) and an upper world band are
            // each searched for the horizontal shift that best matches eye 1
            // to eye 0. Positive = eye 1's content sits to the RIGHT of eye
            // 0's, which for a near object is the wrong way round (eye 0 is
            // the left eye); the world band gives the sign a true stereo
            // pair has. Sampled every 16 px; once every 2 s.
            auto bestShift = [&](unsigned x0, unsigned x1, unsigned y0, unsigned y1, int range, int stride,
                                 double& bestMatch, double& zeroMatch) -> int
            {
                int best = 0;
                bestMatch = 1.0e9;
                zeroMatch = 0.0;
                const unsigned W = g_eyeCacheDesc.Width;
                for (int dx = -range; dx <= range; dx += stride)
                {
                    unsigned long long acc = 0;
                    unsigned n = 0;
                    for (unsigned y = y0; y < y1; y += 16)
                    {
                        const auto* l = static_cast<const unsigned char*>(mapped[0].pData) + y * mapped[0].RowPitch;
                        const auto* r = static_cast<const unsigned char*>(mapped[1].pData) + y * mapped[1].RowPitch;
                        for (unsigned x = x0; x < x1; x += 16)
                        {
                            const int xr = static_cast<int>(x) + dx;
                            if (xr < 0 || xr >= static_cast<int>(W))
                                continue;
                            const unsigned o = x * 4;
                            const unsigned p = static_cast<unsigned>(xr) * 4;
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                const int d = (int)l[o + c] - (int)r[p + c];
                                acc += (unsigned)(d < 0 ? -d : d);
                            }
                            ++n;
                        }
                    }
                    const double m = n ? (double)acc / (n * 3.0) : 1.0e9;
                    if (dx == 0)
                        zeroMatch = m;
                    if (m < bestMatch)
                    {
                        bestMatch = m;
                        best = dx;
                    }
                }
                return best;
            };
            const unsigned Wd = g_eyeCacheDesc.Width;
            const unsigned Hd = g_eyeCacheDesc.Height;
            double weaponMatch = 0.0, weaponZero = 0.0, worldMatch = 0.0, worldZero = 0.0;
            const int weaponShift =
                bestShift(Wd * 45 / 100, Wd * 80 / 100, Hd * 50 / 100, Hd * 95 / 100, 400, 8, weaponMatch, weaponZero);
            const int worldShift =
                bestShift(Wd * 30 / 100, Wd * 70 / 100, Hd * 8 / 100, Hd * 30 / 100, 64, 4, worldMatch, worldZero);
            // An explicitly requested picture, quarter size, next to
            // the log (HaloMCCVR-halo2-eye0.bmp / eye1.bmp). "Cropped" and
            // "goggles" are judged by looking at the frame the mod actually
            // published, not by counters. Staging is already mapped here.
            {
                const uint64_t dueMs = g_halo2EyeDumpDueMs.load(std::memory_order_acquire);
                const bool requested = dueMs != 0 && nowMs >= dueMs;
                if (requested && LogDirectory()[0])
                {
                    g_halo2EyeDumpDueMs.store(0, std::memory_order_release);
                    const bool bgr = g_eyeCacheDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                                     g_eyeCacheDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                    const unsigned scale = 4;
                    const unsigned w = g_eyeCacheDesc.Width / scale;
                    const unsigned h = g_eyeCacheDesc.Height / scale;
                    for (int eye = 0; eye < 2 && w && h; ++eye)
                    {
                        wchar_t path[MAX_PATH];
                        _snwprintf_s(path, _TRUNCATE, L"%sHaloMCCVR-halo2-eye%d.bmp", LogDirectory(), eye);
                        FILE* file = _wfsopen(path, L"wb", _SH_DENYNO);
                        if (!file)
                            continue;
                        const uint32_t rowBytes = (w * 3 + 3) & ~3u;
                        const uint32_t imageBytes = rowBytes * h;
                        uint8_t header[54] = {'B', 'M'};
                        auto put32 = [&](size_t at, uint32_t v) { std::memcpy(header + at, &v, 4); };
                        auto put16 = [&](size_t at, uint16_t v) { std::memcpy(header + at, &v, 2); };
                        put32(2, 54 + imageBytes);
                        put32(10, 54);
                        put32(14, 40);
                        put32(18, w);
                        put32(22, h);
                        put16(26, 1);
                        put16(28, 24);
                        put32(34, imageBytes);
                        fwrite(header, 1, sizeof(header), file);
                        std::vector<uint8_t> row(rowBytes, 0);
                        const auto* base = static_cast<const unsigned char*>(mapped[eye].pData);
                        for (unsigned y = h; y-- > 0;)
                        {
                            const auto* src = base + (y * scale) * mapped[eye].RowPitch;
                            for (unsigned x = 0; x < w; ++x)
                            {
                                const unsigned o = x * scale * 4;
                                row[x * 3 + 0] = bgr ? src[o + 0] : src[o + 2];
                                row[x * 3 + 1] = src[o + 1];
                                row[x * 3 + 2] = bgr ? src[o + 2] : src[o + 0];
                            }
                            fwrite(row.data(), 1, rowBytes, file);
                        }
                        fclose(file);
                    }
                    static bool loggedDump = false;
                    if (!loggedDump)
                    {
                        loggedDump = true;
                        LOG("Halo 2 eye frames dumped on explicit request as "
                            "HaloMCCVR-halo2-eye0.bmp / eye1.bmp next to the log "
                            "(%ux%u, quarter size of the %ux%u capture)",
                            w, h, g_eyeCacheDesc.Width, g_eyeCacheDesc.Height);
                    }
                }
            }
            const bool identical = changed == 0;
            identicalRuns = identical ? identicalRuns + 1 : 0;
            // Compare the probe candidates the same way. If the
            // published pair is identical while a probe pair differs, the
            // probe is the target that carries per-eye content: switch.
            const int currentSource = g_halo2CaptureSource.load(std::memory_order_relaxed);
            double probeChanged[2] = {-1.0, -1.0};
            bool boxCompared[2] = {false, false};
            // The shape-agnostic centre box. Any candidate the
            // full-size copy could not take is compared here instead, so
            // the engine's own scene target is finally sampled.
            for (int probe = 0; probe < 2; ++probe)
            {
                if (!g_halo2ProbeBoxHasImage[probe][0] || !g_halo2ProbeBoxHasImage[probe][1] ||
                    !g_halo2ProbeBox[probe][0] || !g_halo2ProbeBox[probe][1] ||
                    g_halo2ProbeBoxFormat[probe][0] != g_halo2ProbeBoxFormat[probe][1])
                {
                    continue;
                }
                D3D11_MAPPED_SUBRESOURCE bm[2]{};
                if (FAILED(g_context->Map(g_halo2ProbeBox[probe][0], 0, D3D11_MAP_READ, 0, &bm[0])))
                    continue;
                if (FAILED(g_context->Map(g_halo2ProbeBox[probe][1], 0, D3D11_MAP_READ, 0, &bm[1])))
                {
                    g_context->Unmap(g_halo2ProbeBox[probe][0], 0);
                    continue;
                }
                unsigned bSamples = 0, bChanged = 0;
                for (unsigned y = 0; y < kHalo2ProbeBoxSize; y += 2)
                {
                    const auto* l = static_cast<const unsigned char*>(bm[0].pData) + y * bm[0].RowPitch;
                    const auto* r = static_cast<const unsigned char*>(bm[1].pData) + y * bm[1].RowPitch;
                    for (unsigned x = 0; x < kHalo2ProbeBoxSize; x += 2)
                    {
                        const unsigned o = x * 4;
                        unsigned d = 0;
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            const int dd = (int)l[o + c] - (int)r[o + c];
                            d += (unsigned)(dd < 0 ? -dd : dd);
                        }
                        if (d > 12)
                            ++bChanged;
                        ++bSamples;
                    }
                }
                g_context->Unmap(g_halo2ProbeBox[probe][0], 0);
                g_context->Unmap(g_halo2ProbeBox[probe][1], 0);
                if (bSamples)
                {
                    probeChanged[probe] = 100.0 * bChanged / bSamples;
                    boxCompared[probe] = true;
                }
            }
            if (staging[0] && staging[1])
            {
                for (int probe = 0; probe < 2; ++probe)
                {
                    if (!g_halo2ProbeHasImage[probe][0] || !g_halo2ProbeHasImage[probe][1] ||
                        !g_halo2ProbeCache[probe][0] || !g_halo2ProbeCache[probe][1] ||
                        g_halo2ProbeDesc.Width != g_eyeCacheDesc.Width ||
                        g_halo2ProbeDesc.Height != g_eyeCacheDesc.Height ||
                        g_halo2ProbeDesc.Format != g_eyeCacheDesc.Format ||
                        // Both eyes must have copied the SAME target, or the
                        // "pair" compares two different textures.
                        g_halo2ProbeCandidate[probe][0].load(std::memory_order_relaxed) !=
                            g_halo2ProbeCandidate[probe][1].load(std::memory_order_relaxed) ||
                        g_halo2ProbeCandidatePtr[probe][0].load(std::memory_order_relaxed) !=
                            g_halo2ProbeCandidatePtr[probe][1].load(std::memory_order_relaxed))
                    {
                        continue;
                    }
                    g_context->Unmap(staging[0], 0);
                    g_context->Unmap(staging[1], 0);
                    g_context->CopyResource(staging[0], g_halo2ProbeCache[probe][0]);
                    g_context->CopyResource(staging[1], g_halo2ProbeCache[probe][1]);
                    D3D11_MAPPED_SUBRESOURCE pm[2]{};
                    if (FAILED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &pm[0])) ||
                        FAILED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &pm[1])))
                    {
                        // Leave both mapped-state flags consistent for the
                        // unmap below by remapping the eye caches.
                        g_context->CopyResource(staging[0], g_eyeCache[0]);
                        g_context->CopyResource(staging[1], g_eyeCache[1]);
                        stagingMapped[0] = SUCCEEDED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]));
                        stagingMapped[1] = SUCCEEDED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]));
                        break;
                    }
                    unsigned pSamples = 0, pChanged = 0;
                    for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
                    {
                        const auto* l = static_cast<const unsigned char*>(pm[0].pData) + y * pm[0].RowPitch;
                        const auto* r = static_cast<const unsigned char*>(pm[1].pData) + y * pm[1].RowPitch;
                        for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                        {
                            const unsigned o = x * 4;
                            unsigned d = 0;
                            for (unsigned c = 0; c < 3; ++c)
                            {
                                const int dd = (int)l[o + c] - (int)r[o + c];
                                d += (unsigned)(dd < 0 ? -dd : dd);
                            }
                            if (d > 12)
                                ++pChanged;
                            ++pSamples;
                        }
                    }
                    probeChanged[probe] = pSamples ? 100.0 * pChanged / pSamples : 0.0;
                    g_context->Unmap(staging[0], 0);
                    g_context->Unmap(staging[1], 0);
                    boxCompared[probe] = false;
                    g_context->CopyResource(staging[0], g_eyeCache[0]);
                    g_context->CopyResource(staging[1], g_eyeCache[1]);
                    stagingMapped[0] = SUCCEEDED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]));
                    stagingMapped[1] = SUCCEEDED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]));
                }
                // Which candidate each probe slot actually copied
                // this round (the rotation walks the whole candidate set, two
                // at a time, so the probe cost per eye is unchanged).
                // A slot whose two eyes copied different targets reports -1
                // (its comparison was skipped above).
                int probeCandidate[2] = {-1, -1};
                for (int probe = 0; probe < 2; ++probe)
                {
                    const int c0 = g_halo2ProbeCandidate[probe][0].load(std::memory_order_relaxed);
                    const int c1 = g_halo2ProbeCandidate[probe][1].load(std::memory_order_relaxed);
                    const uintptr_t p0 = g_halo2ProbeCandidatePtr[probe][0].load(std::memory_order_relaxed);
                    const uintptr_t p1 = g_halo2ProbeCandidatePtr[probe][1].load(std::memory_order_relaxed);
                    probeCandidate[probe] = (c0 == c1 && p0 == p1) ? c0 : -1;
                }
                const int candidateCount = g_halo2CaptureCandidateCount.load(std::memory_order_relaxed);
                auto candidateName = [](int candidate, uintptr_t pointer, char* out, size_t bytes)
                {
                    if (candidate < 0)
                        _snprintf_s(out, bytes, _TRUNCATE, "-");
                    else if (candidate == 0)
                        _snprintf_s(out, bytes, _TRUNCATE, "final-output slot 0x197EE58");
                    else if (candidate == 1)
                        _snprintf_s(out, bytes, _TRUNCATE, "primary scene slot 0x197EE60");
                    else
                        _snprintf_s(out, bytes, _TRUNCATE, "target #%d bound in the eye (%p)", candidate - 2,
                            reinterpret_cast<void*>(pointer));
                };
                char names[3][96];
                candidateName(currentSource, 0, names[0], sizeof(names[0]));
                candidateName(probeCandidate[0], g_halo2ProbeCandidatePtr[0][1].load(std::memory_order_relaxed),
                    names[1], sizeof(names[1]));
                candidateName(probeCandidate[1], g_halo2ProbeCandidatePtr[1][1].load(std::memory_order_relaxed),
                    names[2], sizeof(names[2]));
                LOG("Halo 2 capture probes: source=%s; %s pair changed %.1f%%%s; "
                    "%s pair changed %.1f%%%s (negative = not sampled); %d "
                    "candidates this eye, rotation at %d",
                    names[0], names[1], probeChanged[0], boxCompared[0] ? " (centre box)" : "", names[2],
                    probeChanged[1], boxCompared[1] ? " (centre box)" : "", candidateCount,
                    g_halo2ProbeRotation.load(std::memory_order_relaxed));
                // Both eyes are legitimately identical during a loading blip or a cutscene, so an
                // identical pair only means "wrong source" in steady gameplay. Switching on a transition once
                // locked capture onto the G-buffer normals for the rest of the session.
                if (identical && identicalRuns >= 2 && !steadyGameplay)
                {
                    static uint64_t lastIgnoredLogMs = 0;
                    if (nowMs - lastIgnoredLogMs >= 10000)
                    {
                        lastIgnoredLogMs = nowMs;
                        LOG("Halo 2 capture source kept: identical pair seen outside steady gameplay "
                            "(mode %u for %llu ms); not treated as a wrong source",
                            static_cast<unsigned>(lastValidationMode),
                            static_cast<unsigned long long>(nowMs - validationModeSinceMs));
                    }
                }
                else if (identical && identicalRuns >= 2)
                {
                    int best = -1;
                    double bestChanged = 5.0;
                    for (int probe = 0; probe < 2; ++probe)
                        if (probeChanged[probe] > bestChanged && probeCandidate[probe] >= 0)
                        {
                            best = probeCandidate[probe];
                            bestChanged = probeChanged[probe];
                        }
                    if (best >= 0)
                    {
                        char bestName[96];
                        candidateName(best,
                            g_halo2ProbeCandidatePtr[probeChanged[0] == bestChanged ? 0 : 1][1].load(
                                std::memory_order_relaxed),
                            bestName, sizeof(bestName));
                        g_halo2CaptureSource.store(best, std::memory_order_relaxed);
                        g_halo2ProbeRotation.store(0, std::memory_order_relaxed);
                        LOG("Halo 2 capture source SWITCHED to %s: the published "
                            "pair was identical twice while that target's pair "
                            "differed by %.1f%% of samples - it is the one the "
                            "engine draws per eye",
                            bestName, bestChanged);
                        identicalRuns = 0;
                    }
                    else
                    {
                        // Nothing in this pair of candidates carried per-eye
                        // content: walk the rotation on so the next round
                        // probes the next two, instead of re-copying the same
                        // two targets forever.
                        int filled = 0;
                        for (int probe = 0; probe < 2; ++probe)
                            if (probeCandidate[probe] >= 0)
                                ++filled;
                        g_halo2ProbeRotation.store(
                            Halo2NextProbeRotation(
                                g_halo2ProbeRotation.load(std::memory_order_relaxed), candidateCount, filled),
                            std::memory_order_relaxed);
                    }
                }
            }
            // The pre-pair copy against each eye. Both
            // eyes equal to the pre-pair picture means the eye pass never
            // changed the captured target; both eyes different from it but
            // equal to each other means two passes drew the same picture.
            double prePairVsEye[2] = {-1.0, -1.0};
            if (staging[0] && staging[1] && g_halo2PrePairCache &&
                g_halo2PrePairHasImage.load(std::memory_order_acquire))
            {
                D3D11_TEXTURE2D_DESC preDesc{};
                g_halo2PrePairCache->GetDesc(&preDesc);
                if (preDesc.Width == g_eyeCacheDesc.Width && preDesc.Height == g_eyeCacheDesc.Height &&
                    preDesc.Format == g_eyeCacheDesc.Format)
                {
                    g_context->Unmap(staging[0], 0);
                    g_context->Unmap(staging[1], 0);
                    g_context->CopyResource(staging[0], g_halo2PrePairCache);
                    for (int eye = 0; eye < 2; ++eye)
                    {
                        g_context->CopyResource(staging[1], g_eyeCache[eye]);
                        D3D11_MAPPED_SUBRESOURCE pm[2]{};
                        if (FAILED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &pm[0])))
                            break;
                        if (FAILED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &pm[1])))
                        {
                            g_context->Unmap(staging[0], 0);
                            break;
                        }
                        unsigned pSamples = 0, pChanged = 0;
                        for (unsigned y = step / 2; y < g_eyeCacheDesc.Height; y += step)
                        {
                            const auto* l = static_cast<const unsigned char*>(pm[0].pData) + y * pm[0].RowPitch;
                            const auto* r = static_cast<const unsigned char*>(pm[1].pData) + y * pm[1].RowPitch;
                            for (unsigned x = step / 2; x < g_eyeCacheDesc.Width; x += step)
                            {
                                const unsigned o = x * 4;
                                unsigned d = 0;
                                for (unsigned c = 0; c < 3; ++c)
                                {
                                    const int dd = (int)l[o + c] - (int)r[o + c];
                                    d += (unsigned)(dd < 0 ? -dd : dd);
                                }
                                if (d > 12)
                                    ++pChanged;
                                ++pSamples;
                            }
                        }
                        prePairVsEye[eye] = pSamples ? 100.0 * pChanged / pSamples : 0.0;
                        g_context->Unmap(staging[0], 0);
                        g_context->Unmap(staging[1], 0);
                    }
                    g_context->CopyResource(staging[0], g_eyeCache[0]);
                    g_context->CopyResource(staging[1], g_eyeCache[1]);
                    stagingMapped[0] = SUCCEEDED(g_context->Map(staging[0], 0, D3D11_MAP_READ, 0, &mapped[0]));
                    stagingMapped[1] = SUCCEEDED(g_context->Map(staging[1], 0, D3D11_MAP_READ, 0, &mapped[1]));
                }
                g_halo2PrePairHasImage.store(false, std::memory_order_release);
            }
            g_halo2PrePairWanted.store(true, std::memory_order_release);
            const int sourceAfter = g_halo2CaptureSource.load(std::memory_order_relaxed);
            const int validationSource = g_halo2HeavyValidationSource.load(std::memory_order_relaxed);
            if (sourceAfter != validationSource)
            {
                // The validator just learned a different source. Give that
                // source one fresh comparison before retiring discovery.
                g_halo2HeavyValidationSource.store(sourceAfter, std::memory_order_relaxed);
                g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
            }
            else
            {
                const unsigned checks = g_halo2HeavyValidationChecks.fetch_add(1, std::memory_order_relaxed) + 1;
                if (!identical || checks >= 6)
                {
                    g_halo2HeavyValidationRetired.store(true, std::memory_order_release);
                    LOG("Halo 2 performance gate: heavy eye validation "
                        "retired after source %d was %s; periodic GPU readback "
                        "and alternate-target probes are dormant",
                        sourceAfter, identical ? "bounded" : "proven distinct");
                }
            }
            if (identical)
            {
                LOG("Halo 2 eye-pair pixel check: IDENTICAL eyes (0/%u "
                    "samples differ, %.1f%% lit) - this is FAKE stereo: the "
                    "engine produced the same image for both eye passes%s; "
                    "captured target before the pair vs eye 0: %.1f%% changed, "
                    "vs eye 1: %.1f%% changed (negative = not sampled; ~0 = "
                    "the eye pass never wrote the captured target)",
                    samples, litPercent,
                    identicalRuns >= 3 ? "; persistent, the per-eye camera is NOT reaching "
                                         "the renderer"
                                       : "",
                    prePairVsEye[0], prePairVsEye[1]);
            }
            else
            {
                LOG("Halo 2 eye-pair pixel check: DISTINCT eyes, mean RGB "
                    "delta=%.3f, changed samples=%.1f%% (%u/%u), %.1f%% lit "
                    "- true per-eye rendering; weapon box (lower right) eye 1 "
                    "vs eye 0 best shift %+d px (match %.1f, at zero %.1f), "
                    "world band best shift %+d px (match %.1f, at zero %.1f) "
                    "- a near object must shift the SAME way as the world, "
                    "further",
                    meanChannelDelta, changedPercent, changed, samples, litPercent, weaponShift, weaponMatch,
                    weaponZero, worldShift, worldMatch, worldZero);
            }
        }
        else
        {
            LOG("Halo 2 eye-pair pixel check: staging map failed "
                "(0x%08X, 0x%08X)",
                (unsigned)hr0, (unsigned)hr1);
        }
        if (stagingMapped[0])
            g_context->Unmap(staging[0], 0);
        if (stagingMapped[1])
            g_context->Unmap(staging[1], 0);
    }
    void HandleHalo2StockScreenFailure(const char* failureReason, Halo2StockScreenXrOwnership ownership) noexcept
    {
        const char* reason = failureReason ? failureReason : "unspecified failure";
        if (Halo2StockScreenNeedsFatalDrain(ownership))
        {
            LOG("Halo 2 stock-screen transaction FATAL: %s; XR image/frame "
                "ownership is unresolved, entering OpenXR session recovery",
                reason);
            g_abortFrameForSwapchainFailure = true;
            EnterFrameWaitFatalDrain(failureReason);
            return;
        }
        LOG("Halo 2 stock-screen transaction dropped: %s; no XR image "
            "ownership is unresolved, keeping the OpenXR session active",
            reason);
    }
} // namespace halo2_frame_bridge
namespace
{
    bool RequireHalo2StockScreenCompletion(XrResult result, const char* stage, const char* failureReason,
        Halo2StockScreenXrOwnership ownershipOnFailure) noexcept
    {
        if (result == XR_SUCCESS)
            return true;
        LOG("Halo 2 stock-screen transaction %s did not complete: %s", stage ? stage : "OpenXR operation",
            XrStr(result));
        HandleHalo2StockScreenFailure(failureReason, ownershipOnFailure);
        return false;
    }
    void ReportHalo2SynchronousDisposition(bool synchronousPresentationActive,
        Halo2SynchronousPresentationDecision decision, uint64_t preparedSerial) noexcept
    {
        // SubmitPreparedFrame is render-thread-owned, so these diagnostics need
        // no synchronization. Log the transition immediately, then only once
        // per two seconds while a non-player/cinematic path continues to omit
        // the exact player-window pair.
        static Halo2SynchronousPresentationDecision previous = Halo2SynchronousPresentationDecision::SharedDefault;
        static uint64_t lastStateLogMs = 0;
        const uint64_t nowMs = GetTickCount64();
        const bool reportable = decision == Halo2SynchronousPresentationDecision::StockScreen ||
                                decision == Halo2SynchronousPresentationDecision::Drop;
        if (reportable)
        {
            if (decision != previous || nowMs - lastStateLogMs >= 2000)
            {
                if (decision == Halo2SynchronousPresentationDecision::StockScreen)
                {
                    LOG("Halo 2 pre-stereo screen: prepared serial "
                        "%llu was not claimed by an eye render transaction; "
                        "submitting the stock screen-quad path from the "
                        "available game backbuffer (no stale/N-1 stereo-pair "
                        "reuse)",
                        static_cast<unsigned long long>(preparedSerial));
                }
                else
                {
                    LOG("Halo 2 frame-local drop: prepared serial %llu "
                        "was claimed immediately before its original eye render "
                        "and did not produce a live admissible pair; submitting "
                        "no world layer and not sampling "
                        "the unproven game backbuffer",
                        static_cast<unsigned long long>(preparedSerial));
                }
                lastStateLogMs = nowMs;
            }
            previous = decision;
            return;
        }

        const bool previousWasReportable = previous == Halo2SynchronousPresentationDecision::StockScreen ||
                                           previous == Halo2SynchronousPresentationDecision::Drop;
        if (!previousWasReportable)
        {
            previous = decision;
            return;
        }
        if (synchronousPresentationActive && (decision == Halo2SynchronousPresentationDecision::SynchronousStereo ||
                                                 decision == Halo2SynchronousPresentationDecision::RepeatLastPair))
        {
            if (previous == Halo2SynchronousPresentationDecision::Drop)
            {
                LOG("Halo 2 frame-local drop ended: prepared serial "
                    "%llu has a projection-ready exact-current-serial pair; "
                    "resuming synchronous stereo",
                    static_cast<unsigned long long>(preparedSerial));
            }
            else
            {
                LOG("Halo 2 pre-stereo screen ended: prepared serial "
                    "%llu has a projection-ready exact-current-serial pair; "
                    "resuming synchronous stereo",
                    static_cast<unsigned long long>(preparedSerial));
            }
        }
        else
        {
            LOG("Halo 2 %s ended: synchronous Halo 2 "
                "presentation is no longer active",
                previous == Halo2SynchronousPresentationDecision::Drop ? "drop" : "pre-stereo screen");
        }
        previous = decision;
        lastStateLogMs = 0;
    }
} // namespace
namespace halo2_frame_bridge
{
    bool PublishHalo2RenderSnapshot(uint64_t preparedSerial, bool padFresh) noexcept
    {
        if (!preparedSerial || !g_headPoseValid || g_views.size() != 2 ||
            TitleAdapter_GetActiveTitle() != GameTitle::Halo2)
        {
            return false;
        }
        const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo2);
        if (!generation)
            return false;

        Halo2VrRenderSnapshot next{};
        next.turnPadValid = padFresh && g_padState.valid;
        next.turnX = next.turnPadValid ? g_padState.turnX : 0.0f;
        next.leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        next.handAlignment = next.leftHanded && g_config.experimental_hand_alignment;
        next.preparedSerial = preparedSerial;
        next.predictedDisplayTimeNs = g_preparedFrame.state.predictedDisplayTime;
        const int64_t pendingSpaceChange = g_contactSpaceChangeAtNs.load(std::memory_order_acquire);
        if (!pendingSpaceChange || next.predictedDisplayTimeNs >= pendingSpaceChange)
            next.trackingSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
        SelectControllerFingerInputs(g_controllerFingerSample, preparedSerial, next.predictedDisplayTimeNs,
            next.trackingSpaceEpoch, next.leftHanded, padFresh, next.controllerFingers);
        const bool supportPhysicalFresh =
            next.leftHanded ? (padFresh && g_rightAimPoseValid) : (padFresh && g_leftAimPoseValid);
        const bool primaryPhysicalFresh =
            next.leftHanded ? (padFresh && g_leftAimPoseValid) : (padFresh && g_rightAimPoseValid);
        if (!supportPhysicalFresh)
            next.controllerFingers[0] = {};
        if (!primaryPhysicalFresh)
            next.controllerFingers[1] = {};
        next.predictedDisplayPeriodNs = g_preparedFrame.state.predictedDisplayPeriod > 0
                                            ? static_cast<uint64_t>(g_preparedFrame.state.predictedDisplayPeriod)
                                            : 0;
        next.predictedDisplayDeltaNs = g_preparedFrame.predictedDisplayDelta > 0
                                           ? static_cast<uint64_t>(g_preparedFrame.predictedDisplayDelta)
                                           : 0;
        next.generation = generation;
        AimPoseInputs independentInputs = CurrentAimPoseInputs(
            padFresh && g_rightAimPoseValid, g_rightAimPose, padFresh && g_leftAimPoseValid, g_leftAimPose);
        independentInputs.twoHandEnabled = false;
        const AimPoseResult independentAim = ComputeAimPose(independentInputs);
        next.independentRightAimValid = independentAim.valid;
        if (independentAim.valid)
        {
            next.independentRightAimOrientation[0] = independentAim.pose.orientation.x;
            next.independentRightAimOrientation[1] = independentAim.pose.orientation.y;
            next.independentRightAimOrientation[2] = independentAim.pose.orientation.z;
            next.independentRightAimOrientation[3] = independentAim.pose.orientation.w;
        }
        next.headOrientation[0] = g_headPose.orientation.x;
        next.headOrientation[1] = g_headPose.orientation.y;
        next.headOrientation[2] = g_headPose.orientation.z;
        next.headOrientation[3] = g_headPose.orientation.w;
        next.headPosition[0] = g_headPose.position.x;
        next.headPosition[1] = g_headPose.position.y;
        next.headPosition[2] = g_headPose.position.z;
        next.headPoseValid = true;
        // Publish the same immutable aim packet Reach and Halo 4 consume. A
        // failed action sync cannot leak an older controller pose into a new
        // Halo 2 prepared serial.
        const bool rightPoseFresh = padFresh && g_rightAimPoseValid;
        const bool leftPoseFresh = padFresh && g_leftAimPoseValid;
        const AimPoseInputs labAimInputs =
            CurrentFrameStockAimPoseInputs(rightPoseFresh, g_rightAimPose, leftPoseFresh, g_leftAimPose);
        const AimPoseResult aim = ComputeAimPose(labAimInputs);
        next.rightAimValid = aim.valid;
        next.twoHandAimActive = aim.valid && aim.twoHandActive;
        if (aim.valid)
        {
            // Orientation only: the presentation surfaces consume the aim that
            // includes any active grab/release transition correction; the
            // position and the raw two-hand activity flag stay exactly as the
            // solver returned them. The Lab temporal presentation (VS-off
            // only) runs after the VS one; at most one is ever active.
            const XrPosef presentedAim =
                PresentedTwoHandLabTemporalPose(PresentedAimContinuityPose(aim.pose, preparedSerial), preparedSerial,
                    labAimInputs.twoHandLabGeneration);
            next.rightAimOrientation[0] = presentedAim.orientation.x;
            next.rightAimOrientation[1] = presentedAim.orientation.y;
            next.rightAimOrientation[2] = presentedAim.orientation.z;
            next.rightAimOrientation[3] = presentedAim.orientation.w;
            next.rightAimPosition[0] = presentedAim.position.x;
            next.rightAimPosition[1] = presentedAim.position.y;
            next.rightAimPosition[2] = presentedAim.position.z;
        }
        next.leftControllerValid = leftPoseFresh;
        if (leftPoseFresh)
        {
            next.leftControllerOrientation[0] = g_leftAimPose.orientation.x;
            next.leftControllerOrientation[1] = g_leftAimPose.orientation.y;
            next.leftControllerOrientation[2] = g_leftAimPose.orientation.z;
            next.leftControllerOrientation[3] = g_leftAimPose.orientation.w;
            next.leftControllerPosition[0] = g_leftAimPose.position.x;
            next.leftControllerPosition[1] = g_leftAimPose.position.y;
            next.leftControllerPosition[2] = g_leftAimPose.position.z;
        }
        for (int eye = 0; eye < 2; ++eye)
        {
            if (!VR_GetEyeViewOffset(eye, next.eyes[eye].position, next.eyes[eye].orientation) ||
                !VR_GetEyeFov(eye, next.eyes[eye].fov))
            {
                return false;
            }
            next.eyes[eye].fovValid = true;
            next.eyes[eye].absolutePosition[0] = g_views[eye].pose.position.x;
            next.eyes[eye].absolutePosition[1] = g_views[eye].pose.position.y;
            next.eyes[eye].absolutePosition[2] = g_views[eye].pose.position.z;
            next.eyes[eye].absoluteOrientation[0] = g_views[eye].pose.orientation.x;
            next.eyes[eye].absoluteOrientation[1] = g_views[eye].pose.orientation.y;
            next.eyes[eye].absoluteOrientation[2] = g_views[eye].pose.orientation.z;
            next.eyes[eye].absoluteOrientation[3] = g_views[eye].pose.orientation.w;
        }

        const uint32_t current = g_halo2RenderSnapshotIndex.load(std::memory_order_seq_cst);
        const uint32_t target = current < 2 ? 1u - current : 0u;
        uint32_t expectedState = 0;
        if (!g_halo2RenderSnapshotStates[target].compare_exchange_strong(
                expectedState, kHalo2RenderSnapshotWriter, std::memory_order_seq_cst, std::memory_order_seq_cst))
        {
            return false;
        }
        g_halo2RenderSnapshots[target] = next;
        g_halo2RenderSnapshotIndex.store(target, std::memory_order_seq_cst);
        g_halo2RenderSnapshotStates[target].store(0, std::memory_order_seq_cst);
        return true;
    }

    void PublishHalo2PreparedCadence(
        uint64_t preparedSerial, XrDuration targetPeriod, XrDuration predictedDisplayDelta) noexcept
    {
        g_halo2PreparedCadenceSerial.store(0, std::memory_order_release);
        g_halo2PreparedTargetPeriodNs.store(
            targetPeriod > 0 ? static_cast<uint64_t>(targetPeriod) : 0, std::memory_order_relaxed);
        g_halo2PreparedDisplayDeltaNs.store(
            predictedDisplayDelta > 0 ? static_cast<uint64_t>(predictedDisplayDelta) : 0, std::memory_order_relaxed);
        g_halo2PreparedCadenceSerial.store(preparedSerial, std::memory_order_release);
    }
    // Halo 2 strict stock screen: a frame that must not show stereo shows the game's
    // own backbuffer on the flat screen quad. Every XR step reports through
    // RequireHalo2StockScreenCompletion / HandleHalo2StockScreenFailure, and a failed
    // wait is left to the fatal session-recovery path (never released here).
    void SubmitHalo2StrictStockScreen(ID3D11Texture2D* backbuffer, const D3D11_TEXTURE2D_DESC& bd,
        bool pausedPresentation, XrCompositionLayerQuad& screenQuad, std::vector<XrCompositionLayerBaseHeader*>& layers,
        bool& halo2StrictStockScreenQueued)
    {
        if (!EnsureScreenChain(bd.Width, bd.Height))
        {
            HandleHalo2StockScreenFailure("screen swapchain creation failed", Halo2StockScreenXrOwnership::None);
        }
        else
        {
            uint32_t idx = 0;
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi2.timeout = 1000000000;
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            FLog("acquire H2 stock-screen image");
            const XrResult acquireResult = xrAcquireSwapchainImage(g_screenChain, &ai, &idx);
            if (RequireHalo2StockScreenCompletion(acquireResult, "acquire", "screen swapchain acquire did not complete",
                    Halo2StockScreenXrOwnership::AcquiredUnresolved))
            {
                const XrResult waitResult = xrWaitSwapchainImage(g_screenChain, &wi2);
                if (RequireHalo2StockScreenCompletion(waitResult, "wait", "screen swapchain wait did not complete",
                        Halo2StockScreenXrOwnership::AcquiredUnresolved))
                {
                    const bool indexValid = g_device && g_context && idx < g_screenImages.size();
                    const bool directCopy = VrBlitCanUseDirectCopy(bd.Width, bd.Height, bd.Format, bd.SampleDesc.Count,
                        g_screenW, g_screenH, static_cast<DXGI_FORMAT>(g_xrFormat));
                    ID3D11RenderTargetView* targetRtv = indexValid && !directCopy && idx < g_screenRtvs.size()
                                                            ? GetRtv(g_screenImages, g_screenRtvs, idx)
                                                            : nullptr;
                    const bool resourcesValid =
                        indexValid && VrBlitResourcesReady(backbuffer != nullptr, g_screenImages[idx] != nullptr,
                                          targetRtv != nullptr, directCopy);
                    bool blitComplete = false;
                    if (resourcesValid)
                    {
                        FLog("blit backbuffer -> H2 stock screen");
                        blitComplete = Blit(backbuffer, bd, g_screenImages[idx], g_screenW, g_screenH, targetRtv);
                    }
                    const XrResult releaseResult = xrReleaseSwapchainImage(g_screenChain, &ri);
                    const bool releaseComplete = RequireHalo2StockScreenCompletion(releaseResult, "release",
                        "screen swapchain release did not "
                        "complete",
                        Halo2StockScreenXrOwnership::AcquiredUnresolved);
                    if (releaseComplete && !resourcesValid)
                    {
                        HandleHalo2StockScreenFailure(directCopy ? "direct-copy path has no valid "
                                                                   "source or destination texture"
                                                                 : "shader path has no valid "
                                                                   "source, destination texture, "
                                                                   "or RTV",
                            Halo2StockScreenXrOwnership::Released);
                    }
                    else if (releaseComplete && !blitComplete)
                    {
                        HandleHalo2StockScreenFailure(
                            "stock backbuffer blit failed", Halo2StockScreenXrOwnership::Released);
                    }
                    else if (releaseComplete)
                    {
                        FLog("H2 stock-screen image released");
                        const bool headLock =
                            !g_haveCenter || pausedPresentation || (g_screenFollow.load() && Game_IsHeadTracking());
                        const XrSpace screenSpace = headLock ? g_viewSpace : g_localSpace;
                        if (screenSpace == XR_NULL_HANDLE)
                        {
                            HandleHalo2StockScreenFailure(
                                headLock ? "VIEW space is unavailable" : "LOCAL space is unavailable",
                                Halo2StockScreenXrOwnership::Released);
                        }
                        else
                        {
                            screenQuad = MakeQuad(g_screenChain, (int32_t)g_screenW, (int32_t)g_screenH,
                                g_config.screen_width_m, g_config.screen_distance_m, 0.0f, 0, headLock);
                            ApplyPauseWorldLock(screenQuad, pausedPresentation, g_config.screen_distance_m);
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&screenQuad));
                            halo2StrictStockScreenQueued = true;
                        }
                    }
                }
                // A failed wait may still own the acquired
                // image. Never release or acquire again here;
                // the fatal session-recovery path owns cleanup.
            }
        }
    }

    // Quarantine the Halo 2 generation whose durable presentation claim owns this
    // prepared serial. Returns false when no such claim exists.
    bool QuarantineHalo2PreparedFrame(Halo2StereoQuarantineReason reason)
    {
        Halo2SynchronousPresentationStamp failedPresentation{};
        if (!VR_Halo2GetSynchronousPresentationStamp(failedPresentation) ||
            failedPresentation.preparedSerial != g_preparedFrame.serial)
            return false;
        Halo2Stereo_RequestGenerationQuarantine(failedPresentation.generation, reason);
        return true;
    }

    // Halo 2 after xrEndFrame: a rejected strict stock screen or pair is reported/
    // quarantined; an accepted pair still current for this serial earns runtime
    // ownership (heartbeat + Gameplay) and is counted.
    void FinishHalo2Submission(bool halo2StrictStockScreenQueued, bool halo2ProjectionQueued, XrResult r)
    {
        if (halo2StrictStockScreenQueued && r != XR_SUCCESS)
        {
            LOG("Halo 2 stock-screen transaction xrEndFrame did not complete: %s", XrStr(r));
            HandleHalo2StockScreenFailure(
                "stock-screen xrEndFrame did not complete", Halo2StockScreenXrOwnership::FrameSubmissionUnresolved);
        }
        if (halo2ProjectionQueued && r != XR_SUCCESS)
            QuarantineHalo2PreparedFrame(Halo2StereoQuarantineReason::VrClaimedSubmissionFailed);
        if (halo2ProjectionQueued && r == XR_SUCCESS)
        {
            // Halo 2 runtime ownership is earned by a headset-admitted pair,
            // not by locating/installing/arming the title hook. Revalidate the
            // exact current serial after xrEndFrame so a concurrent reset or
            // title-generation transition cannot resurrect Gameplay.
            const uint32_t acceptedGeneration = TitleAdapter_GetGeneration(GameTitle::Halo2);
            float acceptedHalfX[2]{}, acceptedHalfY[2]{};
            const bool exactPairStillCurrent = TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
                                               VR_Halo2GetSynchronousHalfFovs(acceptedGeneration,
                                                   g_preparedFrame.serial, acceptedHalfX, acceptedHalfY);
            if (exactPairStillCurrent)
            {
                const uint64_t nowMs = GetTickCount64();
                TitleAdapter_PublishHeartbeat(GameTitle::Halo2, acceptedGeneration, nowMs);
                TitleAdapter_PublishMode(GameTitle::Halo2, acceptedGeneration, RuntimeMode::Gameplay);
                g_halo2XrPairsSubmitted.fetch_add(1, std::memory_order_relaxed);
                if (!g_halo2FirstPairAccepted.exchange(true, std::memory_order_acq_rel))
                {
                    LOG("Halo 2 simultaneous stereo + 6DOF active");
                    LOG("Halo 2 first exact-current-serial two-eye pair "
                        "accepted by xrEndFrame; no temporal eye reuse or "
                        "cadence division");
                }
            }
        }
    }

    // Halo 2: the pair just queued becomes the one a missed serial may re-present
    // (with its own poses and FOVs).
    void RememberHalo2Pair(uint32_t generation, const std::vector<XrCompositionLayerProjectionView>& projectionViews,
        const float halfX[2], const float halfY[2])
    {
        g_halo2LastPair.valid = true;
        g_halo2LastPair.generation = generation;
        g_halo2LastPair.serial = g_preparedFrame.serial;
        g_halo2LastPair.tickMs = GetTickCount64();
        for (int view = 0; view < 2; ++view)
        {
            g_halo2LastPair.pose[view] = projectionViews[view].pose;
            g_halo2LastPair.halfX[view] = halfX[view];
            g_halo2LastPair.halfY[view] = halfY[view];
        }
    }
    // Halo 2: quarantine the generation, invalidate the synchronous pair and count the drop.
    void DropIncompleteHalo2Frame(uint32_t halo2Generation, bool halo2StereoUploadComplete)
    {
        Halo2Stereo_RequestGenerationQuarantine(
            halo2Generation, !halo2StereoUploadComplete ? Halo2StereoQuarantineReason::VrClaimedSwapchainFailed
                                                        : Halo2StereoQuarantineReason::VrClaimedProjectionFailed);
        VR_Halo2InvalidateSynchronousPair(halo2Generation, g_preparedFrame.serial);
        g_halo2XrPairsDropped.fetch_add(1, std::memory_order_relaxed);
        g_halo2LastDropReason.store(!halo2StereoUploadComplete ? 2u : 3u, std::memory_order_relaxed);
    }
    Halo2FrameDecision DecideHalo2Frame(bool stereo, bool pausedPresentation, bool projectionReady,
        std::vector<XrCompositionLayerProjectionView>& projectionViews)
    {
        const bool currentHalo2Title = TitleAdapter_GetActiveTitle() == GameTitle::Halo2;
        Halo2SynchronousPresentationStamp halo2Presentation{};
        (void)VR_Halo2GetSynchronousPresentationStamp(halo2Presentation);
        const bool halo2DurableExactSerial =
            halo2Presentation.preparedSerial == g_preparedFrame.serial && halo2Presentation.generation != 0 &&
            (halo2Presentation.disposition == Halo2SynchronousFrameDisposition::Claimed ||
                halo2Presentation.disposition == Halo2SynchronousFrameDisposition::Complete);
        // An exact durable claim owns this frame even if AutoVrTick
        // changed the active title immediately before Submit.
        const bool halo2Title = currentHalo2Title || halo2DurableExactSerial;
        const uint32_t halo2Generation = halo2DurableExactSerial ? halo2Presentation.generation
                                         : currentHalo2Title     ? TitleAdapter_GetGeneration(GameTitle::Halo2)
                                                                 : 0;
        float halo2HalfX[2]{}, halo2HalfY[2]{};
        // Revalidate the raw pair independently of late presentation
        // eligibility. A completed synchronous pair has replaced the
        // stock output even if pause/head/session/view state changed
        // after rendering, so that case must classify Complete and drop
        // rather than masquerade as an unclaimed flat-screen frame.
        const bool halo2LiveExactPair = currentHalo2Title && VR_Halo2GetSynchronousHalfFovs(halo2Generation,
                                                                 g_preparedFrame.serial, halo2HalfX, halo2HalfY);
        const bool halo2ProjectionReady = projectionReady;
        // A pair the Anniversary core rendered from the
        // observer's published sample is submitted AS that sample's
        // view poses, so the compositor reprojects the image from
        // the pose it was really drawn at (the weapon and the world
        // share it); the located views of this serial are newer.
        if (halo2LiveExactPair)
        {
            float pairPosition[2][3]{};
            float pairOrientation[2][4]{};
            if (VR_Halo2GetSynchronousPairPoses(halo2Generation, g_preparedFrame.serial, pairPosition, pairOrientation))
            {
                for (size_t view = 0; view < projectionViews.size() && view < 2; ++view)
                {
                    projectionViews[view].pose.position = {
                        pairPosition[view][0], pairPosition[view][1], pairPosition[view][2]};
                    projectionViews[view].pose.orientation = {pairOrientation[view][0], pairOrientation[view][1],
                        pairOrientation[view][2], pairOrientation[view][3]};
                }
                g_halo2XrPairsPosedFromObserver.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // The game runs below the panel rate, so some prepared
        // serials have no pair of their own. Re-presenting the last
        // complete pair with its own pose is what every title does
        // on a missed frame; showing the flat screen quad for that
        // one frame was a visible flash about once a second.
        float halo2RepeatHalfX[2]{}, halo2RepeatHalfY[2]{};
        const bool halo2RepeatablePair =
            currentHalo2Title && !halo2LiveExactPair &&
            VR_Halo2GetRepeatableHalfFovs(halo2Generation, g_preparedFrame.serial, halo2RepeatHalfX, halo2RepeatHalfY);
        const bool halo2PauseStockScreen = Halo2PausePresentationOwnsStockScreen(halo2Title, pausedPresentation);
        const Halo2SynchronousPresentationDecision halo2PresentationDecision =
            halo2PauseStockScreen
                ? Halo2SynchronousPresentationDecision::SharedDefault
                : Halo2SynchronousSelectPresentation(stereo, halo2ProjectionReady, halo2Title, halo2LiveExactPair,
                      halo2Generation, g_preparedFrame.serial, halo2Presentation, halo2RepeatablePair);
        const bool halo2RepeatPair = halo2PresentationDecision == Halo2SynchronousPresentationDecision::RepeatLastPair;
        const bool halo2Images = !halo2Title || halo2LiveExactPair || halo2RepeatPair;
        if (halo2RepeatPair)
        {
            // The images are the last pair's; so must the poses be.
            // The cover/crop above already came from that pair via
            // Game_GetRenderHalfFovs.
            for (size_t view = 0; view < projectionViews.size() && view < 2; ++view)
            {
                projectionViews[view].pose = g_halo2LastPair.pose[view];
            }
            g_halo2XrPairsRepeated.fetch_add(1, std::memory_order_relaxed);
        }
        const Halo2SynchronousFrameDisposition halo2FrameDisposition = Halo2SynchronousClassifyFrame(
            halo2LiveExactPair, halo2Generation, g_preparedFrame.serial, halo2Presentation);
        const bool halo2StrictStockScreen =
            Halo2SynchronousRequiresStrictStockScreen(halo2Title, halo2FrameDisposition, halo2PresentationDecision);
        const bool stereoWorldFrame =
            halo2PresentationDecision == Halo2SynchronousPresentationDecision::SharedDefault
                ? stereo
                : (halo2PresentationDecision == Halo2SynchronousPresentationDecision::SynchronousStereo ||
                      halo2PresentationDecision == Halo2SynchronousPresentationDecision::RepeatLastPair);
        const bool allowStockScreenFrame = halo2PresentationDecision != Halo2SynchronousPresentationDecision::Drop;
        if (Halo2SynchronousDropRequiresQuarantine(
                halo2Generation, g_preparedFrame.serial, halo2Presentation, halo2PresentationDecision))
        {
            const Halo2StereoQuarantineReason reason =
                !halo2LiveExactPair     ? Halo2StereoQuarantineReason::VrClaimedPairUnavailable
                : !halo2ProjectionReady ? Halo2StereoQuarantineReason::VrClaimedProjectionFailed
                                        : Halo2StereoQuarantineReason::VrClaimedPresentationUnavailable;
            Halo2Stereo_RequestGenerationQuarantine(halo2Generation, reason);
        }
        ReportHalo2SynchronousDisposition(stereo && halo2Title, halo2PresentationDecision, g_preparedFrame.serial);
        Halo2FrameDecision d;
        d.title = halo2Title;
        d.generation = halo2Generation;
        for (int i = 0; i < 2; ++i)
        {
            d.halfX[i] = halo2HalfX[i];
            d.halfY[i] = halo2HalfY[i];
        }
        d.liveExactPair = halo2LiveExactPair;
        d.images = halo2Images;
        d.strictStockScreen = halo2StrictStockScreen;
        d.stereoWorldFrame = stereoWorldFrame;
        d.allowStockScreenFrame = allowStockScreenFrame;
        return d;
    }
} // namespace halo2_frame_bridge
static bool ReadHalo2RenderSnapshot(Halo2VrRenderSnapshot& snapshot)
{
    snapshot = {};
    const uint64_t serial = g_preparedSerialPublished.load(std::memory_order_acquire);
    if (!serial || !g_stereoEnabled.load(std::memory_order_acquire) ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }

    const uint32_t index = g_halo2RenderSnapshotIndex.load(std::memory_order_seq_cst);
    if (index >= 2)
        return false;
    uint32_t state = g_halo2RenderSnapshotStates[index].load(std::memory_order_seq_cst);
    if ((state & kHalo2RenderSnapshotWriter) != 0 || state >= kHalo2RenderSnapshotWriter - 1 ||
        !g_halo2RenderSnapshotStates[index].compare_exchange_strong(
            state, state + 1, std::memory_order_seq_cst, std::memory_order_seq_cst))
    {
        return false;
    }
    if (g_halo2RenderSnapshotIndex.load(std::memory_order_seq_cst) != index)
    {
        g_halo2RenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    const Halo2VrRenderSnapshot local = g_halo2RenderSnapshots[index];
    g_halo2RenderSnapshotStates[index].fetch_sub(1, std::memory_order_seq_cst);

    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo2);
    if (local.preparedSerial != serial || !generation || local.generation != generation ||
        TitleAdapter_GetActiveTitle() != GameTitle::Halo2 ||
        g_preparedSerialPublished.load(std::memory_order_acquire) != serial ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serial ||
        !g_preparedShouldRender.load(std::memory_order_acquire))
    {
        return false;
    }
    snapshot = local;
    return true;
}
namespace halo2_frame_bridge
{
    bool Halo2SynchronousContextCurrent(uint32_t generation, uint64_t preparedSerial, uint32_t resourceEpoch) noexcept
    {
        return generation != 0 && preparedSerial != 0 && !Halo2SynchronousSerialIsReserved(preparedSerial) &&
               resourceEpoch != 0 && TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
               TitleAdapter_GetGeneration(GameTitle::Halo2) == generation &&
               g_stereoEnabled.load(std::memory_order_acquire) &&
               g_preparedSerialPublished.load(std::memory_order_acquire) == preparedSerial &&
               g_preparedViewSerialPublished.load(std::memory_order_acquire) == preparedSerial &&
               g_preparedShouldRender.load(std::memory_order_acquire) &&
               g_halo2SynchronousResourceEpoch.load(std::memory_order_acquire) == resourceEpoch && g_eyeCache[0] &&
               g_eyeCache[1] && g_eyeCacheRtvs[0] && g_eyeCacheRtvs[1];
    }
} // namespace halo2_frame_bridge
namespace
{
    bool Halo2SynchronousHalfFovValid(float halfFov) noexcept
    {
        return std::isfinite(halfFov) && halfFov > 0.0f && halfFov < 1.5707f;
    }

    bool PublishHalo2SynchronousPresentation(
        uint32_t generation, uint64_t preparedSerial, Halo2SynchronousFrameDisposition disposition) noexcept
    {
        if (!generation || !preparedSerial || Halo2SynchronousSerialIsReserved(preparedSerial) ||
            (disposition != Halo2SynchronousFrameDisposition::Claimed &&
                disposition != Halo2SynchronousFrameDisposition::Complete))
        {
            return false;
        }

        // Writers are only expected on H2's render thread. Keep the bounded CAS
        // nevertheless: failure to acquire the slot makes the first-eye path
        // take its zero-eye pre-stereo screen path, while a failed Complete
        // publication
        // leaves the already-published conservative Claim in force.
        uint64_t sequence = g_halo2SynchronousPresentationSequence.load(std::memory_order_acquire);
        for (unsigned attempt = 0; attempt < 8; ++attempt)
        {
            if ((sequence & 1u) != 0)
            {
                sequence = g_halo2SynchronousPresentationSequence.load(std::memory_order_acquire);
                continue;
            }
            if (g_halo2SynchronousPresentationSequence.compare_exchange_weak(
                    sequence, sequence + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                const uint64_t currentSerial = g_halo2SynchronousPresentationSerial.load(std::memory_order_relaxed);
                const uint32_t currentGeneration =
                    g_halo2SynchronousPresentationGeneration.load(std::memory_order_relaxed);
                const auto currentDisposition = static_cast<Halo2SynchronousFrameDisposition>(
                    g_halo2SynchronousPresentationDisposition.load(std::memory_order_relaxed));

                bool accepted = false;
                bool replace = false;
                if (!currentSerial || preparedSerial > currentSerial)
                {
                    accepted = true;
                    replace = true;
                }
                else if (preparedSerial == currentSerial && generation == currentGeneration)
                {
                    // Complete is terminal and never regresses to Claimed.
                    accepted = true;
                    replace = currentDisposition != Halo2SynchronousFrameDisposition::Complete &&
                              disposition == Halo2SynchronousFrameDisposition::Complete;
                }

                if (replace)
                {
                    g_halo2SynchronousPresentationGeneration.store(generation, std::memory_order_relaxed);
                    g_halo2SynchronousPresentationDisposition.store(
                        static_cast<uint32_t>(disposition), std::memory_order_relaxed);
                    g_halo2SynchronousPresentationSerial.store(preparedSerial, std::memory_order_relaxed);
                }
                g_halo2SynchronousPresentationSequence.store(sequence + 2, std::memory_order_release);
                return accepted;
            }
        }
        return false;
    }
} // namespace
bool VR_Halo2GetSynchronousRenderSnapshot(Halo2SynchronousVrRenderSnapshot& snapshot)
{
    return ReadHalo2RenderSnapshot(snapshot);
}

bool VR_Halo2GetCurrentPreparedCadence(Halo2PreparedCadenceSnapshot& cadence) noexcept
{
    cadence = {};
    const uint64_t serialBefore = g_halo2PreparedCadenceSerial.load(std::memory_order_acquire);
    if (!serialBefore)
        return false;
    const uint64_t targetPeriod = g_halo2PreparedTargetPeriodNs.load(std::memory_order_relaxed);
    const uint64_t displayDelta = g_halo2PreparedDisplayDeltaNs.load(std::memory_order_relaxed);
    const uint64_t serialAfter = g_halo2PreparedCadenceSerial.load(std::memory_order_acquire);
    if (serialBefore != serialAfter || g_preparedSerialPublished.load(std::memory_order_acquire) != serialBefore ||
        g_preparedViewSerialPublished.load(std::memory_order_acquire) != serialBefore ||
        !g_preparedShouldRender.load(std::memory_order_acquire) || TitleAdapter_GetActiveTitle() != GameTitle::Halo2)
    {
        return false;
    }
    cadence.preparedSerial = serialBefore;
    cadence.predictedDisplayPeriodNs = targetPeriod;
    cadence.predictedDisplayDeltaNs = displayDelta;
    return true;
}

static ID3D11Texture2D* Halo2TextureBehind(ID3D11RenderTargetView* view, D3D11_TEXTURE2D_DESC& desc);

bool VR_Halo2BeginSynchronousPair(
    uint32_t generation, uint64_t preparedSerial, ID3D11RenderTargetView* finalRtv, ID3D11RenderTargetView* sceneRtv)
{
    const uint32_t resourceEpoch = g_halo2SynchronousResourceEpoch.load(std::memory_order_acquire);
    if (!finalRtv || !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch) ||
        finalRtv == g_eyeCacheRtvs[0] || finalRtv == g_eyeCacheRtvs[1])
    {
        return false;
    }

    const uint64_t reserved = Halo2ReservedSynchronousSerial(preparedSerial);
    uint64_t observed = g_halo2SynchronousPairToken.load(std::memory_order_acquire);
    for (;;)
    {
        // A live reservation belongs to an in-flight callback. An equal or
        // newer published serial is a duplicate/out-of-order outer call.
        if (Halo2SynchronousSerialIsReserved(observed) || (observed != 0 && observed >= preparedSerial))
        {
            return false;
        }
        if (g_halo2SynchronousPairToken.compare_exchange_weak(
                observed, reserved, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            break;
        }
    }

    // The reservation first makes every prior pair inadmissible. Publish the
    // borrowed target and exact resource identity only for this serial, then
    // clear both eye stamps before either engine render can start.
    g_halo2SynchronousPairGeneration.store(generation, std::memory_order_relaxed);
    g_halo2SynchronousPairResourceEpoch.store(resourceEpoch, std::memory_order_relaxed);
    g_halo2SynchronousFinalRtv.store(finalRtv, std::memory_order_relaxed);
    if (g_halo2PrePairWanted.exchange(false, std::memory_order_acq_rel) && g_context && g_device)
    {
        // The comparison must be against the SAME target the eyes are copied
        // from. Only the two named slots exist at pair begin; a learned
        // bound-target source leaves the pre-pair copy unsampled.
        const int source = g_halo2CaptureSource.load(std::memory_order_relaxed);
        ID3D11RenderTargetView* const sourceRtv = source == 0 ? finalRtv : (source == 1 ? sceneRtv : nullptr);
        D3D11_TEXTURE2D_DESC desc{};
        ID3D11Texture2D* texture = sourceRtv ? Halo2TextureBehind(sourceRtv, desc) : nullptr;
        if (texture)
        {
            if (g_halo2PrePairCache)
            {
                D3D11_TEXTURE2D_DESC have{};
                g_halo2PrePairCache->GetDesc(&have);
                if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format)
                {
                    g_halo2PrePairCache->Release();
                    g_halo2PrePairCache = nullptr;
                }
            }
            if (!g_halo2PrePairCache)
            {
                D3D11_TEXTURE2D_DESC make = desc;
                make.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                make.MiscFlags = 0;
                make.CPUAccessFlags = 0;
                make.Usage = D3D11_USAGE_DEFAULT;
                make.MipLevels = 1;
                make.ArraySize = 1;
                if (FAILED(g_device->CreateTexture2D(&make, nullptr, &g_halo2PrePairCache)))
                {
                    g_halo2PrePairCache = nullptr;
                    if (!g_halo2PrePairCreateFailed.exchange(true, std::memory_order_relaxed))
                    {
                        LOG("Halo 2 pre-pair witness: the %ux%u fmt %u copy "
                            "could not be created; 'captured target before the "
                            "pair' stays unsampled",
                            desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
                    }
                }
            }
            if (g_halo2PrePairCache)
            {
                g_context->CopyResource(g_halo2PrePairCache, texture);
                g_halo2PrePairSerial.store(preparedSerial, std::memory_order_relaxed);
                g_halo2PrePairHasImage.store(true, std::memory_order_release);
            }
            texture->Release();
        }
    }
    g_halo2SynchronousSceneRtv.store(
        (sceneRtv == g_eyeCacheRtvs[0] || sceneRtv == g_eyeCacheRtvs[1]) ? nullptr : sceneRtv,
        std::memory_order_relaxed);
    g_halo2SynchronousTargetSerial.store(preparedSerial, std::memory_order_release);
    for (int eye = 0; eye < 2; ++eye)
    {
        g_halo2SynchronousEyeSerial[eye].store(0, std::memory_order_release);
        g_halo2SynchronousEyeGeneration[eye].store(0, std::memory_order_relaxed);
        g_halo2SynchronousEyeResourceEpoch[eye].store(0, std::memory_order_relaxed);
        g_halo2SynchronousCaptureSerial[eye].store(0, std::memory_order_release);
        g_halo2SynchronousCaptureCount[eye].store(0, std::memory_order_relaxed);
        g_eyeHasImage[eye] = false;
    }

    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != reserved ||
        g_halo2SynchronousTargetSerial.load(std::memory_order_acquire) != preparedSerial ||
        g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed) != finalRtv ||
        !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        VR_Halo2InvalidateSynchronousPair(generation, preparedSerial);
        return false;
    }
    return true;
}

bool VR_Halo2BeginSynchronousEye(uint32_t generation, uint64_t preparedSerial, int eye)
{
    if (eye < 0 || eye > 1 ||
        g_halo2SynchronousPairToken.load(std::memory_order_acquire) != Halo2ReservedSynchronousSerial(preparedSerial) ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation)
    {
        return false;
    }
    VR_BeginRasterEye(eye);
    return g_halo2SynchronousEyeScope.active && g_halo2SynchronousEyeScope.generation == generation &&
           g_halo2SynchronousEyeScope.preparedSerial == preparedSerial && g_halo2SynchronousEyeScope.eye == eye;
}

bool VR_Halo2CompleteSynchronousEye(uint32_t generation, uint64_t preparedSerial, int eye, float halfFovX,
    float halfFovY, const float* renderedPosition, const float* renderedOrientation)
{
    if (eye < 0 || eye > 1 || !Halo2SynchronousHalfFovValid(halfFovX) || !Halo2SynchronousHalfFovValid(halfFovY))
    {
        return false;
    }
    const bool poseOwned = renderedPosition && renderedOrientation;
    if (poseOwned)
    {
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(renderedPosition[i]))
                return false;
        float lengthSquared = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (!std::isfinite(renderedOrientation[i]))
                return false;
            lengthSquared += renderedOrientation[i] * renderedOrientation[i];
        }
        if (std::fabs(lengthSquared - 1.0f) > 0.05f)
            return false;
    }
    const uint32_t resourceEpoch = g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    const uint64_t reserved = Halo2ReservedSynchronousSerial(preparedSerial);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != reserved ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation ||
        g_halo2SynchronousCaptureSerial[eye].load(std::memory_order_acquire) != preparedSerial ||
        g_halo2SynchronousCaptureCount[eye].load(std::memory_order_relaxed) < kHalo2SynchronousRequiredCaptures ||
        !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        return false;
    }

    g_halo2SynchronousEyeGeneration[eye].store(generation, std::memory_order_relaxed);
    g_halo2SynchronousEyeResourceEpoch[eye].store(resourceEpoch, std::memory_order_relaxed);
    g_halo2SynchronousEyeHalfFovX[eye].store(halfFovX, std::memory_order_relaxed);
    g_halo2SynchronousEyeHalfFovY[eye].store(halfFovY, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        g_halo2SynchronousEyePose[eye][i].store(poseOwned ? renderedPosition[i] : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i)
        g_halo2SynchronousEyePose[eye][3 + i].store(
            poseOwned ? renderedOrientation[i] : (i == 3 ? 1.0f : 0.0f), std::memory_order_relaxed);
    g_halo2SynchronousEyePoseOwned[eye].store(poseOwned, std::memory_order_relaxed);
    g_halo2SynchronousEyeSerial[eye].store(preparedSerial, std::memory_order_release);

    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != reserved ||
        !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        uint64_t expected = preparedSerial;
        (void)g_halo2SynchronousEyeSerial[eye].compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel, std::memory_order_acquire);
        return false;
    }
    g_eyeHasImage[eye] = true;
    return true;
}

bool VR_Halo2CompleteSynchronousPair(uint32_t generation, uint64_t preparedSerial)
{
    const uint64_t reserved = Halo2ReservedSynchronousSerial(preparedSerial);
    const uint32_t resourceEpoch = g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != reserved ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation ||
        !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeSerial[eye].load(std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousEyeGeneration[eye].load(std::memory_order_relaxed) != generation ||
            g_halo2SynchronousEyeResourceEpoch[eye].load(std::memory_order_relaxed) != resourceEpoch ||
            !g_eyeHasImage[eye])
        {
            return false;
        }
    }
    uint64_t expected = reserved;
    if (!g_halo2SynchronousPairToken.compare_exchange_strong(
            expected, preparedSerial, std::memory_order_release, std::memory_order_acquire))
    {
        return false;
    }
    return PublishHalo2SynchronousPresentation(generation, preparedSerial, Halo2SynchronousFrameDisposition::Complete);
}

bool VR_Halo2ClaimSynchronousPairForPresentation(uint32_t generation, uint64_t preparedSerial)
{
    if (!generation || !preparedSerial || Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return false;
    }
    const uint32_t resourceEpoch = g_halo2SynchronousResourceEpoch.load(std::memory_order_acquire);
    if (!Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        return false;
    }

    const uint64_t reserved = Halo2ReservedSynchronousSerial(preparedSerial);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != reserved ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation)
    {
        return false;
    }
    return PublishHalo2SynchronousPresentation(generation, preparedSerial, Halo2SynchronousFrameDisposition::Claimed);
}

bool VR_Halo2GetSynchronousPresentationStamp(Halo2SynchronousPresentationStamp& presentation)
{
    presentation = {};
    for (unsigned attempt = 0; attempt < 8; ++attempt)
    {
        const uint64_t sequenceBefore = g_halo2SynchronousPresentationSequence.load(std::memory_order_acquire);
        if ((sequenceBefore & 1u) != 0)
            continue;

        Halo2SynchronousPresentationStamp candidate{};
        candidate.preparedSerial = g_halo2SynchronousPresentationSerial.load(std::memory_order_relaxed);
        candidate.generation = g_halo2SynchronousPresentationGeneration.load(std::memory_order_relaxed);
        candidate.disposition = static_cast<Halo2SynchronousFrameDisposition>(
            g_halo2SynchronousPresentationDisposition.load(std::memory_order_relaxed));
        const uint64_t sequenceAfter = g_halo2SynchronousPresentationSequence.load(std::memory_order_acquire);
        if (sequenceBefore != sequenceAfter || (sequenceAfter & 1u) != 0)
            continue;
        if (!candidate.generation || !candidate.preparedSerial ||
            (candidate.disposition != Halo2SynchronousFrameDisposition::Claimed &&
                candidate.disposition != Halo2SynchronousFrameDisposition::Complete))
        {
            return false;
        }
        presentation = candidate;
        return true;
    }
    return false;
}

void VR_Halo2InvalidateSynchronousPair(uint32_t generation, uint64_t preparedSerial)
{
    if (!preparedSerial || Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return;
    }
    const uint64_t reserved = Halo2ReservedSynchronousSerial(preparedSerial);
    uint64_t observed = g_halo2SynchronousPairToken.load(std::memory_order_acquire);
    if (observed == reserved || observed == preparedSerial)
    {
        (void)g_halo2SynchronousPairToken.compare_exchange_strong(
            observed, 0, std::memory_order_acq_rel, std::memory_order_acquire);
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeGeneration[eye].load(std::memory_order_relaxed) == generation)
        {
            uint64_t eyeSerial = preparedSerial;
            (void)g_halo2SynchronousEyeSerial[eye].compare_exchange_strong(
                eyeSerial, 0, std::memory_order_acq_rel, std::memory_order_acquire);
        }
        uint64_t redirectSerial = preparedSerial;
        (void)g_halo2SynchronousCaptureSerial[eye].compare_exchange_strong(
            redirectSerial, 0, std::memory_order_acq_rel, std::memory_order_acquire);
    }
    uint64_t targetSerial = preparedSerial;
    (void)g_halo2SynchronousTargetSerial.compare_exchange_strong(
        targetSerial, 0, std::memory_order_acq_rel, std::memory_order_acquire);
}

void VR_ResetHalo2SynchronousStereo()
{
    if (g_halo2ScopeSource)
    {
        g_halo2ScopeSource->Release();
        g_halo2ScopeSource = nullptr;
    }
    g_halo2ScopeSourceRtv = nullptr;
    g_halo2ScopeGeneration = 0;
    g_halo2ScopeSerial = 0;
    g_halo2ScopeCopyActive = g_halo2ScopeSourceBound = false;
    g_halo2LastPair = {};
    g_halo2SynchronousSceneRtv.store(nullptr, std::memory_order_release);
    std::memset(g_halo2ProbeHasImage, 0, sizeof(g_halo2ProbeHasImage));
    for (auto& slot : g_halo2ProbeBox)
        for (auto*& box : slot)
        {
            if (box)
                box->Release();
            box = nullptr;
        }
    std::memset(g_halo2ProbeBoxHasImage, 0, sizeof(g_halo2ProbeBoxHasImage));
    std::memset(g_halo2ProbeBoxFormat, 0, sizeof(g_halo2ProbeBoxFormat));
    if (g_halo2PrePairCache)
    {
        g_halo2PrePairCache->Release();
        g_halo2PrePairCache = nullptr;
    }
    g_halo2PrePairHasImage.store(false, std::memory_order_release);
    const uint32_t generation = g_halo2SynchronousPairGeneration.load(std::memory_order_acquire);
    g_halo2SynchronousPairToken.store(0, std::memory_order_release);
    g_halo2SynchronousPairGeneration.store(0, std::memory_order_release);
    g_halo2SynchronousPairResourceEpoch.store(0, std::memory_order_release);
    g_halo2SynchronousTargetSerial.store(0, std::memory_order_release);
    g_halo2SynchronousFinalRtv.store(nullptr, std::memory_order_release);
    // Do not clear g_halo2SynchronousPresentation*. Game_AutoVrTick, resize, or
    // cache recreation can reset live resources before Submit consumes this
    // exact serial. The integer-only stamp owns no RTV/resource and cannot
    // match a later process-monotonic prepared serial.
    g_halo2FirstPairAccepted.store(false, std::memory_order_release);
    for (int eye = 0; eye < 2; ++eye)
    {
        g_halo2SynchronousEyeSerial[eye].store(0, std::memory_order_release);
        g_halo2SynchronousEyeGeneration[eye].store(0, std::memory_order_release);
        g_halo2SynchronousEyeResourceEpoch[eye].store(0, std::memory_order_release);
        g_halo2SynchronousCaptureSerial[eye].store(0, std::memory_order_release);
        g_halo2SynchronousCaptureCount[eye].store(0, std::memory_order_release);
    }
    // Clear the runtime-owner proof along the same reset edge. The stamped
    // Gameplay mode is no longer eligible once its heartbeat is revoked, and
    // a later pair must pass the full render/upload/xrEndFrame transaction to
    // publish a new one. ClearHeartbeat is bounded atomic-only state work.
    if (generation)
        TitleAdapter_ClearHeartbeat(GameTitle::Halo2, generation);
}

bool VR_Halo2GetRepeatableHalfFovs(uint32_t generation, uint64_t preparedSerial, float halfX[2], float halfY[2])
{
    const Halo2LastPair& last = g_halo2LastPair;
    if (!last.valid || !generation || last.generation != generation || !preparedSerial || preparedSerial <= last.serial)
    {
        return false;
    }
    const uint64_t nowMs = GetTickCount64();
    if (nowMs < last.tickMs || nowMs - last.tickMs > kHalo2RepeatPairMaxAgeMs)
        return false;
    // A reserved token means a newer pair is being rendered right now and
    // may already have overwritten one eye cache: never mix two frames.
    if (Halo2SynchronousSerialIsReserved(g_halo2SynchronousPairToken.load(std::memory_order_acquire)))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (!g_eyeHasImage[eye] ||
            g_halo2SynchronousCaptureSerial[eye].load(std::memory_order_acquire) != last.serial ||
            !Halo2SynchronousHalfFovValid(last.halfX[eye]) || !Halo2SynchronousHalfFovValid(last.halfY[eye]))
        {
            return false;
        }
        halfX[eye] = last.halfX[eye];
        halfY[eye] = last.halfY[eye];
    }
    return true;
}

bool VR_Halo2GetSynchronousPairPoses(
    uint32_t generation, uint64_t preparedSerial, float position[2][3], float orientation[2][4])
{
    if (!position || !orientation || !generation || !preparedSerial || Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return false;
    }
    const uint32_t resourceEpoch = g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != preparedSerial ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation ||
        !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeSerial[eye].load(std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousEyeGeneration[eye].load(std::memory_order_relaxed) != generation ||
            !g_halo2SynchronousEyePoseOwned[eye].load(std::memory_order_relaxed))
        {
            return false;
        }
        for (int i = 0; i < 3; ++i)
            position[eye][i] = g_halo2SynchronousEyePose[eye][i].load(std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i)
            orientation[eye][i] = g_halo2SynchronousEyePose[eye][3 + i].load(std::memory_order_relaxed);
    }
    return g_halo2SynchronousPairToken.load(std::memory_order_acquire) == preparedSerial;
}

bool VR_Halo2GetSynchronousHalfFovs(uint32_t generation, uint64_t preparedSerial, float halfX[2], float halfY[2])
{
    if (!halfX || !halfY || !generation || !preparedSerial || Halo2SynchronousSerialIsReserved(preparedSerial))
    {
        return false;
    }
    const uint32_t resourceEpoch = g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
    if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != preparedSerial ||
        g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation ||
        !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
    {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye)
    {
        if (g_halo2SynchronousEyeSerial[eye].load(std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousEyeGeneration[eye].load(std::memory_order_relaxed) != generation ||
            g_halo2SynchronousEyeResourceEpoch[eye].load(std::memory_order_relaxed) != resourceEpoch ||
            !g_eyeHasImage[eye])
        {
            return false;
        }
        halfX[eye] = g_halo2SynchronousEyeHalfFovX[eye].load(std::memory_order_relaxed);
        halfY[eye] = g_halo2SynchronousEyeHalfFovY[eye].load(std::memory_order_relaxed);
        if (!Halo2SynchronousHalfFovValid(halfX[eye]) || !Halo2SynchronousHalfFovValid(halfY[eye]))
        {
            return false;
        }
    }
    return g_halo2SynchronousPairToken.load(std::memory_order_acquire) == preparedSerial &&
           Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch);
}
// Called from Halo 2 manual VR recovery.
void VR_ResetHalo2CaptureSource(const char* reason)
{
    ResetHalo2CaptureSourceLocked(reason);
}
// Copies the engine's finished Halo 2 frame for one eye into that eye's cache.
// Called on the render thread from VR_EndRasterEye, i.e. after render_view has
// returned and every draw of this eye (world, resolve, postprocess, final
// output) has been issued. Resolves the texture behind the proven final-output
// RTV each call and holds NO reference to it: a held reference would pin the
// swapchain buffer and make the game's own ResizeBuffers fail.
// Resolves the single-sample 2D texture behind an RTV, or null. Holds no
// reference beyond the returned one; the caller releases it.
static ID3D11Texture2D* Halo2TextureBehind(ID3D11RenderTargetView* rtv, D3D11_TEXTURE2D_DESC& desc)
{
    if (!rtv)
        return nullptr;
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource)
    {
        (void)resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        resource->Release();
    }
    if (!texture)
        return nullptr;
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || !desc.Width || !desc.Height)
    {
        texture->Release();
        return nullptr;
    }
    return texture;
}

static bool Halo2EnsureProbeCaches(const D3D11_TEXTURE2D_DESC& source)
{
    if (g_halo2ProbeCache[0][0] && g_halo2ProbeCache[1][1] && g_halo2ProbeDesc.Width == source.Width &&
        g_halo2ProbeDesc.Height == source.Height && g_halo2ProbeDesc.Format == source.Format)
    {
        return true;
    }
    for (auto& pair : g_halo2ProbeCache)
        for (auto*& texture : pair)
        {
            if (texture)
                texture->Release();
            texture = nullptr;
        }
    D3D11_TEXTURE2D_DESC desc = source;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    for (auto& pair : g_halo2ProbeCache)
        for (auto*& texture : pair)
            if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &texture)))
                return false;
    g_halo2ProbeDesc = desc;
    std::memset(g_halo2ProbeHasImage, 0, sizeof(g_halo2ProbeHasImage));
    return true;
}
namespace halo2_frame_bridge
{
    bool Halo2CopyFinishedEyeFrame(const Halo2SynchronousEyeScope& scope)
    {
        static bool loggedShape = false;
        static bool loggedFailure = false;
        static uint32_t censusGeneration = 0;
        if (!g_context || !g_device || !scope.finalRtv || scope.eye < 0 || scope.eye > 1)
        {
            return false;
        }
        // The three candidates for "the finished eye": the final-output slot,
        // the primary scene slot, and the last target the engine bound during
        // this eye. Which one carries per-eye content is LEARNED from the
        // readback comparison, never assumed.
        // The two named slots first (their indices are stable across
        // builds and logs), then every distinct target the engine bound inside
        // this eye that is not already one of them.
        ID3D11RenderTargetView* candidates[kHalo2CaptureCandidateSlots] = {scope.finalRtv, scope.sceneRtv};
        int candidateCount = 2;
        for (int i = 0; i < scope.boundCount && candidateCount < kHalo2CaptureCandidateSlots; ++i)
        {
            bool duplicate = false;
            for (int j = 0; j < candidateCount && !duplicate; ++j)
                duplicate = candidates[j] == scope.bound[i];
            if (!duplicate && scope.bound[i])
                candidates[candidateCount++] = scope.bound[i];
        }
        g_halo2CaptureCandidateCount.store(candidateCount, std::memory_order_relaxed);
        int source = g_halo2CaptureSource.load(std::memory_order_relaxed);
        if (source < 0 || source >= candidateCount || !candidates[source])
        {
            const uint64_t n = g_halo2CaptureSourceFallbacks.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 600) == 0)
            {
                LOG("Halo 2 eye capture: learned source #%d is not available in "
                    "this eye (%d candidates); the final-output slot is used and "
                    "re-learned from here (fallback #%llu)",
                    source, candidateCount, static_cast<unsigned long long>(n));
            }
            source = 0;
            g_halo2CaptureSource.store(0, std::memory_order_relaxed);
            g_halo2ProbeRotation.store(0, std::memory_order_relaxed);
            g_halo2HeavyValidationSource.store(0, std::memory_order_relaxed);
            g_halo2HeavyValidationChecks.store(0, std::memory_order_relaxed);
            g_halo2HeavyValidationRetired.store(false, std::memory_order_release);
        }
        if (censusGeneration != scope.generation)
        {
            censusGeneration = scope.generation;
            LOG("Halo 2 eye capture: %d distinct targets were bound inside this "
                "eye; %d capture candidates (final slot, scene slot, then the "
                "bound targets)",
                scope.boundCount, candidateCount);
            for (int i = 0; i < scope.boundCount; ++i)
            {
                D3D11_TEXTURE2D_DESC boundDesc{};
                ID3D11Texture2D* boundTexture = Halo2TextureBehind(scope.bound[i], boundDesc);
                LOG("Halo 2 eye capture: bound target #%d = RTV %p -> tex %p "
                    "%ux%u fmt %u samples %u%s%s",
                    i, scope.bound[i], boundTexture, boundDesc.Width, boundDesc.Height,
                    static_cast<unsigned>(boundDesc.Format), boundDesc.SampleDesc.Count,
                    scope.bound[i] == scope.finalRtv ? " (= final slot)" : "",
                    scope.bound[i] == scope.sceneRtv ? " (= scene slot)" : "");
                if (boundTexture)
                    boundTexture->Release();
            }
            uintptr_t slots[3] = {
                reinterpret_cast<uintptr_t>(scope.finalRtv), reinterpret_cast<uintptr_t>(scope.sceneRtv), 0};
            VR_Halo2LogTargetCensusOnce(
                "eye end, classic/anniversary capture", slots, static_cast<uint32_t>(scope.eye), scope.binds);
            LOG("Halo 2 eye capture: last target bound inside this eye = %p "
                "(%u binds); final slot %p; scene slot %p; current source %d",
                scope.lastBoundRtv, scope.binds, scope.finalRtv, scope.sceneRtv, source);
        }
        D3D11_TEXTURE2D_DESC desc{};
        ID3D11Texture2D* texture = Halo2TextureBehind(candidates[source], desc);
        if (!texture)
        {
            if (!loggedFailure)
            {
                loggedFailure = true;
                LOG("Halo 2 eye capture FAILED: capture source %d RTV %p is not "
                    "backed by a single-sample 2D texture; no eye image is "
                    "published",
                    source, candidates[source]);
            }
            return false;
        }
        // Probe copies of the other two candidates, for the comparison that
        // decides the source. Same shape only; a differently shaped candidate
        // cannot be the finished eye.
        if (!g_halo2HeavyValidationRetired.load(std::memory_order_acquire) && Halo2EnsureProbeCaches(desc))
        {
            const int rotation = g_halo2ProbeRotation.load(std::memory_order_relaxed);
            for (int slot = 0; slot < 2; ++slot)
            {
                const int candidate = Halo2ProbeCandidateForSlot(rotation, slot, source, candidateCount);
                g_halo2ProbeCandidate[slot][scope.eye].store(candidate, std::memory_order_relaxed);
                g_halo2ProbeCandidatePtr[slot][scope.eye].store(
                    candidate >= 0 ? reinterpret_cast<uintptr_t>(candidates[candidate]) : 0, std::memory_order_relaxed);
                if (candidate < 0)
                {
                    g_halo2ProbeHasImage[slot][scope.eye] = false;
                    g_halo2ProbeBoxHasImage[slot][scope.eye] = false;
                    continue;
                }
                g_halo2ProbeHasImage[slot][scope.eye] = false;
                g_halo2ProbeBoxHasImage[slot][scope.eye] = false;
                D3D11_TEXTURE2D_DESC otherDesc{};
                ID3D11Texture2D* other = Halo2TextureBehind(candidates[candidate], otherDesc);
                if (!other)
                    continue;
                if (otherDesc.Width == desc.Width && otherDesc.Height == desc.Height &&
                    otherDesc.Format == desc.Format && other != texture)
                {
                    g_context->CopyResource(g_halo2ProbeCache[slot][scope.eye], other);
                    g_halo2ProbeHasImage[slot][scope.eye] = true;
                }
                // Shape-agnostic centre box, for candidates the
                // same-shape copy above cannot take (the engine's scene target).
                if (otherDesc.Width >= kHalo2ProbeBoxSize && otherDesc.Height >= kHalo2ProbeBoxSize &&
                    otherDesc.SampleDesc.Count == 1)
                {
                    ID3D11Texture2D*& box = g_halo2ProbeBox[slot][scope.eye];
                    if (box && g_halo2ProbeBoxFormat[slot][scope.eye] != otherDesc.Format)
                    {
                        box->Release();
                        box = nullptr;
                    }
                    if (!box)
                    {
                        D3D11_TEXTURE2D_DESC boxDesc{};
                        boxDesc.Width = kHalo2ProbeBoxSize;
                        boxDesc.Height = kHalo2ProbeBoxSize;
                        boxDesc.MipLevels = 1;
                        boxDesc.ArraySize = 1;
                        boxDesc.Format = otherDesc.Format;
                        boxDesc.SampleDesc.Count = 1;
                        boxDesc.Usage = D3D11_USAGE_STAGING;
                        boxDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        if (SUCCEEDED(g_device->CreateTexture2D(&boxDesc, nullptr, &box)))
                            g_halo2ProbeBoxFormat[slot][scope.eye] = otherDesc.Format;
                        else
                            box = nullptr;
                    }
                    if (box)
                    {
                        D3D11_BOX region{};
                        region.left = (otherDesc.Width - kHalo2ProbeBoxSize) / 2;
                        region.top = (otherDesc.Height - kHalo2ProbeBoxSize) / 2;
                        region.right = region.left + kHalo2ProbeBoxSize;
                        region.bottom = region.top + kHalo2ProbeBoxSize;
                        region.front = 0;
                        region.back = 1;
                        g_context->CopySubresourceRegion(box, 0, 0, 0, 0, other, 0, &region);
                        g_halo2ProbeBoxHasImage[slot][scope.eye] = true;
                    }
                }
                other->Release();
            }
        }
        // The eye caches keep the FINAL slot's shape, so learning a
        // differently shaped source (the engine's scene target) cannot churn every
        // Halo 2 resource per eye; the blit resizes and resolves the format.
        D3D11_TEXTURE2D_DESC cacheDesc = desc;
        if (source != 0 && candidates[0])
        {
            D3D11_TEXTURE2D_DESC finalDesc{};
            ID3D11Texture2D* finalTexture = Halo2TextureBehind(candidates[0], finalDesc);
            if (finalTexture)
            {
                cacheDesc = finalDesc;
                finalTexture->Release();
            }
        }
        if (Halo2FormatIsTypeless(static_cast<uint32_t>(cacheDesc.Format)))
        {
            cacheDesc.Format = static_cast<DXGI_FORMAT>(Halo2ConcreteFormat(static_cast<uint32_t>(cacheDesc.Format)));
        }
        bool copied = false;
        if (EnsureEyeCaches(cacheDesc) && g_eyeCache[scope.eye])
        {
            if (desc.Width == g_eyeCacheDesc.Width && desc.Height == g_eyeCacheDesc.Height &&
                desc.Format == g_eyeCacheDesc.Format)
            {
                g_context->CopyResource(g_eyeCache[scope.eye], texture);
                copied = true;
            }
            else
            {
                copied = Blit(texture, desc, g_eyeCache[scope.eye], g_eyeCacheDesc.Width, g_eyeCacheDesc.Height,
                    g_eyeCacheRtvs[scope.eye]);
                static bool loggedBlit = false;
                if (copied && !loggedBlit)
                {
                    loggedBlit = true;
                    LOG("Halo 2 eye capture: the learned source is %ux%u fmt %u "
                        "and the eye cache is %ux%u fmt %u, so the finished eye is "
                        "blitted rather than copied",
                        desc.Width, desc.Height, static_cast<unsigned>(desc.Format), g_eyeCacheDesc.Width,
                        g_eyeCacheDesc.Height, static_cast<unsigned>(g_eyeCacheDesc.Format));
                }
            }
            if (copied && !loggedShape)
            {
                loggedShape = true;
                LOG("Halo 2 eye capture: copying the engine's finished %ux%u "
                    "fmt %u frame into eye cache %d after render_view returns; "
                    "no render target is redirected",
                    desc.Width, desc.Height, static_cast<unsigned>(desc.Format), scope.eye);
            }
        }
        else if (!loggedFailure)
        {
            loggedFailure = true;
            LOG("Halo 2 eye capture FAILED: eye caches for %ux%u fmt %u could "
                "not be created",
                desc.Width, desc.Height, static_cast<unsigned>(desc.Format));
        }
        if (copied)
        {
            // Transfer the reference already acquired by the eye copy. No extra
            // COM discovery/AddRef is performed by the optional scope callback.
            if (g_halo2ScopeSource)
                g_halo2ScopeSource->Release();
            g_halo2ScopeSource = texture;
            g_halo2ScopeSourceRtv = candidates[source];
            g_halo2ScopeSourceDesc = desc;
            g_halo2ScopeGeneration = scope.generation;
            g_halo2ScopeSerial = scope.preparedSerial;
        }
        else
            texture->Release();
        return copied;
    }
} // namespace halo2_frame_bridge
void VR_Halo2RequestEyeDump(uint32_t delayMs)
{
    g_halo2EyeDumpDueMs.store(GetTickCount64() + delayMs, std::memory_order_release);
}

void VR_Halo2NoteDraw()
{
    const int eye = g_rasterEye.load(std::memory_order_relaxed);
    if (eye == 0 || eye == 1)
        g_halo2DrawsInsideEye[eye].fetch_add(1, std::memory_order_relaxed);
    else
        g_halo2DrawsOutsideEye.fetch_add(1, std::memory_order_relaxed);
}

static void Halo2DescribeRtv(ID3D11RenderTargetView* rtv, char* out, size_t outBytes)
{
    if (!rtv)
    {
        _snprintf_s(out, outBytes, _TRUNCATE, "null");
        return;
    }
    ID3D11Resource* resource = nullptr;
    rtv->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource)
    {
        (void)resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
        resource->Release();
    }
    if (!texture)
    {
        _snprintf_s(out, outBytes, _TRUNCATE, "%p (not a 2D texture)", rtv);
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    _snprintf_s(out, outBytes, _TRUNCATE, "%p tex %p %ux%u fmt %u samples %u", rtv, texture, desc.Width, desc.Height,
        static_cast<unsigned>(desc.Format), desc.SampleDesc.Count);
    texture->Release();
}

void VR_Halo2LogTargetCensusOnce(const char* tag, const uintptr_t slots[3], uint32_t viewIndex, uint32_t viewMask)
{
    if (!g_context || !slots)
        return;
    char described[4][160]{};
    for (int index = 0; index < 3; ++index)
    {
        Halo2DescribeRtv(
            reinterpret_cast<ID3D11RenderTargetView*>(slots[index]), described[index], sizeof(described[index]));
    }
    ID3D11RenderTargetView* bound[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth = nullptr;
    g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, bound, &depth);
    Halo2DescribeRtv(bound[0], described[3], sizeof(described[3]));
    for (auto* view : bound)
        if (view)
            view->Release();
    if (depth)
        depth->Release();
    LOG("Halo 2 target census (%s, view %u, views seen mask 0x%X): "
        "backbuffer slot 0x197EE58 = %s | primary scene slot 0x197EE60 = %s | "
        "resolved scene slot 0x197EE88 = %s | OM slot 0 bound now = %s",
        tag ? tag : "?", viewIndex, viewMask, described[0], described[1], described[2], described[3]);
}
namespace
{
    // HUD replay support. All three run on the Saber render thread,
    // after the pair for `preparedSerial` completed on that same thread.
    bool Halo2HudReplayTarget(
        uint32_t generation, uint64_t preparedSerial, const char** reason, ID3D11Texture2D** outTexture)
    {
        if (outTexture)
            *outTexture = nullptr;
        auto fail = [&](const char* why)
        {
            if (reason)
                *reason = why;
            return false;
        };
        if (!g_context || !generation || !preparedSerial || Halo2SynchronousSerialIsReserved(preparedSerial))
            return fail("no context or serial");
        const uint32_t resourceEpoch = g_halo2SynchronousPairResourceEpoch.load(std::memory_order_relaxed);
        if (g_halo2SynchronousPairToken.load(std::memory_order_acquire) != preparedSerial ||
            g_halo2SynchronousPairGeneration.load(std::memory_order_relaxed) != generation ||
            !Halo2SynchronousContextCurrent(generation, preparedSerial, resourceEpoch))
            return fail("the pair for this serial is not the complete one");
        for (int eye = 0; eye < 2; ++eye)
        {
            if (g_halo2SynchronousEyeSerial[eye].load(std::memory_order_acquire) != preparedSerial ||
                !g_eyeHasImage[eye] || !g_eyeCache[eye])
                return fail("an eye image of this serial is missing");
        }
        ID3D11RenderTargetView* finalRtv = g_halo2SynchronousFinalRtv.load(std::memory_order_relaxed);
        if (!finalRtv)
            return fail("no final-output target recorded for the pair");
        D3D11_TEXTURE2D_DESC desc{};
        ID3D11Texture2D* texture = Halo2TextureBehind(finalRtv, desc);
        if (!texture)
            return fail("the final-output target is not a 2D texture");
        D3D11_TEXTURE2D_DESC cacheDesc{};
        g_eyeCache[0]->GetDesc(&cacheDesc);
        if (cacheDesc.Width != desc.Width || cacheDesc.Height != desc.Height || cacheDesc.Format != desc.Format ||
            desc.SampleDesc.Count != 1)
        {
            texture->Release();
            return fail("the final-output target no longer matches the eye cache");
        }
        if (outTexture)
            *outTexture = texture;
        else
            texture->Release();
        return true;
    }
} // namespace
bool VR_Halo2HudReplayEligible(uint32_t generation, uint64_t preparedSerial, const char** reason)
{
    return Halo2HudReplayTarget(generation, preparedSerial, reason, nullptr);
}

bool VR_Halo2GetHudSourceRectangle(uint32_t generation, uint64_t preparedSerial, Halo2CameraRectangle& rectangle)
{
    if (!Halo2HudReplayTarget(generation, preparedSerial, nullptr, nullptr) || !g_eyeCache[0])
    {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    g_eyeCache[0]->GetDesc(&desc);
    if (!desc.Width || !desc.Height || desc.Width > static_cast<UINT>(INT16_MAX) ||
        desc.Height > static_cast<UINT>(INT16_MAX))
    {
        return false;
    }
    rectangle = {0, 0, static_cast<int16_t>(desc.Height), static_cast<int16_t>(desc.Width)};
    return true;
}

bool VR_Halo2RecaptureEyeFromFinalTarget(uint32_t generation, uint64_t preparedSerial, int eye)
{
    if (eye < 0 || eye > 1)
        return false;
    ID3D11Texture2D* texture = nullptr;
    if (!Halo2HudReplayTarget(generation, preparedSerial, nullptr, &texture))
        return false;
    g_context->CopyResource(g_eyeCache[eye], texture);
    texture->Release();
    return true;
}

bool VR_Halo2RestoreEyeToFinalTarget(uint32_t generation, uint64_t preparedSerial, int eye)
{
    if (eye < 0 || eye > 1)
        return false;
    ID3D11Texture2D* texture = nullptr;
    if (!Halo2HudReplayTarget(generation, preparedSerial, nullptr, &texture))
        return false;
    g_context->CopyResource(texture, g_eyeCache[eye]);
    texture->Release();
    return true;
}
bool VR_BeginHalo2Scope(uint32_t generation, uint64_t serial)
{
    const int refusal = !g_context || !g_scopeCache || !g_halo2ScopeSource ? 1
                        : !generation || !serial || generation != g_halo2ScopeGeneration || serial != g_halo2ScopeSerial
                            ? 2
                        : TitleAdapter_GetActiveTitle() != GameTitle::Halo2 ? 3
                        : g_scopeCacheDesc.Width != g_halo2ScopeSourceDesc.Width ||
                                g_scopeCacheDesc.Height != g_halo2ScopeSourceDesc.Height ||
                                g_scopeCacheDesc.Format != g_halo2ScopeSourceDesc.Format
                            ? 4
                            : 0;
    g_halo2ScopeBeginRefusal.store(refusal, std::memory_order_relaxed);
    if (refusal)
        return false;
    g_halo2ScopeCopyActive = true;
    g_halo2ScopeSourceBound = false;
    return true;
}
void VR_EndHalo2Scope(bool completed)
{
    if (g_halo2ScopeCopyActive)
        g_halo2ScopeOutcome.store(!completed ? 2 : !g_halo2ScopeSourceBound ? 3 : 1, std::memory_order_relaxed);
    if (completed && g_halo2ScopeCopyActive && g_halo2ScopeSourceBound && g_context && g_scopeCache &&
        g_halo2ScopeSource)
    {
        g_context->CopyResource(g_scopeCache, g_halo2ScopeSource);
        g_scopeImageTitle = GameTitle::Halo2;
        g_scopeImageGeneration = g_halo2ScopeGeneration;
        g_scopeHasImage.store(true, std::memory_order_release);
        NoteScopeImageCaptured();
    }
    g_halo2ScopeCopyActive = false;
    g_halo2ScopeSourceBound = false;
}
void VR_Halo2ScopeDiagnostics(int& beginRefusal, int& outcome)
{
    beginRefusal = g_halo2ScopeBeginRefusal.load(std::memory_order_relaxed);
    outcome = g_halo2ScopeOutcome.load(std::memory_order_relaxed);
}
