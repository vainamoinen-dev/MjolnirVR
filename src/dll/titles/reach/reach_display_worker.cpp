// reach_display_worker.cpp: Halo Reach's Present-side display worker (moved from vr.cpp in E2, unchanged).
// Admission of the engine swapchain, the display snapshots taken on the Present thread, the capture
// resources and their lease, the eye caches, and the render-access exports the camera core reads.
// vr.cpp calls in through reach_display_worker.h; the worker borrows core's display resource lock.
#include "../../../common/title_traits.h"
#include "../../../common/pause_world_lock.h"
#include "../../../common/vr_interaction_refinement_logic.h"
#include "../../../common/support_grip_logic.h"
#include "../../../common/support_grab_logic.h"
#include "../../../common/hud_reveal_logic.h"
#include "../../../common/weapon_hand_logic.h"
#include "../../../common/weapon_interaction_logic.h"
#include "../../../common/grenade_throw_logic.h"
#include "../../../common/weapon_profiles.h"
#include "../../../common/weapon_model_observation.h"
#include "../../../common/weapon_reload_target.h"
#include "../../core/weapon_accessory_renderer.h"
#include "../../core/aim_pose_trace.h"
#include "../../core/committed_aim_sample.h"
#include "../../core/two_hand_lab_runtime.h"
#include "../../core/telemetry_recorder.h"
#include "../../../common/support_diagnostics.h"
#include "../../../common/title_runtime_state.h"
#include <windows.h>
#include "../../../common/virtual_stock_logic.h"
#include "../../../common/virtual_stock_aim_continuity.h"
#include "../../../common/two_hand_input_smoothing.h"
#include "../../../common/virtual_stock_neutral_capture.h"
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
#include "../../core/vr.h"
#include "../../core/menu.h"
#include "../../core/native_menu_pointer.h"
#include "../../core/native_subtitles.h"
#include "../../../common/subtitle_logic.h"
#include "../../../common/dual_reticle_logic.h"
#include "../../../common/lens_anchor.h"
#include "../../../common/scope_window_logic.h"
#include "../../../common/scope_render_scale_logic.h"
#include "../../../common/glass_reprojection_logic.h"
#include "../../../common/scope_parallax.h"
#include "../../../common/game_menu_pointer.h"
#include "../../../common/fist_melee_logic.h"
#include "../../../common/exclusive_input.h"
#include "../../../common/game_menu_pointer_xr.h"
#include "../../core/game.h"
#include "../ce/haloce_stereo_core.h"
#include "../../../common/titles/ce/haloce_pause_logic.h"
#include "../../../common/titles/ce/haloce_reticle_logic.h"
#include "../../../common/vr_blit_shader.h"
#include "../ce/haloce_controls.h"
#include "../ce/haloce_first_person.h"
#include "../ce/haloce_hud.h"
#include "../ce/haloce_hud_layout_runtime.h"
#include "../h2/halo2_observer_6dof.h"
#include "../../core/d3d11_hook.h"
#include "../../core/d3d_state.h"
#include "../../core/smaa_resource.h"
#include "../../core/dlss.h"
#include "../../core/gpu_eye_timing.h"
#include "../../core/title_adapter.h"
#include "../../../common/dlss_logic.h"
#include "../../../common/dlss_frame_result.h"
#include "../../../common/dlss_depth_tracking.h"
#include <../common/authored_reticle_logic.h>
#include "../../../common/titles/h2/halo2_render_logic.h"
#include "../h2/halo2_stereo_core.h"
#include "../reach/reach_render_candidate.h"
#include "../../../common/titles/reach/reach_chud_logic.h"
#include "../../../common/log.h"
#include "../../../common/config.h"
#include "../../../common/cutscene_theater_logic.h"
#include "../../../common/frame_pacing_logic.h"
#include "../../../common/titles/h4/halo4_render_logic.h"
#include "../../../common/titles/h4/halo4_cui_reticle_logic.h"
#include "../../../common/titles/h4/halo4_world_collision_logic.h"
#include "../../../common/input_logic.h"
#include "../../../common/weapon_haptic_pulses.h"
#include "../../../common/titles/reach/reach_vehicle_logic.h"
#include "../../../common/scope_logic.h"
#include "../../../common/dxgi_view_format.h"
#include "../../../common/scope_sight_logic.h"
#include "../../../common/visor_zoom_logic.h"
#include "../../../common/cockpit_screen_logic.h"
#include "../../../common/vehicle_monitors_logic.h"
#include "../../../common/scope_log.h"
#include "../../../common/titles/ce/haloce_vehicle_frame.h"
#include "../../core/native_vehicle_first_person.h"
#include "../../../common/zoom_smoothing_logic.h"
#include "../../../common/vehicle_comfort_logic.h"
#include "../../../common/view_cache_logic.h"
#include "../../core/display_resource_lock.h"
#include "reach_display_worker.h"
using namespace vr_core;
using namespace reach_bridge;

namespace
{
    constinit ReachDirectCopyGate g_reachDirectCopyGate;
} // namespace
namespace reach_bridge
{
    constinit ReachModuleEpoch g_reachDisplayEpoch{};
} // namespace reach_bridge
namespace
{
    constinit uint64_t g_reachDisplayResourceRevision = 0;
    constinit uint64_t g_reachDisplayNextAttemptMs = 0;
    constinit uint64_t g_reachDisplayLastFailureLogMs = 0;
    constinit bool g_reachDisplayReadyLogged = false;
    constinit std::atomic<bool> g_reachPresentSelectedEligible{false};
    constinit std::atomic<uint64_t> g_reachPresentAvailabilitySetEpochMs{0};
    constinit std::atomic<uint32_t> g_reachPresentGeneration{0};
    constinit std::atomic<uintptr_t> g_reachPresentModuleBase{0};
    constinit std::atomic<uint64_t> g_reachDisplayLifecycleSerial{1};
    constinit std::atomic<uint64_t> g_reachPresentNextSnapshotMs{0};
} // namespace
namespace reach_bridge
{
    constinit std::atomic<bool> g_reachResizeActive{false};
} // namespace reach_bridge
namespace
{
    constinit ID3D11Texture2D* g_reachEyeCache[2]{};
    constinit ID3D11Texture2D* g_reachCaptureSource = nullptr;
    constinit ID3D11Texture2D* g_reachCaptureEyes[2]{};
    constinit ID3D11DeviceContext* g_reachCaptureContext = nullptr;
} // namespace
namespace reach_bridge
{
    constinit ReachDisplaySurfaceProof g_reachCaptureProof{};
    constinit D3D11_TEXTURE2D_DESC g_reachCaptureDesc{};
    constinit std::atomic<bool> g_reachCaptureEnabled{false};
    constinit std::atomic<uint32_t> g_reachCaptureUsers{0};
    constinit std::atomic<uint64_t> g_reachEyeSerial[2]{};
} // namespace reach_bridge
namespace
{
    template <typename T>
    class ReachComRef
    {
    public:
        ReachComRef() = default;
        ~ReachComRef() { Reset(); }
        ReachComRef(const ReachComRef&) = delete;
        ReachComRef& operator=(const ReachComRef&) = delete;

