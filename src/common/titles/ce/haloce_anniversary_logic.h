// Halo CE Anniversary-graphics (Saber) camera logic: the native camera layout and how a tracked eye
// or a monitor/scope view is staged into it.
// Used by the CE DLSS, flare and view-pair logic, src/dll/titles/ce/haloce_native_bindings.h and
// tests/haloce_render_tests.cpp. Pure logic: no Windows calls, no globals, no logging.
#pragma once

#include "haloce_render_logic.h"
#include <cstring>

namespace halo_ce
{
    // +0x2EA720 copies 7*0x80+0x18 bytes from CE's Saber
    // camera, then stores that camera at view-list +0x40 + index*0x3C8.
    // The active render list is renderer+0xB0, hence camera 0 at +0xF0.
    // Unknown/derived fields remain opaque and must be restored as a whole.
    struct SaberCamera
    {
        SaberPose pose;
        float view[16]{};
        float nearPlane{}, farPlane{};
        uint8_t derived88[0xb4]{};
        float viewportX{}, viewportY{}, viewportWidth{}, viewportHeight{};
        float horizontalFovDegrees{}, verticalFovDegrees{}, aspectRatio{};
        float rasterScaleX{}, rasterScaleY{};
        uint8_t derived160[0x238]{};
    };
    static_assert(sizeof(SaberCamera) == 0x398);
    static_assert(offsetof(SaberCamera, nearPlane) == 0x80);
    static_assert(offsetof(SaberCamera, viewportX) == 0x13c);
    static_assert(offsetof(SaberCamera, horizontalFovDegrees) == 0x14c);
    static_assert(offsetof(SaberCamera, aspectRatio) == 0x154);
    static_assert(offsetof(SaberCamera, derived160) == 0x160);

