// Universal VR scope logic: the R3 toggle, runtime magnification, scope camera placement and FOV maths.
// Used by src/dll/core (game.cpp, vr.cpp), the CE and Halo 2 title adapters and scope_action_input.h;
// tested by tests/core_tests.cpp and the per-title scope tests. Implemented partly in scope_logic.cpp.
// Pure logic: no globals, no D3D or OpenXR calls (render-target checks take caller callbacks).
#pragma once
#include <algorithm>
#include <cmath>

struct ScopeToggleUpdate
{
    bool changed = false;
    bool active = false;
};

// R3 toggles the universal VR scope on release. A menu chord can begin with
// either stick, so the whole R3 gesture is cancelled as soon as the chord is
// consumed. Disabled/unavailable scope input resets the latch without letting
// a held R3 toggle when gameplay resumes.
class ScopeToggleDetector
{
public:
    ScopeToggleUpdate Update(bool enabled, bool rightClick, bool cancelGesture);
    void Reset();

private:
    bool m_active = false;
    bool m_rightDown = false;
    bool m_cancelled = false;
};

class ScopeRefreshScheduler
{
public:
    bool Advance(bool active, int divisor);

private:
    unsigned m_frame = 0;
};

// Runtime magnification for the universal scope. Opening the scope always
// restores the configured default; while it remains open, right-stick Y trims
// the lens continuously. Keeping this state outside Config avoids making a
// temporary in-game adjustment permanent.
class ScopeZoomController
{
public:
    float Update(bool active, float stickY, float deltaSeconds, float defaultZoom);
    float Zoom() const { return m_zoom; }

private:
    float m_zoom = 1.25f;
    bool m_wasActive = false;
};

// Resolves one R3 stream into Halo-authored zoom behavior for scoped weapons
// and a delayed fallback toggle for weapons that do not react to native zoom.
class ScopeZoomResolver
{
public:
    void RequestToggle();
    bool Update(bool enabled, bool nativeZoomed);
    void Reset();

private:
    bool m_fallbackActive = false;
    bool m_nativeEngaged = false;
    unsigned m_pendingFrames = 0;
    unsigned m_ignoreRequestFrames = 0;
};

struct ScopeQuadTransform
{
    float position[3]{};
    float width = 0.0f;
    float height = 0.0f;
};

// Applies direct controller-local right/up/forward offsets. The dimensions are
// fixed physical meters and intentionally do not depend on headset distance.
ScopeQuadTransform ComputeScopeQuadTransform(const float orientation[4], const float origin[3], float rightMeters,
    float upMeters, float forwardMeters, float widthMeters);

struct ScopeCameraPose
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
};

// Builds a bullet-aligned scope camera from Halo's collision-safe gameplay
// camera origin. Its roll follows the controller, while the physical scope quad
// remains positioned independently on the gun.
// the earlier CE VR mod's placement: the scope camera at the gun's muzzle plus a
// forward offset along the barrel (world units), so the rifle model is
// always behind it. False (out untouched) for a non-finite input.
bool ScopeOriginAtMuzzle(const float muzzle[3], const float barrelForward[3], float forwardWorld, float out[3]);
// Capped variant: the head camera's vertical FOV (radians) after the scope
// pass widens it for shared culling is limited to the original plus maxExtra
// (never below the original, never above the needed value). NaN -> needed.
float CapCullVerticalFov(float originalRadians, float neededRadians, float maxExtraRadians);
// Diagnostic (Reach "lower quality while zoomed"): where the scope pass runs in
// the frame transaction. 0 = before the eyes (as shipped), 1 = after the eyes,
// so per-frame engine state set by the first player-view render (streaming,
// shadows, detail) comes from the head, not the zoomed camera.
enum class ReachScopeOrder : int
{
    First = 0,
    AfterEyes = 1
};
inline ReachScopeOrder ReachScopeOrderFrom(int key)
{
    return key == 1 ? ReachScopeOrder::AfterEyes : ReachScopeOrder::First;
}
inline const char* ReachScopeOrderName(ReachScopeOrder order)
{
    return order == ReachScopeOrder::AfterEyes ? "after eyes" : "first";
}
// Zoom spec: the camera sits at the lens, so the lens-node path's lens-to-front
// travel is limited to maxTravel (same units). Bad/negative input -> 0.
float CapLensTravel(float travel, float maxTravel);
// Level-horizon option for every title's zoom camera (set once per frame).
void SetScopeLevelHorizon(bool level);
bool ComputeScopeCameraPose(
    const float controllerBasis[9], const float cameraOrigin[3], const float bulletForward[3], ScopeCameraPose& result);

struct ScopeProjectionTangents
{
    float horizontal = 0.0f;
    float vertical = 0.0f;
};

// A conventional 70-degree horizontal lens at 1x. The final image is square;
// horizontal is widened only enough to compensate for the source center crop.
ScopeProjectionTangents ComputeScopeProjectionTangents(float zoom, float sourceAspect);

// Conservative angular union at a shared origin. Refuse rays behind the head
// hemisphere rather than asking a perspective camera to represent >180 degrees.
// Existing head tangents can only grow; output is unchanged on refusal.
bool ExpandScopeCullTangents(const float headForward[3], const float headUp[3], const ScopeCameraPose& scope,
    const ScopeProjectionTangents& lens, ScopeProjectionTangents& head);

// Whether any bound render target is a view of `target`. Halo 2
// Anniversary writes its finished eye through its own RTV over the backbuffer
// texture, never the backbuffer slot's RTV, so the scope pass must match the
// resource, not the view pointer. resourceOf(view) returns the view's resource
// (the caller releases any reference it takes).
template <typename View, typename ResourceOf>
bool AnyBoundViewOf(const void* target, unsigned count, View* const* views, ResourceOf resourceOf)
{
    if (!target || !views)
        return false;
    for (unsigned i = 0; i < count; ++i)
        if (views[i] && resourceOf(views[i]) == target)
            return true;
    return false;
}

// The scope camera looks along the barrel the shot leaves from (the fresh
// muzzle receipt's direction) and falls back to the engine aim. Writes the
// unit forward; returns true when the barrel was used.
inline bool SelectScopeForward(const float barrel[3], bool barrelFresh, const float aim[3], float out[3])
{
    const auto unit = [&](const float* v)
    {
        if (!v)
            return false;
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!std::isfinite(l) || l < 1e-4f)
            return false;
        for (int i = 0; i < 3; ++i)
            out[i] = v[i] / l;
        return true;
    };
    if (barrelFresh && unit(barrel))
        return true;
    if (!unit(aim))
    {
        out[0] = 1;
        out[1] = out[2] = 0;
    }
    return false;
}

// Signed yaw (about world up, z) and pitch differences b - a in degrees, plus
// the total angle. Inputs need not be unit length.
inline void DirectionDeltaDegrees(const float a[3], const float b[3], float& yaw, float& pitch, float& total)
{
    constexpr float kDeg = 57.2957795f;
    const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    yaw = pitch = total = 0;
    if (!(la > 1e-5f) || !(lb > 1e-5f))
        return;
    yaw = std::remainder((std::atan2(b[1], b[0]) - std::atan2(a[1], a[0])) * kDeg, 360.0f);
    pitch = (std::asin(std::clamp(b[2] / lb, -1.0f, 1.0f)) - std::asin(std::clamp(a[2] / la, -1.0f, 1.0f))) * kDeg;
    const float c = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb);
    total = std::acos(std::clamp(c, -1.0f, 1.0f)) * kDeg;
}