        T* Get() const noexcept { return m_value; }
        T* Detach() noexcept
        {
            T* value = m_value;
            m_value = nullptr;
            return value;
        }
        T** Put() noexcept
        {
            Reset();
            return &m_value;
        }
        void Reset() noexcept
        {
            if (m_value)
            {
                m_value->Release();
                m_value = nullptr;
            }
        }

    private:
        T* m_value = nullptr;
    };

    enum class ReachDisplayFailure : uint8_t
    {
        None = 0,
        StaticPreflight,
        ModuleReference,
        EngineFields,
        SwapchainContract,
        EyeAllocation,
        DeviceContext,
        Publication,
    };

    const char* ReachDisplayFailureName(ReachDisplayFailure failure)
    {
        switch (failure)
        {
        case ReachDisplayFailure::None:
            return "none";
        case ReachDisplayFailure::StaticPreflight:
            return "static-preflight";
        case ReachDisplayFailure::ModuleReference:
            return "module-reference";
        case ReachDisplayFailure::EngineFields:
            return "engine-fields";
        case ReachDisplayFailure::SwapchainContract:
            return "swapchain-contract";
        case ReachDisplayFailure::EyeAllocation:
            return "eye-allocation";
        case ReachDisplayFailure::DeviceContext:
            return "device-context";
        case ReachDisplayFailure::Publication:
            return "resource-publication";
        default:
            return "unknown";
        }
    }

    class ReachModuleReference
    {
    public:
        ~ReachModuleReference() = default;

        bool Acquire(uintptr_t moduleBase) noexcept
        {
            if (m_module || !moduleBase)
                return false;
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(moduleBase), &module) ||
                reinterpret_cast<uintptr_t>(module) != moduleBase)
            {
                return false;
            }
            m_module = module;
            return true;
        }

    private:
        HMODULE m_module = nullptr;
    };

#include "reach_display_admission.inl"

    void BumpReachDisplayLifecycleSerial() noexcept
    {
        uint64_t current = g_reachDisplayLifecycleSerial.load(std::memory_order_relaxed);
        while (current != std::numeric_limits<uint64_t>::max() &&
               !g_reachDisplayLifecycleSerial.compare_exchange_weak(
                   current, current + 1, std::memory_order_release, std::memory_order_relaxed))
        {
        }
    }
} // namespace
namespace reach_bridge
{
    void InvalidateReachPresentAdmission() noexcept
    {
        g_reachPresentSelectedEligible.store(false, std::memory_order_release);
        g_reachPresentAvailabilitySetEpochMs.store(0, std::memory_order_release);
        g_reachPresentGeneration.store(0, std::memory_order_release);
        g_reachPresentModuleBase.store(0, std::memory_order_release);
        BumpReachDisplayLifecycleSerial();
    }
} // namespace reach_bridge
namespace
{
    void CaptureReachDisplaySnapshot(IDXGISwapChain* presentSwapchain, const ReachDisplayAdmission& admission) noexcept;
} // namespace
namespace reach_bridge
{
    // Present contributes only a throttled safe-boundary snapshot with retained
    // swapchain-owned interfaces and descriptors. Hashing, allocation,
    // publication, and logging stay on TitleHookSupervisorThread.
    void ObserveReachPresentSwapchain(IDXGISwapChain* presentSwapchain) noexcept
    {
        ReachDisplayAdmission admission{};
        const bool eligible = ReadReachDisplayAdmission(admission) && admission.selected;
        const uint64_t availabilitySetEpochMs = eligible ? admission.availability.availabilitySetEpochMs : 0;
        const uint32_t generation = eligible ? admission.epoch.generation : 0;
        const uintptr_t moduleBase = eligible ? admission.epoch.moduleBase : 0;
        const bool changed =
            g_reachPresentSelectedEligible.load(std::memory_order_relaxed) != eligible ||
            g_reachPresentAvailabilitySetEpochMs.load(std::memory_order_relaxed) != availabilitySetEpochMs ||
            g_reachPresentGeneration.load(std::memory_order_relaxed) != generation ||
            g_reachPresentModuleBase.load(std::memory_order_relaxed) != moduleBase;
        if (changed)
        {
            g_reachPresentSelectedEligible.store(eligible, std::memory_order_release);
            g_reachPresentAvailabilitySetEpochMs.store(availabilitySetEpochMs, std::memory_order_release);
            g_reachPresentGeneration.store(generation, std::memory_order_release);
            g_reachPresentModuleBase.store(moduleBase, std::memory_order_release);
            BumpReachDisplayLifecycleSerial();
        }
        if (!eligible || !presentSwapchain)
            return;
        CaptureReachDisplaySnapshot(presentSwapchain, admission);
    }
} // namespace reach_bridge
namespace
{
    void ReleaseReachEyeCaches() noexcept
    {
        for (auto*& cache : g_reachEyeCache)
        {
            if (cache)
                cache->Release();
            cache = nullptr;
        }
    }

    bool InvalidateReachCaptureResources() noexcept
    {
        g_reachCaptureEnabled.store(false, std::memory_order_seq_cst);
        for (unsigned spin = 0; g_reachCaptureUsers.load(std::memory_order_seq_cst) != 0 && spin < 2000; ++spin)
        {
            Sleep(1);
        }
        if (g_reachCaptureUsers.load(std::memory_order_seq_cst) != 0)
            return false;
        if (g_reachCaptureSource)
            g_reachCaptureSource->Release();
        for (auto*& eye : g_reachCaptureEyes)
        {
            if (eye)
                eye->Release();
            eye = nullptr;
        }
        if (g_reachCaptureContext)
            g_reachCaptureContext->Release();
        g_reachCaptureSource = nullptr;
        g_reachCaptureContext = nullptr;
        g_reachCaptureProof = {};
        g_reachCaptureDesc = {};
        g_reachEyeSerial[0].store(0, std::memory_order_release);
        g_reachEyeSerial[1].store(0, std::memory_order_release);
        return true;
    }

