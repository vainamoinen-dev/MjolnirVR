// Optional first-person vehicle camera for CE, Halo 2 and Halo 4: hooks each title's seat
// camera selector and places the eye in the seat. Polled from game.cpp; settings from menu.cpp.
// Off unless vehicle_first_person is set; a failed proof or fault leaves the native camera.
#include "native_vehicle_first_person.h"
#include "hook_install_check.h" // MinHook create/remove go through the install-time detour check
#include "../../common/titles/ce/haloce_vehicle_frame.h"
#include "../../common/cockpit_screen_logic.h"
#include "vr.h"
#include "title_adapter.h"
#include "game.h"
#include "sigscan.h"
#include "hook_quiescence.h"
#include "../../common/config.h"
#include "../../common/log.h"
#include "../../common/minhook_lifecycle.h"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <type_traits>
#include "guard_count.h"

namespace
{
    enum Role : unsigned
    {
        Selector,
        Camera,
        Marker,
        MarkerProof,
        RoleCount
    };
    struct Binding
    {
        uint32_t rva;
        const char* pattern;
    };
    struct BindingSet
    {
        GameTitle title;
        uint32_t markerRva;
        std::array<Binding, RoleCount> entries;
    };
#include "native_vehicle_first_person_bindings.generated.h"
    struct Runtime
    {
        HMODULE module{};
        uintptr_t base{};
        uint32_t generation{};
        bool attempted{}, ready{}, enabled[2]{};
        void* target[2]{};
        void* original[2]{};
        void* marker{};
        void* markerInterpolated{};
        std::atomic<uint64_t> trimIdentity{};
        std::atomic_flag trimWriting = ATOMIC_FLAG_INIT;
        std::atomic<uint32_t> trimVersion{}, trimGeneration{};
        std::atomic<int> trimSeat{-1};
        std::atomic<bool> requested{}, faulted{};
        std::atomic<uint32_t> callbacks{};
        std::atomic<uint64_t> selected{}, positioned{}, missingMarker{}, faults{}, recentSelect{};
        // The unit SelectHook last forced into first person, and when. The
        // selector is a transition state machine (modes 1/3 enter/exit), so a
        // second call with a zeroed mode need not answer 2 for a seat it just
        // selected; this lets the camera hook trust its own fresh selection.
        std::atomic<uint32_t> selectedUnit{UINT32_MAX};
        std::atomic<uint64_t> selectedAtMs{};
        // The seat that selection proved. The engine need not re-call the selector
        // for the local unit every frame, so a selection with no later verdict for
        // that unit holds the proven seated head (same parent+seat) for
        // kSeatHoldMs, like the earlier CE VR mod's settled seat anchor, instead of
        // dropping back to the native vehicle camera between calls.
        std::atomic<uint32_t> selectedParent{UINT32_MAX};
        std::atomic<int32_t> selectedSeat{-1};
        std::atomic<uint64_t> seatHold{}, frameAnchored{};
        // Diagnostic (resume-while-seated): native selector modes answered for the
        // local unit while it owns a seat but first person was NOT selected.
        std::atomic<uint64_t> seatedUnselectedModes[8]{};
        // CE: the last fresh seated head in the proven vehicle frame (camera hook
        // thread only). A held frame re-places it on the moving hull.
        haloce_vehicle_frame::Vec3 seatLocal{};
        uint32_t seatVehicle = UINT32_MAX;
        int16_t seatIndex = -1;
        bool seatLocalValid = false;
        uint64_t seatEntryMs = 0, seatLastFrameMs = 0;
        bool seatAnchorLogged = false;
        std::atomic<bool> anchorLogPending{false};
        // "CE seat proof" line (every 2 s while seated): the last hull-frame eye
        // in hull-local metres (forward, left, up), the seat, and how many camera
        // frames were placed by the hull eye vs. left on the native head.
        std::atomic<float> proofEye[3]{}, proofSeatLocal[3]{};
        std::atomic<int> proofSeat{-1};
        std::atomic<uint64_t> proofEyeMs{}, everyFrame{};
        uint64_t proofReportAt{};
        // Diagnostic: selector call cadence for the local unit (gaps between calls).
        std::atomic<uint64_t> selectorLastMs{}, selectorGapSum{}, selectorGapCount{}, selectorGapMax{};
        uint64_t reportAt{};
    };
    Runtime runtime[3];
    using CameraFn = void(__fastcall*)(uint32_t, const void*, void*);
    bool Current(unsigned i) noexcept
    {
        const auto& r = runtime[i];
        return r.requested.load(std::memory_order_acquire) && !r.faulted.load(std::memory_order_acquire) &&
               TitleAdapter_GetActiveTitle() == kBindings[i].title &&
               TitleAdapter_GetGeneration(kBindings[i].title) == r.generation;
    }
    bool Owner(unsigned i, uint32_t unit, NativeVehicleCameraOwner& owner) noexcept
    {
        return Current(i) && unit != UINT32_MAX && (unit >> 16) &&
               Game_ReadVehicleCameraOwner(kBindings[i].title, owner) && owner.unit == unit &&
               owner.generation == runtime[i].generation && owner.parent != UINT32_MAX && owner.seat >= 0;
    }
    // All three native marker records have their world transform at +56 and
    // translation at +96, independently witnessed by their own camera consumers.
    // Positive count is essential: the native missing-marker fallback also fills
    // a transform, but returns zero. It must not put the camera at the body origin.
    __declspec(noinline) bool Head(unsigned i, const NativeVehicleCameraOwner& owner, float position[3])
    {
        const uint32_t unit = owner.unit;
        alignas(16) uint8_t record[128]{};
        int16_t count = 0;
        if (i == 0)
            count = reinterpret_cast<int16_t(__fastcall*)(uint32_t, const char*, void*, int16_t)>(runtime[i].marker)(
                unit, "head", record, 1);
        else if (i == 1)
        {
            if (g_config.vehicle_cam_smoothing && runtime[i].markerInterpolated)
                count = reinterpret_cast<int16_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t, uint8_t, uint8_t)>(
                    runtime[i].markerInterpolated)(unit, 0x4000095, record, 1, 0, 1);
            else
                count = reinterpret_cast<int16_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t)>(runtime[i].marker)(
                    unit, 0x4000095, record, 1);
        }
        else
            count =
                reinterpret_cast<int16_t(__fastcall*)(uint32_t, uint32_t, void*, int16_t, uint8_t, uint8_t, uint8_t)>(
                    runtime[i].marker)(unit, 0x122, record, 1, 0, 0, 1);
        if (count != 1)
            return false;
        std::memcpy(position, record + 96, 12);
        float forward[3]{}, up[3]{};
        std::memcpy(forward, record + 60, 12);
        std::memcpy(up, record + 84, 12);
        float forwardLength = 0, upLength = 0, dot = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(forward[axis]) || !std::isfinite(up[axis]))
                return false;
            forwardLength += forward[axis] * forward[axis];
            upLength += up[axis] * up[axis];
            dot += forward[axis] * up[axis];
        }
        if (std::fabs(forwardLength - 1) > 0.05f || std::fabs(upLength - 1) > 0.05f || std::fabs(dot) > 0.05f)
            return false;
        const float right[3]{forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2],
            forward[0] * up[1] - forward[1] * up[0]};
        const float scale = Game_GetWorldScale();
        const auto title = kBindings[i].title;
        const float f = ConfigVehicleModelCam(g_config, title, owner.vehicleIdentity, owner.seat, 0),
                    u = ConfigVehicleModelCam(g_config, title, owner.vehicleIdentity, owner.seat, 1),
                    h = ConfigVehicleModelCam(g_config, title, owner.vehicleIdentity, owner.seat, 2);
        if (!std::isfinite(scale) || scale <= 0 || scale > 100 || !std::isfinite(f) || !std::isfinite(u) ||
            !std::isfinite(h) || std::fabs(f) > 5 || std::fabs(u) > 5 || std::fabs(h) > 5)
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            position[axis] += scale * (forward[axis] * f + up[axis] * u + right[axis] * h);
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!std::isfinite(position[axis]) || std::fabs(position[axis]) > 100000.0f)
                return false;
        return true;
    }
    __declspec(noinline) void Fault(unsigned i) noexcept
    {
        // Keep a real handler call under MSVC /O2. Production-detour SEH fixtures
        // verify that a native marker exception persists this feature-only latch.
        runtime[i].faulted.store(true, std::memory_order_release);
        runtime[i].faults.fetch_add(1, std::memory_order_relaxed);
    }
    inline constexpr uint64_t kFreshSelectMs = 100, kSeatHoldMs = 2000, kSeatSettleMs = 1000, kSeatFrameGapMs = 250;
    template <unsigned I>
    using Mode = std::conditional_t<I == 2, int32_t, int16_t>;
    template <unsigned I>
    using SelectorFn = uint32_t(__fastcall*)(uint32_t, Mode<I>*);
    // Keep the guarded feature body separate from native forwarding/finally. A
    // fault in the optional marker query must be handled here, never swallowed by
    // or attributed to the native camera call.
    template <unsigned I>
    bool SelectOwned(uint32_t unit, Mode<I>* mode) noexcept
    {
        __try
        {
            NativeVehicleCameraOwner before{}, after{};
            float point[3]{};
            if (!mode || *mode != 2 || !Owner(I, unit, before))
                return false;
            if (!Head(I, before, point))
            {
                runtime[I].missingMarker.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            return Owner(I, unit, after) && before == after;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("selecting the owned vehicle camera faulted");
            Fault(I);
            return false;
        }
    }
    template <unsigned I>
    void PositionOwned(uint32_t unit, void* result) noexcept
    {
        __try
        {
            NativeVehicleCameraOwner before{}, after{};
            float point[3]{};
            Mode<I> mode{};
            if (!result || !Owner(I, unit, before))
                return;
            reinterpret_cast<SelectorFn<I>>(runtime[I].original[Selector])(unit, &mode);
            const uint64_t now = GetTickCount64(), at = runtime[I].selectedAtMs.load(std::memory_order_acquire);
            const bool mine = runtime[I].selectedUnit.load(std::memory_order_relaxed) == unit && at && now >= at;
            const bool recent = mine && now - at <= kFreshSelectMs;
            // No fresh verdict: hold only the exact seat the selection proved.
            const bool held = mine && !recent && now - at <= kSeatHoldMs &&
                              runtime[I].selectedParent.load(std::memory_order_relaxed) == before.parent &&
                              runtime[I].selectedSeat.load(std::memory_order_relaxed) == before.seat;
            // CE: with ce_vehicle_eye_every_frame, a seated camera frame with
            // no fresh selector verdict is still placed, but only from the hull
            // eye below (never the native head fallback), so enter/exit cameras
            // in the first second (before the seat settles) stay native. A headset
            // log showed 448 selector calls, positioned=1, frameAnchored=0.
            const bool verdict = mode == 2 || recent || held;
            const bool everyFrame = I == 0 && g_config.ce_vehicle_eye_every_frame;
            if (!verdict && !everyFrame)
                return;
            if (mode != 2 && verdict)
                (recent ? runtime[I].recentSelect : runtime[I].seatHold).fetch_add(1, std::memory_order_relaxed);
            bool hullEye = false;
            if (!Head(I, before, point))
            {
                runtime[I].missingMarker.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!Owner(I, unit, after) || before != after)
                return;
            if constexpr (I == 0)
            {
                // CE (the earlier CE VR mod): while the vehicle frame is proven, the eye is
                // the seated BODY point frozen in hull coordinates once entry has
                // settled (1 s), plus the seat's eye offset and the per-model trims.
                // The live head marker sits low in the hull in CE (even with the frame
                // proven, the eye sat mid-bottom) and is only the unproven fallback.
                auto& r = runtime[I];
                haloce_vehicle_frame::Frame vf{};
                const bool frame = HaloCEControls_ReadVehicleFrame(vf) && vf.seat == before.seat;
                if (frame)
                {
                    r.seatLastFrameMs = now;
                    if (vf.vehicle != r.seatVehicle || vf.seat != r.seatIndex)
                    {
                        r.seatVehicle = vf.vehicle;
                        r.seatIndex = vf.seat;
                        r.seatEntryMs = now;
                        r.seatLocalValid = false;
                        r.seatAnchorLogged = false;
                    }
                    if (!r.seatLocalValid && now - r.seatEntryMs >= kSeatSettleMs)
                        r.seatLocalValid = haloce_vehicle_frame::OccupantLocal(vf, r.seatLocal);
                    if (r.seatLocalValid)
                    {
                        const auto title = kBindings[I].title;
                        const auto base = haloce_vehicle_frame::SeatEyeFor(before.vehicleIdentity, before.seat);
                        const haloce_vehicle_frame::SeatEyeOffset offset{
                            base.forward +
                                ConfigVehicleModelCam(g_config, title, before.vehicleIdentity, before.seat, 0),
                            base.left - ConfigVehicleModelCam(g_config, title, before.vehicleIdentity, before.seat, 2),
                            base.up + ConfigVehicleModelCam(g_config, title, before.vehicleIdentity, before.seat, 1)};
                        haloce_vehicle_frame::Vec3 eye{};
                        if (haloce_vehicle_frame::SeatEye(vf, r.seatLocal, offset, Game_GetWorldScale(), eye))
                        {
                            point[0] = eye.x;
                            point[1] = eye.y;
                            point[2] = eye.z;
                            hullEye = true;
                            const float wu = Game_GetWorldScale();
                            const float inv = std::isfinite(wu) && wu > 0.0f ? 1.0f / wu : 0.0f;
                            r.proofEye[0].store(r.seatLocal.x * inv + offset.forward, std::memory_order_relaxed);
                            r.proofEye[1].store(r.seatLocal.y * inv + offset.left, std::memory_order_relaxed);
                            r.proofEye[2].store(r.seatLocal.z * inv + offset.up, std::memory_order_relaxed);
                            r.proofSeatLocal[0].store(r.seatLocal.x * inv, std::memory_order_relaxed);
                            r.proofSeatLocal[1].store(r.seatLocal.y * inv, std::memory_order_relaxed);
                            r.proofSeatLocal[2].store(r.seatLocal.z * inv, std::memory_order_relaxed);
                            r.proofSeat.store(vf.seat, std::memory_order_relaxed);
                            r.proofEyeMs.store(now, std::memory_order_release);
                            if (!verdict)
                                r.everyFrame.fetch_add(1, std::memory_order_relaxed);
                            r.frameAnchored.fetch_add(1, std::memory_order_relaxed);
                            if (!r.seatAnchorLogged)
                            {
                                r.seatAnchorLogged = true;
                                r.anchorLogPending.store(true, std::memory_order_release);
                            }
                        }
                    }
                }
                // A brief gap (one unproven or torn sample) keeps the frozen seat;
                // only a real absence restarts the 1 s settle.
                else if (r.seatVehicle != UINT32_MAX && now - r.seatLastFrameMs > kSeatFrameGapMs)
                    r.seatVehicle = UINT32_MAX;
            }
            if (!verdict && !hullEye)
                return;
            // After the full native evaluation, including any slave-turret marker.
            std::memcpy(static_cast<uint8_t*>(result) + 4, point, 12);
            auto& r = runtime[I];
            // Menu edits the last positively identified seat in this generation.
            // Opening F1 pauses native ownership, so a time expiry would otherwise
            // silently rebind a slider to the per-game base during a drag.
            if (!r.trimWriting.test_and_set(std::memory_order_acquire))
            {
                r.trimVersion.fetch_add(1, std::memory_order_acq_rel);
                r.trimIdentity.store(before.vehicleIdentity, std::memory_order_relaxed);
                r.trimSeat.store(before.seat, std::memory_order_relaxed);
                r.trimGeneration.store(before.generation, std::memory_order_relaxed);
                r.trimVersion.fetch_add(1, std::memory_order_release);
                r.trimWriting.clear(std::memory_order_release);
            }
            runtime[I].positioned.fetch_add(1, std::memory_order_relaxed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("positioning the owned vehicle camera faulted");
            Fault(I);
        }
    }
    template <unsigned I>
    uint32_t __fastcall SelectHook(uint32_t unit, Mode<I>* mode)
    {
        auto& r = runtime[I];
        r.callbacks.fetch_add(1, std::memory_order_acq_rel);
        uint32_t result = 0;
        __try
        {
            result = reinterpret_cast<SelectorFn<I>>(r.original[Selector])(unit, mode);
            const uint64_t now = GetTickCount64();
            NativeVehicleCameraOwner seat{};
            const bool selected = SelectOwned<I>(unit, mode) && Owner(I, unit, seat);
            const bool localUnit = selected || r.selectedUnit.load(std::memory_order_relaxed) == unit;
            if (localUnit)
            {
                const uint64_t last = r.selectorLastMs.exchange(now, std::memory_order_relaxed);
                if (last && now >= last && now - last < 10000)
                {
                    const uint64_t gap = now - last;
                    r.selectorGapSum.fetch_add(gap, std::memory_order_relaxed);
                    r.selectorGapCount.fetch_add(1, std::memory_order_relaxed);
                    if (gap > r.selectorGapMax.load(std::memory_order_relaxed))
                        r.selectorGapMax.store(gap, std::memory_order_relaxed);
                }
            }
            if (!selected && mode && Owner(I, unit, seat))
            {
                const int value = static_cast<int>(*mode);
                r.seatedUnselectedModes[value >= 0 && value < 7 ? value : 7].fetch_add(1, std::memory_order_relaxed);
            }
            if (selected)
            {
                result = 0;
                r.selected.fetch_add(1, std::memory_order_relaxed);
                r.selectedUnit.store(unit, std::memory_order_relaxed);
                r.selectedParent.store(seat.parent, std::memory_order_relaxed);
                r.selectedSeat.store(seat.seat, std::memory_order_relaxed);
                r.selectedAtMs.store(now, std::memory_order_release);
            }
            // Only the latest answer for the selected unit counts: its transition
            // or stock frame clears it, so the camera hook never positions an
            // enter/exit camera. Calls for other units leave it alone.
            else if (localUnit)
                r.selectedAtMs.store(0, std::memory_order_release);
        }
        __finally
        {
            r.callbacks.fetch_sub(1, std::memory_order_release);
        }
        return result;
    }
    template <unsigned I>
    void __fastcall CameraHook(uint32_t unit, const void* facing, void* result)
    {
        auto& r = runtime[I];
        r.callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            reinterpret_cast<CameraFn>(r.original[Camera])(unit, facing, result);
            PositionOwned<I>(unit, result);
        }
        __finally
        {
            r.callbacks.fetch_sub(1, std::memory_order_release);
        }
    }
    void* const hooks[3][2]{{reinterpret_cast<void*>(&SelectHook<0>), reinterpret_cast<void*>(&CameraHook<0>)},
        {reinterpret_cast<void*>(&SelectHook<1>), reinterpret_cast<void*>(&CameraHook<1>)},
        {reinterpret_cast<void*>(&SelectHook<2>), reinterpret_cast<void*>(&CameraHook<2>)}};
    bool Prove(uintptr_t base, size_t size, const Binding& binding)
    {
        if (binding.rva >= size)
            return false;
        const auto hit = sig::Find(base, size, binding.pattern);
        return hit == base + binding.rva && !sig::Find(hit + 1, base + size - hit - 1, binding.pattern);
    }
    bool Retire(unsigned i)
    {
        auto& r = runtime[i];
        r.requested.store(false, std::memory_order_release);
        for (unsigned j = 0; j < 2; ++j)
            if (r.enabled[j])
            {
                const auto status = MCCVR_DisableHookForRetirement(r.target[j]);
                if (status != MH_OK && status != MH_ERROR_DISABLED)
                    return false;
                r.enabled[j] = false;
            }
        // Only these two entries can enter this feature from the engine. Their
        // callback lease encloses Current/Owner/Head/Fault, including exception
        // cleanup. Requiring unwind metadata for the leaf Fault helper made every
        // retirement fail permanently in Release and retained the old title DLL.
        // The ingress ranges still cover the window before callback acquisition;
        // the count protects every nested helper and original until it returns.
        const void* functions[]{hooks[i][0], hooks[i][1]};
        const void* originals[]{r.original[0], r.original[1]};
        if (!WaitForNativeDetourQuiescence(functions, originals, 2, r.callbacks))
            return false;
        for (unsigned j = 0; j < 2; ++j)
            if (r.target[j])
            {
                if (MCCVR_RemoveHookChecked(r.target[j]) != MH_OK)
                    return false;
                r.target[j] = r.original[j] = nullptr;
            }
        if (r.module)
            FreeLibrary(r.module);
        r.module = nullptr;
        r.base = 0;
        r.generation = 0;
        r.marker = r.markerInterpolated = nullptr;
        r.trimIdentity.store(0);
        r.trimGeneration.store(0);
        r.trimSeat.store(-1);
        r.attempted = r.ready = false;
        r.faulted.store(false, std::memory_order_release);
        return true;
    }
    bool Install(unsigned i, size_t size)
    {
        auto& r = runtime[i];
        for (const auto& binding : kBindings[i].entries)
            if (!Prove(r.base, size, binding))
                return false;
        r.marker = reinterpret_cast<void*>(r.base + kBindings[i].markerRva);
        // H2EK object marker argument 6 selects frame-interpolated object nodes.
        // This exact retail entry is already independently cold-proved above.
        if (i == 1)
            r.markerInterpolated = reinterpret_cast<void*>(r.base + kBindings[i].entries[MarkerProof].rva);
        // Both detours are installed before admission. Partial installation stays
        // stock and is retired through the same quiescence path as a title change.
        for (unsigned j = 0; j < 2; ++j)
        {
            void* target = reinterpret_cast<void*>(r.base + kBindings[i].entries[j].rva);
            if (MCCVR_CreateHookChecked(target, hooks[i][j], &r.original[j]) != MH_OK)
                return false;
            r.target[j] = target;
            if (MH_EnableHook(target) != MH_OK)
                return false;
            r.enabled[j] = true;
        }
        return true;
    }
}
bool NativeVehicleFirstPerson_TrimTarget(GameTitle title, uint64_t& identity, int& seat) noexcept
{
    identity = 0;
    seat = -1;
    if (title != TitleAdapter_GetActiveTitle())
        return false;
    for (unsigned i = 0; i < 3; ++i)
        if (kBindings[i].title == title)
        {
            auto& r = runtime[i];
            const auto before = r.trimVersion.load(std::memory_order_acquire);
            if (before & 1u)
                return false;
            const auto candidate = r.trimIdentity.load(std::memory_order_relaxed);
            const auto candidateSeat = r.trimSeat.load(std::memory_order_relaxed);
            const auto generation = r.trimGeneration.load(std::memory_order_relaxed);
            if (before != r.trimVersion.load(std::memory_order_acquire) || !candidate || candidateSeat < 0 ||
                candidateSeat > 31 || !generation || generation != TitleAdapter_GetGeneration(title))
                return false;
            identity = candidate;
            seat = candidateSeat;
            return true;
        }
    return false;
}
void NativeVehicleFirstPerson_Poll()
{
    const auto active = TitleAdapter_GetActiveTitle();
    const uint64_t now = GetTickCount64();
    for (unsigned i = 0; i < 3; ++i)
    {
        auto& r = runtime[i];
        const auto title = kBindings[i].title;
        const auto generation = TitleAdapter_GetGeneration(title);
        if (r.module && (active != title || generation != r.generation))
        {
            if (!Retire(i))
            {
                if (now >= r.reportAt)
                {
                    r.reportAt = now + 2000;
                    LOG("Vehicle first person title=%u: cleanup pending; feature stock", unsigned(title));
                }
                continue;
            }
        }
        if (active != title || !generation)
            continue;
        if (!r.attempted && g_config.vehicle_first_person)
        {
            const auto* descriptor = TitleRegistry_Find(title);
            if (!descriptor || !GetModuleHandleExW(0, descriptor->moduleName, &r.module))
                continue;
            r.base = reinterpret_cast<uintptr_t>(r.module);
            r.generation = generation;
            r.attempted = true;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(r.base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(r.base + dos->e_lfanew);
            r.ready = Install(i, nt->OptionalHeader.SizeOfImage);
            LOG("Vehicle first person %s: %s; existing toggle, local seated head marker, native entry/exit; headset verification pending",
                descriptor->displayName, r.ready ? "installed" : "STOCK FALLBACK (binding/hook)");
        }
        r.requested.store(r.ready && g_config.vehicle_first_person, std::memory_order_release);
        if (r.module)
        {
            // Diagnostic: the seat's stable model identity whenever it changes. Add the
            // identity as a row in config.h (cockpit eye-up) to give that vehicle
            // a default; it is also the key of the vehicle_model_* config trims.
            static uint64_t loggedIdentity[3]{};
            static int loggedSeat[3]{-2, -2, -2};
            const uint64_t identity = r.trimIdentity.load(std::memory_order_relaxed);
            const int seat = r.trimSeat.load(std::memory_order_relaxed);
            if (identity && (identity != loggedIdentity[i] || seat != loggedSeat[i]))
            {
                LOG("Vehicle first person title=%u seat identity %016llX seat %d", unsigned(title),
                    static_cast<unsigned long long>(identity), seat);
                loggedIdentity[i] = identity;
                loggedSeat[i] = seat;
            }
        }
        if (i == 0 && r.module && now >= r.proofReportAt)
        {
            // Every 2 s while seated (a hull eye in the last 2 s).
            r.proofReportAt = now + 2000;
            const uint64_t at = r.proofEyeMs.load(std::memory_order_acquire);
            if (at && now >= at && now - at <= 2000)
            {
                int state = 0, agree = 0, disagree = 0;
                float speed = 0;
                HaloCEControls_VehicleProofStatus(state, agree, disagree, speed);
                // eye-vs-hull: from the hull origin; eye-vs-seat: from the seated
                // body point (the kit offset plus trims, e.g. Warthog driver 0.10/0/0.72).
                const float e[3]{r.proofEye[0].load(), r.proofEye[1].load(), r.proofEye[2].load()};
                const float b[3]{r.proofSeatLocal[0].load(), r.proofSeatLocal[1].load(), r.proofSeatLocal[2].load()};
                LOG("CE seat proof: %s speed=%.2f m/s agree=%d disagree=%d seat %d eye-vs-hull (%.3f,%.3f,%.3f) m "
                    "eye-vs-seat (%.3f,%.3f,%.3f) m [forward,left,up] hullEyeFrames=%llu everyFrame=%llu",
                    state == 1   ? "proven"
                    : state == 2 ? "refuted"
                                 : "pending",
                    speed, agree, disagree, r.proofSeat.load(), e[0], e[1], e[2], e[0] - b[0], e[1] - b[1], e[2] - b[2],
                    static_cast<unsigned long long>(r.frameAnchored.load()),
                    static_cast<unsigned long long>(r.everyFrame.load()));
            }
        }
        if (r.module && now >= r.reportAt)
        {
            r.reportAt = now + 5000;
            LOG("Vehicle first person title=%u enabled=%d selected=%llu positioned=%llu missingHead=%llu faults=%llu recentSelect=%llu%s",
                unsigned(title), r.requested.load() ? 1 : 0, r.selected.load(), r.positioned.load(),
                r.missingMarker.load(), r.faults.load(), r.recentSelect.load(),
                r.faulted.load() ? " STOCK FALLBACK (guarded access fault)" : "");
            const uint64_t gaps = r.selectorGapCount.exchange(0), sum = r.selectorGapSum.exchange(0),
                           maxGap = r.selectorGapMax.exchange(0);
            if (i == 0 && r.anchorLogPending.exchange(false, std::memory_order_acq_rel))
                LOG("CE seat: eye anchored on the proven hull frame (seat %d, seated body frozen after %llu ms "
                    "at hull-local %.2f/%.2f/%.2f wu)",
                    int(r.seatIndex), (unsigned long long)kSeatSettleMs, r.seatLocal.x, r.seatLocal.y, r.seatLocal.z);
            {
                uint64_t m[8]{};
                uint64_t any = 0;
                for (unsigned k = 0; k < 8; ++k)
                {
                    m[k] = r.seatedUnselectedModes[k].exchange(0, std::memory_order_relaxed);
                    any += m[k];
                }
                if (any)
                    LOG("Vehicle first person title=%u seated but not selected: native modes 0=%llu 1=%llu 2=%llu 3=%llu "
                        "4=%llu 5=%llu 6=%llu other=%llu (5 s)",
                        unsigned(title), m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]);
            }
            LOG("Vehicle first person title=%u selector cadence: calls=%llu avgGapMs=%llu maxGapMs=%llu seatHold=%llu frameAnchored=%llu",
                unsigned(title), gaps, gaps ? sum / gaps : 0ull, maxGap, r.seatHold.load(), r.frameAnchored.load());
        }
    }
}
