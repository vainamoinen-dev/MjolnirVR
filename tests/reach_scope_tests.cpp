// Tests the Reach scope pass (reach_scope.inl): the per-frame render, skip and retire decisions, fault
// restore of the camera state, the cockpit screen and vehicle monitor views, and the occlusion diagnostics.
// Compiles reach_scope.inl with scope_logic.cpp against stub VR and native view functions; built as
// halomccvr_reach_scope_tests. No game process or GPU work: the render calls are stubs.
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../src/common/scope_logic.h"
#include "../src/common/titles/reach/reach_wind_replay.h"
#include "../src/common/titles/reach/reach_render_logic.h"
#include "../src/common/cockpit_screen_logic.h"
#include "../src/common/titles/reach/reach_occlusion_diag.h"
#include "../src/dll/core/guard_count.h"
static unsigned checks{};
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
constexpr uintptr_t kReachPvSnapshotBegin = kReachPlayerViewCameraStateOffset;
constexpr size_t kReachCompactCameraBytes = kReachCompactCameraSize;
constexpr size_t kReachPvSnapshotBytes = kReachLastWindowFlagOffset + 1 - kReachPvSnapshotBegin;
struct ReachVrRenderAccess
{
    bool active = true;
};
// clang-format off
#pragma warning(suppress : 4324) // the production layout: the alignment padding is intended
struct ReachScopeFrame { bool ready = false; alignas(16) unsigned char compact[kReachCompactCameraBytes]{}, derived[kReachDerivedBlockSize]{}; };
// clang-format on
struct ReachVehicleFrame
{
    bool active = false, carrierBasisValid = false;
    int32_t seatIndex = -1;
    ReachVehicleId identity = ReachVehicleId::Unknown;
    float cameraBase[3]{}, rawCarrierForward[3]{};
};
struct ReachOwnerScope
{
    uintptr_t workspace = 0;
    bool cutsceneTheater = false;
    ReachVehicleFrame vehicle{};
    unsigned char headCenter[kReachCompactCameraBytes]{};
    ReachScopeFrame scope{};
};
static ReachOwnerScope g_reachOwnerScope;
static bool g_reachScopeRendering = false;
static std::atomic<bool> g_scopeRenderActive{false}, g_reachCinematicLocked{false};
static std::atomic<float> g_aimFwdX{1}, g_aimFwdY{0}, g_aimFwdZ{0};
static std::atomic<uint32_t> g_reachScopeFaultGeneration{0}, g_reachScopeFaults{0}, g_reachWindReplayFaults{0};
static std::atomic<uintptr_t> g_reachFirstPersonRenderGateReturn{1};
static struct
{
    bool active = false;
    uint64_t serial = 42;
} g_reachFpCameraEyeScope;
static unsigned char fixtureWind[48]{};
static struct
{
    std::atomic<uint32_t> generation{7};
    uintptr_t base = 0;
    void* hudDrawWidgetTarget = fixtureWind;
    void* decoratorWindState = fixtureWind;
} g_reachCamera;
static unsigned phase{}, faultAt{}, draws{}, copies{}, logs{};
static bool requested = true, resources = true, image = false;
static float zoom = 6, fixtureAspect = 16.f / 9.f;
static alignas(16) unsigned char fixtureWorkspace[kReachRenderScopeSnapshotSize], fixtureView[0xA40],
    fixtureSavedWorkspace[sizeof(fixtureWorkspace)], savedView[sizeof(fixtureView)];