    template <typename T>
    bool ReadReachLocal(uintptr_t address, T& value) noexcept
    {
        if (!address || sizeof(T) > std::numeric_limits<uintptr_t>::max() - address)
            return false;
        SIZE_T received = 0;
        return ReadProcessMemory(
                   GetCurrentProcess(), reinterpret_cast<const void*>(address), &value, sizeof(value), &received) &&
               received == sizeof(value);
    }

    uintptr_t ReachComIdentity(IUnknown* value) noexcept
    {
        if (!value)
            return 0;
        IUnknown* identity = nullptr;
        const HRESULT hr = value->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(&identity));
        const uintptr_t result = SUCCEEDED(hr) && identity ? reinterpret_cast<uintptr_t>(identity) : 0;
        if (identity)
            identity->Release();
        return result;
    }

    uintptr_t ReachResourceDeviceIdentity(ID3D11Resource* resource) noexcept
    {
        if (!resource)
            return 0;
        ReachComRef<ID3D11Device> device;
        resource->GetDevice(device.Put());
        return ReachComIdentity(device.Get());
    }

    ReachCopyShape ReachShape(const D3D11_TEXTURE2D_DESC& desc) noexcept
    {
        return {
            desc.Width,
            desc.Height,
            desc.MipLevels,
            desc.ArraySize,
            static_cast<uint32_t>(desc.Format),
            desc.SampleDesc.Count,
            desc.SampleDesc.Quality,
        };
    }

    // TitleHookSupervisorThread only. Preserve the exact allocation rejection stage without
    // putting logging or device queries in the eye/render hooks.
    void NoteReachEyeAllocationFailure(
        const char* stage, HRESULT result, ID3D11Device* device, const D3D11_TEXTURE2D_DESC& desc) noexcept
    {
        static uint64_t lastReportMs = 0;
        static HRESULT lastResult = S_OK;
        static const char* lastStage = nullptr;
        const uint64_t now = GetTickCount64();
        if (stage == lastStage && result == lastResult && now - lastReportMs < 5000)
            return;
        lastReportMs = now;
        lastStage = stage;
        lastResult = result;
        const HRESULT removed = device ? device->GetDeviceRemovedReason() : E_POINTER;
        LOG("Reach eye-allocation: stage=%s hr=0x%08X removed=0x%08X "
            "size=%ux%u fmt=%u samples=%u/%u mips=%u array=%u "
            "usage=%u bind=0x%X cpu=0x%X misc=0x%X",
            stage, static_cast<unsigned>(result), static_cast<unsigned>(removed), desc.Width, desc.Height,
            static_cast<unsigned>(desc.Format), desc.SampleDesc.Count, desc.SampleDesc.Quality, desc.MipLevels,
            desc.ArraySize, static_cast<unsigned>(desc.Usage), desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags);
    }

    bool EnsureReachEyeCaches(
        ID3D11Device* device, const D3D11_TEXTURE2D_DESC& sourceDesc, const ReachCopyShape& sourceShape) noexcept
    {
        const uintptr_t deviceIdentity = ReachComIdentity(device);
        bool reusable = deviceIdentity != 0;
        uintptr_t existingIdentities[2]{};
        for (size_t eye = 0; eye < 2 && reusable; ++eye)
        {
            if (!g_reachEyeCache[eye])
            {
                reusable = false;
                break;
            }
            D3D11_TEXTURE2D_DESC eyeDesc{};
            g_reachEyeCache[eye]->GetDesc(&eyeDesc);
            existingIdentities[eye] = ReachComIdentity(g_reachEyeCache[eye]);
            reusable = existingIdentities[eye] != 0 && ReachSameCopyShape(sourceShape, ReachShape(eyeDesc)) &&
                       ReachResourceDeviceIdentity(g_reachEyeCache[eye]) == deviceIdentity;
        }
        if (reusable && existingIdentities[0] != existingIdentities[1])
        {
            return true;
        }

        D3D11_TEXTURE2D_DESC cacheDesc = sourceDesc;
        cacheDesc.Usage = D3D11_USAGE_DEFAULT;
        cacheDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        cacheDesc.CPUAccessFlags = 0;
        cacheDesc.MiscFlags = 0;
        cacheDesc.MipLevels = 1;
        cacheDesc.ArraySize = 1;
        ReachComRef<ID3D11Texture2D> replacement[2];
        if (!device)
        {
            NoteReachEyeAllocationFailure("device-missing", E_POINTER, device, cacheDesc);
            return false;
        }
        for (int eye = 0; eye < 2; ++eye)
        {
            const HRESULT created = device->CreateTexture2D(&cacheDesc, nullptr, replacement[eye].Put());
            if (FAILED(created) || !replacement[eye].Get())
            {
                NoteReachEyeAllocationFailure(
                    eye ? "create-right" : "create-left", FAILED(created) ? created : E_POINTER, device, cacheDesc);
                return false;
            }
        }

        const uintptr_t replacementIdentities[2] = {
            ReachComIdentity(replacement[0].Get()), ReachComIdentity(replacement[1].Get())};
        D3D11_TEXTURE2D_DESC replacementDesc[2]{};
        replacement[0].Get()->GetDesc(&replacementDesc[0]);
        replacement[1].Get()->GetDesc(&replacementDesc[1]);
        if (!replacementIdentities[0] || !replacementIdentities[1] ||
            replacementIdentities[0] == replacementIdentities[1] ||
            !ReachSameCopyShape(sourceShape, ReachShape(replacementDesc[0])) ||
            !ReachSameCopyShape(sourceShape, ReachShape(replacementDesc[1])) ||
            ReachResourceDeviceIdentity(replacement[0].Get()) != deviceIdentity ||
            ReachResourceDeviceIdentity(replacement[1].Get()) != deviceIdentity)
        {
            NoteReachEyeAllocationFailure("replacement-identity-shape-device", E_FAIL, device, cacheDesc);
            return false;
        }

        // Commit only after both replacements pass every check. Reach never
        // releases or mutates Halo 3/ODST's shared g_eyeCache resources.
        ReleaseReachEyeCaches();
        g_reachEyeCache[0] = replacement[0].Detach();
        g_reachEyeCache[1] = replacement[1].Detach();
        return true;
    }

    struct ReachDisplayFields
    {
        IDXGISwapChain* engineSwapchain = nullptr;
        uint32_t specializationCount = 0;
        uintptr_t surfaceArray = 0;
        ID3D11RenderTargetView* record0Rtv = nullptr;
        ID3D11ShaderResourceView* record0Srv = nullptr;
        ID3D11RenderTargetView* selectedRtv = nullptr;
        uint32_t selectedSpecialization = 0;
    };

    bool ReadReachDisplayFields(const ReachModuleEpoch& epoch, ReachDisplayFields& fields) noexcept
    {
        if (!ReachModuleEpochValid(epoch))
            return false;
        const uintptr_t group = epoch.moduleBase + kReachDisplayGroupRva;
        if (!ReadReachLocal(epoch.moduleBase + kReachDisplaySwapchainRva, fields.engineSwapchain) ||
            !ReadReachLocal(group + kReachDisplaySurfaceCountOffset, fields.specializationCount) ||
            !ReadReachLocal(group + kReachDisplaySurfaceArrayOffset, fields.surfaceArray) || !fields.surfaceArray ||
            fields.surfaceArray > std::numeric_limits<uintptr_t>::max() - kReachDisplaySurfaceSrvOffset ||
            !ReadReachLocal(fields.surfaceArray + kReachDisplaySurfaceRtvOffset, fields.record0Rtv) ||
            !ReadReachLocal(fields.surfaceArray + kReachDisplaySurfaceSrvOffset, fields.record0Srv) ||
            !ReadReachLocal(epoch.moduleBase + kReachDisplaySelectedRtvRva, fields.selectedRtv) ||
            !ReadReachLocal(epoch.moduleBase + kReachSelectedSpecializationRva, fields.selectedSpecialization))
        {
            return false;
        }
        return fields.engineSwapchain && fields.record0Rtv && fields.record0Srv && fields.selectedRtv;
    }
} // namespace
namespace reach_bridge
{
    bool ReachPresentMatchesEngineSwapchain(const ReachModuleEpoch& epoch, IDXGISwapChain* presentSwapchain) noexcept
    {
        if (!ReachModuleEpochValid(epoch) || !presentSwapchain)
            return false;
        IDXGISwapChain* engineSwapchain = nullptr;
        return ReadReachLocal(epoch.moduleBase + kReachDisplaySwapchainRva, engineSwapchain) &&
               engineSwapchain == presentSwapchain;
    }
} // namespace reach_bridge
namespace
{
    enum class ReachDisplaySnapshotState : uint8_t
    {
        Free = 0,
        Writing,
        Ready,
        Reading,
    };