    // Finite output guards for fields written by the native view,
    // projection and culling rebuilds. Do not scan the opaque resource tail as
    // floats: it also contains pointers and native flags.
    inline bool FiniteSaberDerivedCamera(const SaberCamera& camera)
    {
        const auto finiteRange = [&](size_t offset, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                float value{};
                std::memcpy(&value, reinterpret_cast<const uint8_t*>(&camera) + offset + 4 * i, 4);
                if (!std::isfinite(value))
                    return false;
            }
            return true;
        };
        return finiteRange(0x40, 16) &&  // native view matrix
               finiteRange(0x8c, 8) &&   // four projection-bound corners
               finiteRange(0x12c, 4) &&  // projection origin/width/height
               finiteRange(0x158, 2) &&  // raster scale
               finiteRange(0x318, 24) && // eight world-space culling corners
               finiteRange(0x378, 6);    // world-space bounds
    }

    inline Vec3 FromSaber(Vec3 v)
    {
        return {v.x, -v.z, v.y};
    }

    inline bool NativeCameraFromSaber(const SaberCamera& saber, Camera& out)
    {
        const float* matrix = saber.pose.matrix;
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(matrix[i]))
                return false;
        if (std::fabs(matrix[3]) > 0.0001f || std::fabs(matrix[7]) > 0.0001f || std::fabs(matrix[11]) > 0.0001f ||
            std::fabs(matrix[15] - 1) > 0.0001f || !std::isfinite(saber.viewportX) || !std::isfinite(saber.viewportY) ||
            !std::isfinite(saber.viewportWidth) || !std::isfinite(saber.viewportHeight) || saber.viewportX != 0 ||
            saber.viewportY != 0 || saber.viewportWidth < 1 || saber.viewportWidth > 16384 ||
            saber.viewportHeight < 1 || saber.viewportHeight > 16384 ||
            std::floor(saber.viewportWidth) != saber.viewportWidth ||
            std::floor(saber.viewportHeight) != saber.viewportHeight)
            return false;
        const Vec3 right{matrix[0], matrix[1], matrix[2]};
        const Vec3 up{matrix[4], matrix[5], matrix[6]};
        const Vec3 forward{matrix[8], matrix[9], matrix[10]};
        const Vec3 rightError = right - Cross(forward, up);
        if (Dot(rightError, rightError) > 0.0001f)
            return false;
        Camera candidate{};
        candidate.position = FromSaber({matrix[12], matrix[13], matrix[14]}) * (1.0f / kSaberUnitsPerNativeUnit);
        candidate.forward = FromSaber(forward);
        candidate.up = FromSaber(up);
        candidate.verticalFov = saber.verticalFovDegrees * 0.017453292519943295f;
        candidate.viewport = candidate.window = {
            0, 0, static_cast<int16_t>(saber.viewportHeight), static_cast<int16_t>(saber.viewportWidth)};
        candidate.nearPlane = saber.nearPlane / kSaberUnitsPerNativeUnit;
        candidate.farPlane = saber.farPlane / kSaberUnitsPerNativeUnit;
        if (!Valid(candidate))
            return false;
        out = candidate;
        return true;
    }

    // A monitor-frame camera (Tracking::monitorFrame) in place of an eye:
    // the hull camera (1), the head view (eye 0's pose) narrowed to the zoom (2),
    // or the gun camera on the lens glass (3, BuildScopeCamera: lens receipt,
    // else the aim frame at the kit glass).
    // Symmetric FOV: vertical tangent scopeTanHalfVertical, horizontal by the
    // raster's aspect. Same staging contract as StageSaberEye.
    inline bool StageSaberMonitor(const SaberCamera& stock, const Tracking& tracking, const Reference& reference,
        float unitsPerMeter, bool positional, SaberCamera& out, Cover& cover)
    {
        const float t = tracking.scopeTanHalfVertical;
        Camera native{};
        if (!std::isfinite(t) || t <= 0.001f || t >= 1.0f || !NativeCameraFromSaber(stock, native) ||
            !std::isfinite(stock.viewportWidth) || !std::isfinite(stock.viewportHeight) || stock.viewportHeight < 1)
            return false;
        const float aspect = stock.viewportWidth / stock.viewportHeight;
        Cover candidateCover{2 * std::atan(t), std::atan(t * aspect), std::atan(t)};
        if (!std::isfinite(candidateCover.halfX) || candidateCover.halfX >= 1.55f)
            return false;
        Camera posed = native;
        if (tracking.monitorFrame == 1)
        {
            if (!Finite(tracking.scopeHullPosition) || !Finite(tracking.scopeHullForward) ||
                !Finite(tracking.scopeHullUp))
                return false;
            posed.position = tracking.scopeHullPosition;
            posed.forward = tracking.scopeHullForward;
            posed.up = tracking.scopeHullUp;
        }
        else if (tracking.monitorFrame == 2)
        {
            Cover eyeCover{};
            if (!BuildCover(tracking, native.viewport, eyeCover) ||
                !BuildEye(native, tracking, reference, 0, unitsPerMeter, positional, eyeCover, posed))
                return false;
        }
        else if (tracking.monitorFrame == 3)
        {
            // The same gun camera as the Classic third pass, requested by the frame kind.
            Tracking gun = tracking;
            gun.scopeWanted = true;
            gun.scopeHeadView = false;
            gun.scopeHullCamera = false;
            if (!BuildScopeCamera(native, gun, reference, unitsPerMeter, positional, posed))
                return false;
        }
        else
            return false;
        posed.verticalFov = candidateCover.verticalFov;
        SaberPose candidatePose{};
        if (!Valid(posed) || !BuildSaberPose(posed, {}, 0, candidatePose))
            return false;
        SaberCamera candidate = stock;
        candidate.pose = candidatePose;
        constexpr float radiansToDegrees = 57.29577951308232f;
        candidate.horizontalFovDegrees = 2 * candidateCover.halfX * radiansToDegrees;
        candidate.verticalFovDegrees = 2 * candidateCover.halfY * radiansToDegrees;
        candidate.aspectRatio = stock.viewportHeight / stock.viewportWidth;
        out = candidate;
        cover = candidateCover;
        return true;
    }

    // Stages owned pose/FOV fields only. The caller must run CE's own view,
    // frustum and culling rebuilds before rendering, then restore the saved camera.
    // This function alone never produces an engine-ready derived camera.
    inline bool StageSaberEye(const SaberCamera& stock, const Tracking& tracking, const Reference& reference, int eye,
        float unitsPerMeter, bool positional, SaberCamera& out, Cover& cover)
    {
        Camera native{}, posed{};
        Cover candidateCover{};
        SaberPose candidatePose{};
        if (!NativeCameraFromSaber(stock, native) || !BuildCover(tracking, native.viewport, candidateCover) ||
            !BuildEye(native, tracking, reference, eye, unitsPerMeter, positional, candidateCover, posed) ||
            !BuildSaberPose(posed, {}, 0, candidatePose))
            return false;
        SaberCamera candidate = stock;
        candidate.pose = candidatePose;
        constexpr float radiansToDegrees = 57.29577951308232f;
        candidate.horizontalFovDegrees = 2 * candidateCover.halfX * radiansToDegrees;
        candidate.verticalFovDegrees = 2 * candidateCover.halfY * radiansToDegrees;
        candidate.aspectRatio = stock.viewportHeight / stock.viewportWidth;
        out = candidate;
        cover = candidateCover;
        return true;
    }
}