static uintptr_t ownerGlobal{};
static void Phase()
{
    if (++phase == faultAt)
        RaiseException(0xE0431111, 0, 0, nullptr);
}
static bool Frustum(void*, float* b)
{
    Phase();
    for (int i = 0; i < 4; ++i)
        b[i] = 1;
    return true;
}
static void Projection(void* camera, float*, void* derived, float)
{
    Phase();
    std::memset(derived, 0xAD, kReachDerivedBlockSize);
    auto* p = reinterpret_cast<float*>(static_cast<unsigned char*>(derived) + kReachDerivedProjectionOffset);
    const float y = std::tan(*reinterpret_cast<float*>(static_cast<unsigned char*>(camera) + 0x28) / 2);
    p[0] = 1 / (y * fixtureAspect);
    p[5] = 1 / y;
}
static void State(void* p, void*)
{
    Phase();
    std::memset(p, 0xCE, 32);
}
static void Matrix(void* p, void*, void*, void*, void*)
{
    Phase();
    std::memset(p, 0xAB, 64);
}
static void Commit()
{
    Phase();
}
static struct
{
    decltype(&Frustum) frustum = Frustum;
    decltype(&Projection) projection = Projection;
    decltype(&State) cameraState = State;
    decltype(&Matrix) matrix = Matrix;
    decltype(&Commit) commitOuterCamera = Commit;
} g_reachHelpers;
static bool VR_ScopeShouldRenderThisFrame()
{
    return requested;
}
static void VR_InvalidateScopeImage()
{
    image = false;
}
static bool VR_ReachScopeReady(const ReachVrRenderAccess& a)
{
    return a.active && resources;
}
static bool VR_GetScopeRenderAspect(float& a)
{
    a = fixtureAspect;
    return resources;
}
static float VR_GetScopeZoom()
{
    return zoom;
}
static bool visorHeadView = false;
static bool VR_VisorZoomHeadView()
{
    return visorHeadView;
}
struct CockpitTestConfig
{
    bool vehicle_cockpit_screen = false;
    float cockpit_screen_ahead_m = 0, cockpit_screen_fov_deg = 90;
} g_config;
static std::atomic<bool> noted{false};
static void VR_NoteCockpitScreen(bool wanted)
{
    noted = wanted;
}
static bool ControllerWorldBasis(float* b)
{
    b[0] = b[4] = b[8] = 1;
    return true;
}
static bool ReachWindCopy(void* d, const void* s, size_t n)
{
    std::memcpy(d, s, n);
    return true;
}
// 0 = render, 1 = skipped (pending lightmap restore), 2 = failed.
static int viewCallOutcome = 0;
static bool ReachCallPlayerViewWithEyeScopedSuppressions(uintptr_t pv, bool* transient = nullptr)
{
    if (transient)
        *transient = viewCallOutcome == 1;
    if (viewCallOutcome)
        return false;
    ++draws;
    Check(g_reachScopeRendering && g_scopeRenderActive, "scope suppression owns only optional pass");
    Check(pv == reinterpret_cast<uintptr_t>(fixtureView), "same proven player view");
    Check(fixtureView[kReachLastWindowFlagOffset] == 0, "scope cannot run final-window cleanup");
    Check(ownerGlobal == pv + kReachPlayerViewCameraStateOffset, "native owner rearmed");
    Check(std::memcmp(fixtureWorkspace, fixtureWorkspace + kReachSecondaryCompactOffset, kReachCompactCameraBytes) == 0,
        "scope primary/secondary coherent");
    const auto* p =
        reinterpret_cast<float*>(fixtureWorkspace + kReachPrimaryDerivedOffset + kReachDerivedProjectionOffset);
    const auto lens = ComputeScopeProjectionTangents(zoom, fixtureAspect);
    Check(std::fabs(p[0] * lens.horizontal - 1) < .005f && std::fabs(p[5] * lens.vertical - 1) < .005f,
        "native lens magnification matches source");
    std::memset(fixtureWind, 0xCC, sizeof(fixtureWind));
    ownerGlobal = 0;
    Phase();
    return true;
}
static bool VR_ReachCopyScope(ReachVrRenderAccess&)
{
    Phase();
    ++copies;
    image = true;
    return true;
}
#define LOG(...) (++logs)
std::atomic<unsigned> g_reachScopeStage{0};
unsigned VR_ReachScopeNotReadyStage(const ReachVrRenderAccess&)
{
    return 57;
}
#include "../src/common/vehicle_monitors_logic.h"
// Vehicle monitors stub: off unless a check turns it on (forward view, kit row).
static bool monitorsOn = false;
static int monitorCalls = 0;
static const char* monitorVehicle = nullptr;
static bool VR_VehicleMonitorCamera(int game, const char* vehicle, int cls, const float anchor[3],
    const float hullForward[3], float unitsPerMeter, float pos[3], float fwd[3], float up[3], float& fov)
{
    ++monitorCalls;
    monitorVehicle = vehicle;
    if (!monitorsOn)
        return false;
    const auto* row = vehicle_monitors::RowFor(static_cast<vehicle_monitors::Game>(game), vehicle);
    const auto spec =
        vehicle_monitors::SpecFor(row, static_cast<cockpit_screen::Class>(cls), vehicle_monitors::View::Left);
    if (!cockpit_screen::CameraPose(
            anchor, hullForward, spec.aheadM * unitsPerMeter, spec.upM * unitsPerMeter, pos, fwd, up))
        return false;
    vehicle_monitors::YawAboutUp(fwd, up, spec.yawDeg);
    fov = spec.fovDeg;
    return true;
}
static void ReachScopeLogCamera(const float*, const float*) {}
static const float* ReachScopeOrigin(const float* head, const float*, float*)
{
    return head;
}
static void ReachScopeLogFov(float, float) {}
static float ReachScopeCullVertical(float, float needed)
{
    return needed;
}
#include "../src/dll/titles/reach/reach_scope.inl"
#undef LOG
static void Reset()
{
    g_reachOwnerScope = {};
    g_reachOwnerScope.workspace = reinterpret_cast<uintptr_t>(fixtureWorkspace);
    float* h = reinterpret_cast<float*>(g_reachOwnerScope.headCenter);
    h[0] = 2;
    h[1] = 3;
    h[2] = 4;
    h[3] = 1;
    h[8] = 1;
    h[10] = 1.8f;
    for (size_t i = 0; i < sizeof(fixtureWorkspace); ++i)
        fixtureWorkspace[i] = static_cast<unsigned char>(i * 13);
    for (size_t i = 0; i < sizeof(fixtureView); ++i)
        fixtureView[i] = static_cast<unsigned char>(i * 7);
    std::memcpy(fixtureSavedWorkspace, fixtureWorkspace, sizeof(fixtureWorkspace));
    std::memcpy(savedView, fixtureView, sizeof(fixtureView));
    ownerGlobal = reinterpret_cast<uintptr_t>(fixtureView) + kReachPlayerViewCameraStateOffset;
    g_reachCamera.base = reinterpret_cast<uintptr_t>(&ownerGlobal) - kReachRenderCameraOwnerRva;
    std::memset(fixtureWind, 0x34, sizeof(fixtureWind));
    g_reachScopeRendering = false;
    g_scopeRenderActive = false;
    g_reachScopeFaultGeneration = 0;
    g_reachScopeFaults = 0;
    g_reachCinematicLocked = false;
    g_reachFpCameraEyeScope = {true, 42};
    phase = faultAt = draws = copies = 0;
    requested = resources = true;
    image = false;
    g_aimFwdX = 1;
    g_aimFwdY = 0;
}
static bool Prepare(ReachVrRenderAccess& access)
{
    unsigned char derived[kReachDerivedBlockSize]{};
    float bounds[4]{};
    Projection(g_reachOwnerScope.headCenter, bounds, derived, 0);
    ReachPrepareScope(g_reachOwnerScope, derived, access);
    return g_reachOwnerScope.scope.ready;
}
static void Restored()
{
    Check(std::memcmp(fixtureWorkspace, fixtureSavedWorkspace, sizeof(fixtureWorkspace)) == 0,
        "all camera and callback bytes restored");
    Check(std::memcmp(fixtureView, savedView, sizeof(fixtureView)) == 0, "all player-view bytes/last-window restored");
    Check(ownerGlobal == reinterpret_cast<uintptr_t>(fixtureView) + kReachPlayerViewCameraStateOffset,
        "owner restored for first real eye");
    Check(!g_scopeRenderActive && !g_reachScopeRendering && g_reachFpCameraEyeScope.active &&
              g_reachFpCameraEyeScope.serial == 42,
        "FP receipts and scope ownership restored");
    for (auto b : fixtureWind)
        Check(b == 0x34, "scope wind advance removed before real eye pair");
}
static void OcclusionDiagTests()
{
    using namespace reach_occlusion;
    uint32_t keys[8];
    uint8_t states[8 * kStateStride]{};
    for (uint32_t& k : keys)
        k = 0xFFFFFFFFu;
    keys[0] = 5; // window 0, visible, result back
    keys[1] = 6 | 0x1000000u;
    states[1 * 16] = 2 | 4;
    states[1 * 16 + 3] = 1; // occluded, fresh: culls, flagged
    keys[2] = 7;
    states[2 * 16] = 2 | 4;
    states[2 * 16 + 3] = 3; // occluded but stale: no cull
    keys[3] = 8;
    states[3 * 16] = 1;
    states[3 * 16 + 3] = 1; // pending, one frame late
    keys[4] = 9;
    states[4 * 16] = 1;
    states[4 * 16 + 3] = 7; // pending, 3+ late
    keys[5] = 10 | (1u << 17);
    states[5 * 16] = 4; // window 1: ignored
    keys[6] = 0x40000000u | 11;
    states[6 * 16] = 4; // deleted: ignored
    Tally t{};
    Count(keys, states, 8, 0, t);
    Check(t.live == 5 && t.flagged == 1 && t.occluded == 2 && t.culling == 1 && t.pending == 2 &&
              t.pendingAge[1] == 1 && t.pendingAge[3] == 1,
        "z40 occlusion tally: live/flagged/occluded/culling/pending by window");
    Second s{};
    s.Add(t, true, true, 3);
    s.Add(t, true, false, 2);
    s.Add(Tally{}, false, false, 2);
    char line[640];
    Format(s, line, sizeof line);
    Check(
        std::strstr(line,
            "scope gun: 2 frames (scope pass 1, passes/frame 2.50) live 5 flagged 1.0 occluded 2.0 culling 1.0 (max 1)") &&
            std::strstr(line, "no scope: 1 frames (scope pass 0, passes/frame 2.00) live 0"),
        "z40 occlusion line splits scope and no-scope frames");
}