    struct ReachDisplaySnapshot
    {
        std::atomic<uint8_t> state{static_cast<uint8_t>(ReachDisplaySnapshotState::Free)};
        ReachModuleEpoch epoch{};
        uint64_t availabilitySetEpochMs = 0;
        uint64_t lifecycleSerial = 0;
        ReachDisplayFields fields{};
        ID3D11Texture2D* buffer0 = nullptr;
        ID3D11Device* device = nullptr;
        D3D11_TEXTURE2D_DESC buffer0Desc{};
        DXGI_SWAP_CHAIN_DESC swapchainDesc{};
        uintptr_t buffer0Identity = 0;
        uintptr_t deviceIdentity = 0;
        uintptr_t immediateContextIdentity = 0;
        uint32_t deviceCreationFlags = 0;
    };

    constexpr size_t kReachDisplaySnapshotCapacity = 3;
    constinit std::array<ReachDisplaySnapshot, kReachDisplaySnapshotCapacity> g_reachDisplaySnapshots{};

    bool ReachSameDisplayFields(const ReachDisplayFields& left, const ReachDisplayFields& right) noexcept
    {
        return left.engineSwapchain == right.engineSwapchain && left.specializationCount == right.specializationCount &&
               left.surfaceArray == right.surfaceArray && left.record0Rtv == right.record0Rtv &&
               left.record0Srv == right.record0Srv && left.selectedRtv == right.selectedRtv &&
               left.selectedSpecialization == right.selectedSpecialization;
    }

    void ReleaseReachDisplaySnapshot(ReachDisplaySnapshot& snapshot) noexcept
    {
        if (snapshot.device)
            snapshot.device->Release();
        if (snapshot.buffer0)
            snapshot.buffer0->Release();
        if (snapshot.fields.engineSwapchain)
            snapshot.fields.engineSwapchain->Release();
        snapshot.epoch = {};
        snapshot.availabilitySetEpochMs = 0;
        snapshot.lifecycleSerial = 0;
        snapshot.fields = {};
        snapshot.buffer0 = nullptr;
        snapshot.device = nullptr;
        snapshot.buffer0Desc = {};
        snapshot.swapchainDesc = {};
        snapshot.buffer0Identity = 0;
        snapshot.deviceIdentity = 0;
        snapshot.immediateContextIdentity = 0;
        snapshot.deviceCreationFlags = 0;
        snapshot.state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
    }

    class ReachDisplaySnapshotLease
    {
    public:
        ~ReachDisplaySnapshotLease()
        {
            if (m_snapshot)
                ReleaseReachDisplaySnapshot(*m_snapshot);
        }

        bool Acquire() noexcept
        {
            if (m_snapshot)
                return false;
            for (auto& snapshot : g_reachDisplaySnapshots)
            {
                uint8_t expected = static_cast<uint8_t>(ReachDisplaySnapshotState::Ready);
                if (snapshot.state.compare_exchange_strong(expected,
                        static_cast<uint8_t>(ReachDisplaySnapshotState::Reading), std::memory_order_acquire,
                        std::memory_order_relaxed))
                {
                    m_snapshot = &snapshot;
                    return true;
                }
            }
            return false;
        }

        const ReachDisplaySnapshot* Get() const noexcept { return m_snapshot; }

