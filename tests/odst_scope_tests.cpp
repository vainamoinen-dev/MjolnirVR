// Tests the ODST scope pass (odst_scope.inl, compiled in directly with scope_logic.cpp): one private
// native render per frame, fault restore of every camera block, handedness routing, and the cockpit
// screen and vehicle monitor views. Built as halomccvr_odst_scope_tests.
// No game process or GPU work: the render calls are stubs.
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../src/common/scope_logic.h"
#include "../src/common/titles/odst/odst_bringup_logic.h"
#include "../src/common/runtime_types.h"
#include "../src/common/titles/odst/odst_vehicle_logic.h"
#include "../src/common/cockpit_screen_logic.h"
#include "../src/dll/core/guard_count.h"
static unsigned checks{};
static void Check(bool ok, const char* reason)
{
    ++checks;
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", reason);
        std::exit(1);
    }
}
// ODST's independently documented layout; deliberately not H3's 0x90 derived block.
static struct
{
    struct
    {
        uintptr_t compactSize = 0x90, derivedSize = 0xC0, rootCurrentCompact = 8, rootCurrentDerived = 0x98,
                  rootSecondaryCompact = 0x158, rootSecondaryDerived = 0x1E8, nestedCurrentCompact = 0x6D0,
                  nestedCurrentDerived = 0x760, nestedSecondaryCompact = 0x820, nestedSecondaryDerived = 0x8B0,
                  compactPosition = 0, compactForward = 0xC, compactUp = 0x18, verticalFov = 0x28, referenceFov = 0x2C,
                  projectionMatrix = 0x78;
    } layout;
} kOdstCameraProfile;
static std::atomic<uint32_t> g_odstRuntimeGeneration{7};
static std::atomic<float> g_aimFwdX{0}, g_aimFwdY{1}, g_aimFwdZ{0};
static std::atomic<bool> g_scopeRenderActive{false};
struct FpInterpolationContext
{
    uint64_t tag = 12, serial = 23;
};
static FpInterpolationContext g_fpInterpolationContexts[2];
static struct
{
    std::atomic<bool> armed{true}, teardownRequested{false};
    void (*buildViewport)(void*, void*) = nullptr;
    void (*buildMatrices)(void*, void*, void*, float) = nullptr;
    void (*prepareView)(void*, int) = nullptr;
    void (*originalRenderView)(void*) = nullptr;
    void (*fpCameraUpload)(void*, void*) = nullptr;
} g_odstCamera;
static bool cinematic = false, controllerValid = true, requested = true, targetReady = true, raster = false;
static float zoom = 6, fixtureAspect = 4.f / 3.f;
static unsigned fixtureStep = 0, faultStep = 0, draws = 0, copies = 0, ends = 0, logs = 0;
static alignas(16) unsigned char fixtureView[0xB00], fixtureSaved[sizeof(fixtureView)];
static CinematicControlState ReadOdstCinematicControl(int32_t&, int32_t&)
{
    return cinematic ? CinematicControlState::AuthoredLocked : CinematicControlState::PlayerControlled;
}
static bool VR_ScopeShouldRenderThisFrame()
{
    return requested;
}
static bool ControllerWorldPoseEx(bool left, float* basis, float*, float&)
{
    Check(!left, "primary role follows current handedness routing");
    basis[0] = basis[4] = basis[8] = 1;
    return controllerValid;
}
static bool VR_GetScopeRenderAspect(float& out)
{
    out = fixtureAspect;
    return targetReady;
}
static float VR_GetScopeZoom()
{
    return zoom;
}
static bool VR_VisorZoomHeadView()
{
    return false;
}
struct OdstSeatSnapshot
{
    float rawFwd[3];
    float anchor[3];
    float anchorBase[3];
    bool anchorValid;
    bool anchorBaseValid;
    int seatIndex;
    int parentHandle;
    OdstVehicleId identity;
    bool mounted;
};
static bool seatAvailable = false;
static OdstSeatSnapshot fakeSeat{};
static bool OdstReadSeatSnapshot(OdstSeatSnapshot& out)
{
    out = fakeSeat;
    return seatAvailable;
}
struct CockpitTestConfig
{
    bool vehicle_cockpit_screen = false;
    float cockpit_screen_ahead_m = 0, cockpit_screen_fov_deg = 90;
} g_config;
static bool cockpitNoted = false;
static void VR_NoteCockpitScreen(bool wanted)
{
    cockpitNoted = wanted;
}
static bool VR_BeginScopeRaster()
{
    raster = targetReady;
    return raster;
}
static void Phase()
{
    ++fixtureStep;
    if (fixtureStep == faultStep)
        RaiseException(0xE0431111, 0, 0, nullptr);
}
static void VR_CaptureScope()
{
    Phase();
    Check(raster && g_scopeRenderActive, "scope capture owns private raster");
    ++copies;
}
static void VR_EndScopeRaster()
{
    Check(raster, "raster ends once");
    raster = false;
    ++ends;
}
static void Viewport(void*, void*)
{
    Phase();
}
static void Matrices(void*, void*, void* derived, float)
{
    std::memset(derived, 0xBD, 0xC0);
    Phase();
}
static void Upload(void*, void*)
{
    Phase();
}
static void Prepare(void*, int)
{
    Phase();
}
static void Draw(void* input)
{
    ++draws;
    Check(input == fixtureView && raster && g_scopeRenderActive, "one private ODST native render");
    const auto& l = kOdstCameraProfile.layout;
    const auto* camera = reinterpret_cast<const float*>(fixtureView + l.rootCurrentCompact);
    if (cockpitNoted)
        Check(std::abs(camera[0] - 4.0f * cockpit_screen::kWorldUnitsPerMeter) < 1e-3f && camera[3] == 1 &&
                  camera[4] == 0,
            "the cockpit camera sits ahead of the seat along the hull forward");
    else
    {
        Check(camera[0] == 2 && camera[1] == 3 && camera[2] == 4 && camera[3] == 0 && camera[4] == 1 && camera[5] == 0,
            "scope stays at native gameplay origin with independent primary direction");
        const auto lens = ComputeScopeProjectionTangents(zoom, fixtureAspect);
        const auto* matrix = reinterpret_cast<const float*>(fixtureView + l.rootCurrentDerived + l.projectionMatrix);
        Check(std::abs(matrix[0] - 1.f / lens.horizontal) < .001f && std::abs(matrix[5] - 1.f / lens.vertical) < .001f,
            "scope projection preserves requested optical zoom and source aspect");
    }
    const uintptr_t compact[]{l.rootSecondaryCompact, l.nestedCurrentCompact, l.nestedSecondaryCompact};
    const uintptr_t derived[]{l.rootSecondaryDerived, l.nestedCurrentDerived, l.nestedSecondaryDerived};
    for (int i = 0; i < 3; ++i)
    {
        Check(
            std::memcmp(fixtureView + compact[i], camera, l.compactSize) == 0, "every nested compact owns scope pose");
        Check(std::memcmp(fixtureView + derived[i], fixtureView + l.rootCurrentDerived, l.derivedSize) == 0,
            "every nested derived owns scope projection");
    }
    g_fpInterpolationContexts[0] = {99, 100};
    g_fpInterpolationContexts[1] = {101, 102};
    // Native preparation may mutate any camera block. All eight must restore.
    for (auto offset : compact)
        std::memset(fixtureView + offset, 0xAC, l.compactSize);
    for (auto offset : derived)
        std::memset(fixtureView + offset, 0xDA, l.derivedSize);
    Phase();
}
#define LOG(...) (++logs)
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
static void OdstScopeLogCamera(const float*, const float*) {}
static const float* OdstScopeOrigin(const float* head, const float*, float*)
{
    return head;
}
#include "../src/dll/titles/odst/odst_scope.inl"
#undef LOG
static void Reset()
{
    for (size_t i = 0; i < sizeof(fixtureView); ++i)
        fixtureView[i] = uint8_t(i * 13 + 7);
    const float origin[]{2, 3, 4};
    std::memcpy(fixtureView + 8, origin, 12);
    std::memcpy(fixtureSaved, fixtureView, sizeof(fixtureView));
    g_odstRuntimeGeneration = 7;
    g_odstScopeFaultGeneration = 0;
    g_scopeRenderActive = false;
    g_odstCamera.armed = true;
    g_odstCamera.teardownRequested = false;
    g_odstCamera.buildViewport = Viewport;
    g_odstCamera.buildMatrices = Matrices;
    g_odstCamera.fpCameraUpload = Upload;
    g_odstCamera.prepareView = Prepare;
    g_odstCamera.originalRenderView = Draw;
    cinematic = false;
    controllerValid = requested = targetReady = true;
    raster = false;
    fixtureStep = faultStep = draws = copies = ends = 0;
    g_fpInterpolationContexts[0] = {12, 23};
    g_fpInterpolationContexts[1] = {34, 45};
}
static void Restored()
{
    Check(std::memcmp(fixtureView, fixtureSaved, sizeof(fixtureView)) == 0,
        "all camera bytes and surrounding canaries restored");
    Check(!raster && !g_scopeRenderActive, "optional scope ownership released");
    Check(g_fpInterpolationContexts[0].tag == 12 && g_fpInterpolationContexts[1].serial == 45,
        "primary/secondary palette receipts restored");
    Check(g_odstCamera.armed && !g_odstCamera.teardownRequested, "scope never disarms stereo camera");
}
int main()
{
    for (float z : {6.f, 12.f, 24.f})
        for (float a : {1.f, 4.f / 3.f, 16.f / 9.f})
        {
            Reset();
            zoom = z;
            fixtureAspect = a;
            RenderOdstScope(fixtureView);
            Check(draws == 1 && copies == 1 && ends == 1, "one completed zoom view");
            Restored();
        }
    for (unsigned fault = 1; fault <= 6; ++fault)
    {
        Reset();
        faultStep = fault;
        RenderOdstScope(fixtureView);
        Restored();
        Check(g_odstScopeFaultGeneration == 7 && ends == 1 && copies == 0,
            "each native failure isolates optional generation");
        const auto oldDraws = draws;
        RenderOdstScope(fixtureView);
        Check(draws == oldDraws, "failed generation never retries native drawing");
    }
    for (int refusal = 0; refusal < 7; ++refusal)
    {
        Reset();
        switch (refusal)
        {
        case 0:
            cinematic = true;
            break;
        case 1:
            controllerValid = false;
            break;
        case 2:
            requested = false;
            break;
        case 3:
            targetReady = false;
            break;
        case 4:
            g_odstCamera.buildMatrices = nullptr;
            break;
        case 5:
            g_odstScopeFaultGeneration = 7;
            break;
        case 6:
            g_odstRuntimeGeneration = 0;
            break;
        }
        RenderOdstScope(fixtureView);
        Check(draws == 0 && ends == 0, "unproven scope never opens native transaction");
        Restored();
    }
    {
        // Enclosed seat: the scope pass is the cockpit screen's hull camera; open vehicles and passengers are not.
        const auto seat = [](OdstVehicleId id, int index, bool mounted)
        {
            fakeSeat = {};
            fakeSeat.anchorValid = true;
            fakeSeat.anchor[2] = 1;
            fakeSeat.rawFwd[0] = 1;
            fakeSeat.identity = id;
            fakeSeat.seatIndex = index;
            fakeSeat.mounted = mounted;
            seatAvailable = true;
        };
        g_config.vehicle_cockpit_screen = true;
        Reset();
        seat(OdstVehicleId::Scorpion, 0, false);
        RenderOdstScope(fixtureView);
        Check(cockpitNoted && draws == 1 && copies == 1, "an enclosed ODST driver seat renders the cockpit camera");
        Restored();
        Reset();
        seat(OdstVehicleId::Warthog, 0, false);
        RenderOdstScope(fixtureView);
        Check(!cockpitNoted, "an open vehicle never asks for the screen");
        Reset();
        seat(OdstVehicleId::Scorpion, 1, false);
        RenderOdstScope(fixtureView);
        Check(!cockpitNoted, "nor does a passenger seat");
        Reset();
        seat(OdstVehicleId::Wraith, 0, true);
        RenderOdstScope(fixtureView);
        Check(!cockpitNoted, "nor a mounted turret");
        Reset();
        seat(OdstVehicleId::Banshee, 0, false);
        cinematic = true;
        RenderOdstScope(fixtureView);
        Check(!cockpitNoted && draws == 0, "cinematics refuse it");
        g_config.vehicle_cockpit_screen = false;
        seatAvailable = false;
        Reset();
        seat(OdstVehicleId::Scorpion, 0, false);
        RenderOdstScope(fixtureView);
        Check(!cockpitNoted, "switch off: no screen");
    }
    ReportOdstScope();
    Check(logs == 1, "fault details emitted only by worker report");
    std::printf("PASS: %u production ODST scope checks (native renderer stubbed)\n", checks);
}