int main()
{
    OcclusionDiagTests();
    ReachVrRenderAccess access{};
    for (float z : {6.f, 12.f, 24.f})
        for (float a : {1.f, 4.f / 3.f, 16.f / 9.f})
        {
            Reset();
            zoom = z;
            fixtureAspect = a;
            Check(Prepare(access), "scope camera prepared");
            ReachRenderScope(reinterpret_cast<uintptr_t>(fixtureView), access);
            Check(draws == 1 && copies == 1 && image, "one separate optical draw/copy");
            Restored();
        }
    for (unsigned fail = 1; fail <= 5; ++fail)
    {
        Reset();
        Check(Prepare(access), "fault fixture prepared");
        phase = 0;
        faultAt = fail;
        ReachRenderScope(reinterpret_cast<uintptr_t>(fixtureView), access);
        Check(draws <= 1 && copies == 0 && !image && g_reachScopeFaultGeneration == 7,
            "fault isolates scope without native draw replay");
        Restored();
    }
    {
        // A pending lightmap restore skips one frame; a real failure retires.
        Reset();
        Check(Prepare(access), "transient fixture prepared");
        viewCallOutcome = 1;
        ReachRenderScope(reinterpret_cast<uintptr_t>(fixtureView), access);
        Check(g_reachScopeStage == 96 && g_reachScopeFaultGeneration == 0 && g_reachScopeFaults == 0,
            "A2-04: a pending lightmap restore skips the frame without retiring the scope");
        Restored();
        Reset();
        Check(Prepare(access), "failure fixture prepared");
        viewCallOutcome = 2;
        ReachRenderScope(reinterpret_cast<uintptr_t>(fixtureView), access);
        Check(g_reachScopeStage == 97 && g_reachScopeFaultGeneration == 7,
            "A2-04: a real failure still retires the scope");
        Restored();
        viewCallOutcome = 0;
    }
    Reset();
    g_aimFwdX = -1;
    Check(!Prepare(access), "back-facing lens cannot corrupt head perspective culling");
    Reset();
    resources = false;
    Check(!Prepare(access), "missing GPU cache refuses scope only");
    Reset();
    g_reachCinematicLocked = true;
    Check(!Prepare(access), "authored cinematics refuse scope");
    Reset();
    requested = false;
    Check(!Prepare(access), "disabled scope changes nothing");
    {
        // Enclosed seat: the scope pass renders the cockpit screen's forward hull camera.
        Reset();
        g_config.vehicle_cockpit_screen = true;
        auto& v = g_reachOwnerScope.vehicle;
        v = {};
        v.active = v.carrierBasisValid = true;
        v.seatIndex = 0;
        v.identity = ReachVehicleId::Scorpion;
        v.cameraBase[0] = 2;
        v.cameraBase[1] = 3;
        v.cameraBase[2] = 4;
        v.rawCarrierForward[0] = 1;
        Check(Prepare(access) && noted, "an enclosed driver seat prepares the hull camera and asks for the screen");
        const float* c = reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact);
        Check(std::fabs(c[0] - (2 + 4.0f * kReachWorldUnitsPerMeter)) < 1e-3f && std::fabs(c[1] - 3) < 1e-3f &&
                  std::fabs(c[2] - (4 + 1.2f * kReachWorldUnitsPerMeter)) < 1e-3f,
            "the camera sits ahead of and above the seat along the hull");
        // A leftover Visor tier must not hand the cockpit camera the head's forward/up.
        Reset();
        v.active = v.carrierBasisValid = true;
        v.seatIndex = 0;
        v.identity = ReachVehicleId::Scorpion;
        v.cameraBase[0] = 2;
        v.cameraBase[1] = 3;
        v.cameraBase[2] = 4;
        v.rawCarrierForward[0] = 1;
        {
            float* hc = reinterpret_cast<float*>(g_reachOwnerScope.headCenter);
            hc[3] = 0.98f;
            hc[4] = 0.199f;
            hc[5] = 0;
        }
        visorHeadView = true;
        Check(Prepare(access), "cockpit camera prepared under a leftover visor tier");
        {
            const float* cInner = reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact);
            Check(std::fabs(cInner[3] - 1) < 1e-4f && std::fabs(cInner[4]) < 1e-4f,
                "cockpit camera keeps the hull forward, not the head's, under the visor tier");
        }
        // With vehicle monitors on, the Scorpion seat asks for a monitor view by name and the
        // camera takes it (the stub returns the left view: hull forward turned left; 75 deg was refused by the
        // Reach head-hemisphere cull at stage 24, hence kReachSideYawDeg).
        visorHeadView = false;
        monitorsOn = true;
        monitorVehicle = nullptr;
        Reset();
        v.active = v.carrierBasisValid = true;
        v.seatIndex = 0;
        v.identity = ReachVehicleId::Scorpion;
        v.cameraBase[0] = 2;
        v.cameraBase[1] = 3;
        v.cameraBase[2] = 4;
        v.rawCarrierForward[0] = 1;
        {
            const bool prepared = Prepare(access);
            Check(prepared && monitorVehicle && !std::strcmp(monitorVehicle, "scorpion"),
                "monitors: the Reach Scorpion seat names its vehicle");
        }
        {
            const float* cInner = reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact);
            Check(std::fabs(cInner[3] - std::cos(vehicle_monitors::kReachSideYawDeg * 0.01745329252f)) < 1e-3f &&
                      std::fabs(cInner[4] - std::sin(vehicle_monitors::kReachSideYawDeg * 0.01745329252f)) < 1e-3f,
                "monitors: the left view turns the hull camera left by the Reach side yaw");
        }
        monitorsOn = false;
        visorHeadView = false;
        Reset();
        v.identity = ReachVehicleId::Warthog;
        Prepare(access);
        Check(!noted, "an open vehicle never asks for the screen");
        Reset();
        v.identity = ReachVehicleId::Scorpion;
        v.seatIndex = 1;
        Prepare(access);
        Check(!noted, "nor does a passenger seat");
        // Screen off: the Scorpion driver seat never asks for the screen.
        g_config.vehicle_cockpit_screen = false;
        Reset();
        v.identity = ReachVehicleId::Scorpion;
        v.seatIndex = 0;
        v.active = v.carrierBasisValid = true;
        Prepare(access);
        Check(!noted, "screen off: the Scorpion seat does not ask for the screen");
        v = {};
    }
    const float f[]{1, 0, 0}, u[]{0, 0, 1};
    for (int degrees = -70; degrees <= 70; degrees += 5)
    {
        const float radians = degrees * .0174532925f;
        ScopeCameraPose pose{};
        pose.forward[0] = cosf(radians);
        pose.forward[1] = sinf(radians);
        pose.up[2] = 1;
        ScopeProjectionTangents head{1, .7f};
        Check(ExpandScopeCullTangents(f, u, pose, {.1f, .1f}, head), "visible scope fits shared hemisphere");
        Check(head.horizontal >= 1 && head.vertical >= .7f, "scope union never narrows head visibility");
    }
    std::printf("Reach scope: %u checks passed\n", checks);
}