    private:
        ReachDisplaySnapshot* m_snapshot = nullptr;
    };
} // namespace
namespace reach_bridge
{
    void DiscardReachDisplaySnapshots(bool waitForWriters) noexcept
    {
        for (auto& snapshot : g_reachDisplaySnapshots)
        {
            uint8_t state = snapshot.state.load(std::memory_order_acquire);
            if (waitForWriters)
            {
                unsigned spins = 0;
                while (state == static_cast<uint8_t>(ReachDisplaySnapshotState::Writing))
                {
                    if (++spins < 1024)
                        YieldProcessor();
                    else
                        SwitchToThread();
                    state = snapshot.state.load(std::memory_order_acquire);
                }
            }
            uint8_t expected = static_cast<uint8_t>(ReachDisplaySnapshotState::Ready);
            if (snapshot.state.compare_exchange_strong(expected,
                    static_cast<uint8_t>(ReachDisplaySnapshotState::Reading), std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                ReleaseReachDisplaySnapshot(snapshot);
            }
        }
    }
} // namespace reach_bridge
namespace
{
    void CaptureReachDisplaySnapshot(IDXGISwapChain* presentSwapchain, const ReachDisplayAdmission& admission) noexcept
    {
        if (!presentSwapchain || !admission.selected || g_reachResizeActive.load(std::memory_order_acquire))
        {
            return;
        }

        // An overlay/video Present must never consume the Reach snapshot
        // throttle. This is a raw identity read only; the full stable-boundary
        // snapshot and COM retention still happen after the exact match.
        if (!ReachPresentMatchesEngineSwapchain(admission.epoch, presentSwapchain))
        {
            return;
        }

        const uint64_t now = GetTickCount64();
        uint64_t next = g_reachPresentNextSnapshotMs.load(std::memory_order_relaxed);
        if (now < next || !g_reachPresentNextSnapshotMs.compare_exchange_strong(
                              next, now + 250, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return;
        }

        ReachDisplaySnapshot* destination = nullptr;
        for (auto& snapshot : g_reachDisplaySnapshots)
        {
            uint8_t expected = static_cast<uint8_t>(ReachDisplaySnapshotState::Free);
            if (snapshot.state.compare_exchange_strong(expected,
                    static_cast<uint8_t>(ReachDisplaySnapshotState::Writing), std::memory_order_acquire,
                    std::memory_order_relaxed))
            {
                destination = &snapshot;
                break;
            }
        }
        if (!destination)
            return;
        if (g_reachResizeActive.load(std::memory_order_acquire))
        {
            destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
            return;
        }

        const uint64_t lifecycleSerial = g_reachDisplayLifecycleSerial.load(std::memory_order_acquire);
        ReachDisplayFields initial{};
        const bool initialReady = lifecycleSerial != 0 && lifecycleSerial != std::numeric_limits<uint64_t>::max() &&
                                  ReadReachDisplayFields(admission.epoch, initial) &&
                                  initial.engineSwapchain == presentSwapchain &&
                                  initial.specializationCount == kReachDisplaySurfaceCount &&
                                  initial.selectedSpecialization == 0 && initial.selectedRtv == initial.record0Rtv;
        if (!initialReady)
        {
            destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
            return;
        }

        // Capture only interfaces whose lifetime is owned by this live Present
        // receiver. Raw engine RTV/SRV values remain structural identities and
        // are never dereferenced across whole-rasterizer recovery.
        ReachComRef<ID3D11Texture2D> buffer0;
        ReachComRef<ID3D11Device> device;
        ReachComRef<ID3D11DeviceContext> immediateContext;
        DXGI_SWAP_CHAIN_DESC swapchainDesc{};
        D3D11_TEXTURE2D_DESC buffer0Desc{};
        const bool d3dReady =
            SUCCEEDED(
                presentSwapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(buffer0.Put()))) &&
            buffer0.Get() &&
            SUCCEEDED(presentSwapchain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(device.Put()))) &&
            device.Get() && SUCCEEDED(presentSwapchain->GetDesc(&swapchainDesc));
        if (!d3dReady)
        {
            destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
            return;
        }
        const uint32_t deviceCreationFlags = device.Get()->GetCreationFlags();
        if (deviceCreationFlags & D3D11_CREATE_DEVICE_SINGLETHREADED)
        {
            destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
            return;
        }
        buffer0.Get()->GetDesc(&buffer0Desc);
        device.Get()->GetImmediateContext(immediateContext.Put());
        const uintptr_t buffer0Identity = ReachComIdentity(buffer0.Get());
        const uintptr_t deviceIdentity = ReachComIdentity(device.Get());
        const uintptr_t immediateContextIdentity = ReachComIdentity(immediateContext.Get());
        if (!buffer0Identity || !deviceIdentity || !immediateContextIdentity)
        {
            destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
            return;
        }

        ReachDisplayFields final{};
        const bool finalReady =
            !g_reachResizeActive.load(std::memory_order_acquire) && ReadReachDisplayFields(admission.epoch, final) &&
            ReachSameDisplayFields(initial, final) &&
            g_reachDisplayLifecycleSerial.load(std::memory_order_acquire) == lifecycleSerial &&
            g_reachPresentSelectedEligible.load(std::memory_order_acquire) &&
            g_reachPresentAvailabilitySetEpochMs.load(std::memory_order_acquire) ==
                admission.availability.availabilitySetEpochMs &&
            g_reachPresentGeneration.load(std::memory_order_acquire) == admission.epoch.generation &&
            g_reachPresentModuleBase.load(std::memory_order_acquire) == admission.epoch.moduleBase;
        if (!finalReady)
        {
            destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Free), std::memory_order_release);
            return;
        }

        destination->epoch = admission.epoch;
        destination->availabilitySetEpochMs = admission.availability.availabilitySetEpochMs;
        destination->lifecycleSerial = lifecycleSerial;
        destination->fields = initial;
        presentSwapchain->AddRef();
        destination->buffer0 = buffer0.Detach();
        destination->device = device.Detach();
        destination->buffer0Desc = buffer0Desc;
        destination->swapchainDesc = swapchainDesc;
        destination->buffer0Identity = buffer0Identity;
        destination->deviceIdentity = deviceIdentity;
        destination->immediateContextIdentity = immediateContextIdentity;
        destination->deviceCreationFlags = deviceCreationFlags;
        destination->state.store(static_cast<uint8_t>(ReachDisplaySnapshotState::Ready), std::memory_order_release);
    }

    ReachDisplayContinuity MakeReachDisplayContinuity(const ReachModuleEpoch& epoch, uint64_t revision,
        uintptr_t buffer0Identity, const ReachDisplayFields& fields) noexcept
    {
        ReachDisplayContinuity continuity{};
        continuity.epoch = epoch;
        continuity.resourceRevision = revision;
        continuity.swapchainIdentity = reinterpret_cast<uintptr_t>(fields.engineSwapchain);
        continuity.buffer0Identity = buffer0Identity;
        continuity.surfaceArrayIdentity = fields.surfaceArray;
        continuity.record0RtvIdentity = reinterpret_cast<uintptr_t>(fields.record0Rtv);
        continuity.record0SrvIdentity = reinterpret_cast<uintptr_t>(fields.record0Srv);
        continuity.selectedRtvIdentity = reinterpret_cast<uintptr_t>(fields.selectedRtv);
        continuity.specializationCount = fields.specializationCount;
        continuity.selectedSpecialization = fields.selectedSpecialization;
        return continuity;
    }
} // namespace
namespace reach_bridge
{
    bool ResetReachDisplayCandidateLocked(bool teardown, bool releaseResources) noexcept
    {
        if (ReachModuleEpochValid(g_reachDisplayEpoch))
        {
            if (teardown)
                g_reachDirectCopyGate.Teardown(g_reachDisplayEpoch);
            else
                g_reachDirectCopyGate.Invalidate(g_reachDisplayEpoch);
        }
        if (teardown)
        {
            g_reachDisplayEpoch = {};
            g_reachDisplayResourceRevision = 0;
        }
        bool resourcesReleased = true;
        if (releaseResources)
        {
            resourcesReleased = InvalidateReachCaptureResources();
            if (resourcesReleased)
            {
                ReleaseReachEyeCaches();
                DiscardReachDisplaySnapshots(false);
            }
        }
        g_reachDisplayNextAttemptMs = 0;
        g_reachDisplayReadyLogged = false;
        return resourcesReleased;
    }
} // namespace reach_bridge
namespace
{
    bool BuildReachDisplayProof(const ReachDisplaySnapshot& snapshot, const ReachModuleEpoch& epoch,
        const ReachPreflightToken& preflight, ReachDisplaySurfaceProof& proof, ReachDisplayFailure& failure)
    {
        const ReachDisplayFields& initialFields = snapshot.fields;
        const uint64_t lifecycleSerial = snapshot.lifecycleSerial;
        failure = ReachDisplayFailure::EngineFields;
        if (!initialFields.engineSwapchain || !snapshot.buffer0 || !snapshot.device || !snapshot.buffer0Identity ||
            !snapshot.deviceIdentity || !snapshot.immediateContextIdentity ||
            (snapshot.deviceCreationFlags & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0 ||
            initialFields.specializationCount != kReachDisplaySurfaceCount ||
            initialFields.selectedSpecialization != 0 || initialFields.selectedRtv != initialFields.record0Rtv)
            return false;

        const uintptr_t bufferIdentity = snapshot.buffer0Identity;
        const DXGI_SWAP_CHAIN_DESC& swapchainDesc = snapshot.swapchainDesc;
        const D3D11_TEXTURE2D_DESC& sourceDesc = snapshot.buffer0Desc;
        // An injected retail run is authoritative over the
        // earlier static constructor reading: MCC's live Reach Present owner
        // is a two-buffer flip-discard chain. Keep this exact rather than
        // broadening admission to arbitrary flip-model swapchains.
        const bool swapchainContract =
            swapchainDesc.BufferCount == 2 && swapchainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD &&
            swapchainDesc.BufferDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && swapchainDesc.SampleDesc.Count == 1 &&
            swapchainDesc.SampleDesc.Quality == 0 &&
            (swapchainDesc.BufferUsage & (DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT)) ==
                (DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT);
        const ReachCopyShape sourceShape = ReachShape(sourceDesc);
        const bool sourceContract = ReachDisplayCopyShapeValid(sourceShape) &&
                                    sourceDesc.Usage == D3D11_USAGE_DEFAULT && sourceDesc.CPUAccessFlags == 0 &&
                                    (sourceDesc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)) ==
                                        (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        if (!swapchainContract || !sourceContract)
        {
            failure = ReachDisplayFailure::SwapchainContract;
            return false;
        }

        if (!EnsureReachEyeCaches(snapshot.device, sourceDesc, sourceShape) || !g_reachEyeCache[0] ||
            !g_reachEyeCache[1])
        {
            failure = ReachDisplayFailure::EyeAllocation;
            return false;
        }

        D3D11_TEXTURE2D_DESC eyeDesc[2]{};
        g_reachEyeCache[0]->GetDesc(&eyeDesc[0]);
        g_reachEyeCache[1]->GetDesc(&eyeDesc[1]);
        const ReachCopyShape eyeShape[2] = {ReachShape(eyeDesc[0]), ReachShape(eyeDesc[1])};
        const uintptr_t eyeIdentity[2] = {ReachComIdentity(g_reachEyeCache[0]), ReachComIdentity(g_reachEyeCache[1])};
        if (!eyeIdentity[0] || !eyeIdentity[1] || eyeIdentity[0] == eyeIdentity[1] ||
            eyeIdentity[0] == bufferIdentity || eyeIdentity[1] == bufferIdentity ||
            !ReachSameCopyShape(sourceShape, eyeShape[0]) || !ReachSameCopyShape(sourceShape, eyeShape[1]))
        {
            NoteReachEyeAllocationFailure("published-eye-identity-shape", E_FAIL, snapshot.device, sourceDesc);
            failure = ReachDisplayFailure::EyeAllocation;
            return false;
        }

        const uintptr_t deviceIdentity = snapshot.deviceIdentity;
        const uintptr_t contextIdentity = snapshot.immediateContextIdentity;
        const bool sameDevice = deviceIdentity && contextIdentity &&
                                ReachResourceDeviceIdentity(snapshot.buffer0) == deviceIdentity &&
                                ReachResourceDeviceIdentity(g_reachEyeCache[0]) == deviceIdentity &&
                                ReachResourceDeviceIdentity(g_reachEyeCache[1]) == deviceIdentity;
        const bool immediate = contextIdentity != 0;
        if (!sameDevice || !immediate)
        {
            NoteReachEyeAllocationFailure("published-eye-device-context", E_FAIL, snapshot.device, sourceDesc);
            failure = ReachDisplayFailure::DeviceContext;
            return false;
        }

        ReachDisplayFields finalFields{};
        if (!ReadReachDisplayFields(epoch, finalFields) ||
            finalFields.engineSwapchain != initialFields.engineSwapchain ||
            finalFields.specializationCount != initialFields.specializationCount ||
            finalFields.surfaceArray != initialFields.surfaceArray ||
            finalFields.record0Rtv != initialFields.record0Rtv || finalFields.record0Srv != initialFields.record0Srv ||
            finalFields.selectedRtv != initialFields.selectedRtv ||
            finalFields.selectedSpecialization != initialFields.selectedSpecialization)
        {
            failure = ReachDisplayFailure::EngineFields;
            return false;
        }
        if (g_reachDisplayResourceRevision == std::numeric_limits<uint64_t>::max())
        {
            failure = ReachDisplayFailure::Publication;
            return false;
        }
        ++g_reachDisplayResourceRevision;
        proof = {};
        proof.continuity =
            MakeReachDisplayContinuity(epoch, g_reachDisplayResourceRevision, bufferIdentity, finalFields);
        proof.continuity.lifecycleSerial = lifecycleSerial;
        proof.continuity.deviceIdentity = deviceIdentity;
        proof.continuity.immediateContextIdentity = contextIdentity;
        proof.continuity.eyeResourceIdentities[0] = eyeIdentity[0];
        proof.continuity.eyeResourceIdentities[1] = eyeIdentity[1];
        proof.preflight = preflight;
        proof.immediateContextIdentity = contextIdentity;
        proof.eyeResourceIdentities[0] = eyeIdentity[0];
        proof.eyeResourceIdentities[1] = eyeIdentity[1];
        proof.source = sourceShape;
        proof.eyes[0] = eyeShape[0];
        proof.eyes[1] = eyeShape[1];
        proof.readyEyeMask = 0x3u;
        proof.engineSwapchainMatchesPresent = true;
        proof.selectedRtvMatchesRecord0 = true;
        proof.swapchainContract = true;
        proof.sameDevice = true;
        proof.immediateContext = true;
        failure = ReachDisplayFailure::None;
        return ReachDisplaySurfaceProofComplete(proof);
    }

    bool PublishReachCaptureResources(
        const ReachDisplaySnapshot& snapshot, const ReachDisplaySurfaceProof& proof) noexcept
    {
        if (!snapshot.buffer0 || !snapshot.device || !g_reachEyeCache[0] || !g_reachEyeCache[1] ||
            !ReachDisplaySurfaceProofComplete(proof))
            return false;
        if (g_reachCaptureEnabled.load(std::memory_order_acquire) && g_reachCaptureSource == snapshot.buffer0 &&
            g_reachCaptureEyes[0] == g_reachEyeCache[0] && g_reachCaptureEyes[1] == g_reachEyeCache[1] &&
            ReachSameModuleEpoch(g_reachCaptureProof.continuity.epoch, proof.continuity.epoch) &&
            g_reachCaptureProof.continuity.lifecycleSerial == proof.continuity.lifecycleSerial)
            return true;

        if (!InvalidateReachCaptureResources())
            return false;
        ID3D11DeviceContext* context = nullptr;
        snapshot.device->GetImmediateContext(&context);
        if (!context || ReachComIdentity(context) != proof.continuity.immediateContextIdentity)
        {
            if (context)
                context->Release();
            return false;
        }
        snapshot.buffer0->AddRef();
        g_reachEyeCache[0]->AddRef();
        g_reachEyeCache[1]->AddRef();
        g_reachCaptureSource = snapshot.buffer0;
        g_reachCaptureEyes[0] = g_reachEyeCache[0];
        g_reachCaptureEyes[1] = g_reachEyeCache[1];
        g_reachCaptureContext = context;
        g_reachCaptureProof = proof;
        g_reachCaptureDesc = snapshot.buffer0Desc;
        g_reachCaptureEnabled.store(true, std::memory_order_seq_cst);
        return true;
    }

    void PollReachDisplayCandidate()
    {
        ReachExclusiveResourceLock lock(g_reachDisplayResourceLock);
        ReachDisplayAdmission admission{};
        if (!ReadReachDisplayAdmission(admission))
        {
            if (ReachModuleEpochValid(g_reachDisplayEpoch))
                g_reachDirectCopyGate.Invalidate(g_reachDisplayEpoch);
            g_reachDisplayReadyLogged = false;
            return;
        }

        if (!admission.resident)
        {
            ResetReachDisplayCandidateLocked(true, true);
            return;
        }
        if (!admission.selected)
        {
            if (ReachSameModuleEpoch(admission.epoch, g_reachDisplayEpoch))
            {
                // H3/Reach overlap is normal during MCC title transitions.
                // Preserve the resident Reach epoch and its revision high-water
                // so the same generation can safely re-arm after ambiguity.
                ResetReachDisplayCandidateLocked(false, true);
            }
            else
            {
                ResetReachDisplayCandidateLocked(true, true);
            }
            return;
        }

        static ReachModuleEpoch loggedAdmissionEpoch{};
        static uint64_t loggedAvailabilityEpoch = 0;
        if (!ReachSameModuleEpoch(loggedAdmissionEpoch, admission.epoch) ||
            loggedAvailabilityEpoch != admission.availability.availabilitySetEpochMs)
        {
            loggedAdmissionEpoch = admission.epoch;
            loggedAvailabilityEpoch = admission.availability.availabilitySetEpochMs;
            LOG("Reach display admission: selected Reach generation %u with resident mask 0x%X; "
                "verifying current native Present resources",
                admission.epoch.generation, admission.availability.availabilityMask);
        }

        if (!ReachSameModuleEpoch(admission.epoch, g_reachDisplayEpoch))
        {
            ResetReachDisplayCandidateLocked(true, true);
            if (!g_reachDirectCopyGate.AdvanceEpoch(admission.epoch))
                return;
            g_reachDisplayEpoch = admission.epoch;
        }

        const uint64_t lifecycleSerial = g_reachDisplayLifecycleSerial.load(std::memory_order_acquire);
        const bool presentAdmissionMatches =
            lifecycleSerial != std::numeric_limits<uint64_t>::max() &&
            g_reachPresentSelectedEligible.load(std::memory_order_acquire) &&
            g_reachPresentAvailabilitySetEpochMs.load(std::memory_order_acquire) ==
                admission.availability.availabilitySetEpochMs &&
            g_reachPresentGeneration.load(std::memory_order_acquire) == admission.epoch.generation &&
            g_reachPresentModuleBase.load(std::memory_order_acquire) == admission.epoch.moduleBase;
        const ReachPreflightToken preflight = ReachRenderCandidate_GetPreflight(admission.epoch);
        if (!presentAdmissionMatches || !preflight.Complete() || !ReachRenderCandidate_IsPreflightCurrent(preflight))
        {
            g_reachDirectCopyGate.Invalidate(admission.epoch);
            g_reachDisplayReadyLogged = false;
            const uint64_t waitingNow = GetTickCount64();
            if (!g_reachDisplayLastFailureLogMs || waitingNow - g_reachDisplayLastFailureLogMs >= 2000)
            {
                g_reachDisplayLastFailureLogMs = waitingNow;
                LOG("Reach display worker waiting: generation=%u residentMask=0x%X "
                    "currentPresent=%d preflight=%d",
                    admission.epoch.generation, admission.availability.availabilityMask,
                    presentAdmissionMatches ? 1 : 0,
                    preflight.Complete() && ReachRenderCandidate_IsPreflightCurrent(preflight) ? 1 : 0);
            }
            return;
        }

        const uint64_t now = GetTickCount64();
        if (now < g_reachDisplayNextAttemptMs)
            return;
        g_reachDisplayNextAttemptMs = now + 250;
        // Every cold refresh revokes the old capability before touching any
        // engine or COM object. A failed replacement can never leave the prior
        // resource proof current.
        g_reachDirectCopyGate.Invalidate(admission.epoch);

        ReachModuleReference moduleReference;
        ReachDisplayFailure failure = ReachDisplayFailure::ModuleReference;
        if (!moduleReference.Acquire(admission.epoch.moduleBase))
        {
            g_reachDisplayReadyLogged = false;
        }
        else
        {
            ReachDisplaySnapshotLease snapshotLease;
            const bool haveSnapshot = snapshotLease.Acquire();
            const ReachDisplaySnapshot* snapshot = snapshotLease.Get();
            const bool snapshotMatches =
                haveSnapshot && snapshot && ReachSameModuleEpoch(snapshot->epoch, admission.epoch) &&
                snapshot->availabilitySetEpochMs == admission.availability.availabilitySetEpochMs &&
                snapshot->lifecycleSerial == lifecycleSerial;
            failure = snapshotMatches ? ReachDisplayFailure::SwapchainContract : ReachDisplayFailure::EngineFields;

            ReachDisplaySurfaceProof proof{};
            const bool built =
                snapshotMatches && BuildReachDisplayProof(*snapshot, admission.epoch, preflight, proof, failure);
            ReachDisplayAdmission finalAdmission{};
            const bool admissionStillCurrent =
                ReadReachDisplayAdmission(finalAdmission) && ReachSameDisplayAdmission(admission, finalAdmission) &&
                g_reachDisplayLifecycleSerial.load(std::memory_order_acquire) == lifecycleSerial &&
                g_reachPresentSelectedEligible.load(std::memory_order_acquire) &&
                g_reachPresentAvailabilitySetEpochMs.load(std::memory_order_acquire) ==
                    admission.availability.availabilitySetEpochMs &&
                g_reachPresentGeneration.load(std::memory_order_acquire) == admission.epoch.generation &&
                g_reachPresentModuleBase.load(std::memory_order_acquire) == admission.epoch.moduleBase &&
                ReachRenderCandidate_IsPreflightCurrent(preflight);
            const bool published = built && admissionStillCurrent && g_reachDirectCopyGate.Publish(proof) &&
                                   PublishReachCaptureResources(*snapshot, proof);
            if (published)
            {
                if (!g_reachDisplayReadyLogged)
                {
                    g_reachDisplayReadyLogged = true;
                    LOG("Reach display worker proof PASS: exact Present "
                        "buffer0, specialization0 structural continuity, "
                        "same device/context identity, and two private exact "
                        "eye caches; camera core remains gated by armed state "
                        "and exact current-frame render access");
                }
                return;
            }
            if (built && !admissionStillCurrent)
                failure = ReachDisplayFailure::Publication;
            else if (built)
                failure = ReachDisplayFailure::Publication;
            g_reachDisplayReadyLogged = false;
        }

        if (!g_reachDisplayLastFailureLogMs || now - g_reachDisplayLastFailureLogMs >= 2000)
        {
            g_reachDisplayLastFailureLogMs = now;
            LOG("Reach display worker proof waiting (%s); stock Reach remains active",
                ReachDisplayFailureName(failure));
        }
    }
} // namespace
void VR_ReachRenderCandidate_ColdPoll()
{
    PollReachDisplayCandidate();
}
bool VR_ReachDisplayReady(const ReachModuleEpoch& epoch)
{
    // Pin the plain proof fields exactly like BeginRenderAccess. The worker
    // disables publication and waits for this reader count before releasing or
    // zeroing them, so readiness cannot race resource invalidation.
    g_reachCaptureUsers.fetch_add(1, std::memory_order_seq_cst);
    const bool ready = g_reachCaptureEnabled.load(std::memory_order_seq_cst) &&
                       ReachSameModuleEpoch(g_reachCaptureProof.continuity.epoch, epoch) &&
                       ReachRenderCandidate_IsPreflightCurrent(g_reachCaptureProof.preflight);
    g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
    return ready;
}

bool VR_ReachBeginRenderAccess(
    const ReachModuleEpoch& epoch, const ReachPreparedFrameToken& prepared, ReachVrRenderAccess& access)
{
    access = {};
    g_reachCaptureUsers.fetch_add(1, std::memory_order_seq_cst);
    if (!g_reachCaptureEnabled.load(std::memory_order_seq_cst) || !prepared.Ready() ||
        !ReachSameModuleEpoch(prepared.Epoch(), epoch) || !g_reachCaptureSource || !g_reachCaptureContext ||
        !g_reachCaptureEyes[0] || !g_reachCaptureEyes[1] ||
        !ReachSameModuleEpoch(g_reachCaptureProof.continuity.epoch, epoch) ||
        !ReachRenderCandidate_IsPreflightCurrent(g_reachCaptureProof.preflight))
    {
        g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    access.proof = g_reachCaptureProof;
    access.source = g_reachCaptureSource;
    access.eyes[0] = g_reachCaptureEyes[0];
    access.eyes[1] = g_reachCaptureEyes[1];
    access.context = g_reachCaptureContext;
    access.preparedSerial = prepared.Serial();
    access.active = true;
    return true;
}
bool VR_ReachCopyEye(ReachVrRenderAccess& access, int eye)
{
    if (!access.active || eye < 0 || eye > 1 || !access.context || !access.source || !access.eyes[eye] ||
        !g_reachCaptureEnabled.load(std::memory_order_seq_cst))
        return false;
    access.context->CopyResource(access.eyes[eye], access.source);
    g_reachEyeSerial[eye].store(access.preparedSerial, std::memory_order_release);
    return true;
}

void VR_ReachEndRenderAccess(ReachVrRenderAccess& access)
{
    if (!access.active)
        return;
    access = {};
    g_reachCaptureUsers.fetch_sub(1, std::memory_order_seq_cst);
}
void VR_AfterResizeBuffers(IDXGISwapChain*)
{
    g_reachPresentNextSnapshotMs.store(0, std::memory_order_relaxed);
    g_reachResizeActive.store(false, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_reachDisplayResourceLock);
}
