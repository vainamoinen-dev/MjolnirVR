// vr_format_helpers.h: small pure helpers several VR units call per frame (formats, poses, visor rectangles; no state), in namespace vr_core. Moved from vr.cpp unchanged apart from `inline`.
#pragma once
#include "vr_internal.h"
#include "../../common/config.h"

namespace vr_core
{
    // The window from its four edges, size, distance and anchor.
    inline visor_zoom::EdgeWindow VisorWindow() noexcept
    {
        return visor_zoom::WindowFromEdges({g_config.visor_zoom_left_m, g_config.visor_zoom_right_m,
                                               g_config.visor_zoom_top_m, g_config.visor_zoom_bottom_m},
            g_config.visor_zoom_scale, g_config.visor_zoom_distance_m, g_config.visor_zoom_anchor_x_m,
            g_config.visor_zoom_anchor_y_m);
    }

    // The width of the 4:3 virtual window the zoom render covers (centred on the
    // anchor); every 4:3 zoom/reticle/crop rule runs on it. On a migrated window
    // it is the window's own width, as the single-size window used.
    inline float VisorWindowWidthM() noexcept
    {
        return VisorWindow().virtualWidth;
    }

    // The visible part of the visor image, in swapchain pixels (the quad's imageRect).
    inline XrRect2Di VisorImageRect(const visor_zoom::EdgeWindow& window) noexcept
    {
        const auto at = [](float t, uint32_t size)
        { return static_cast<int32_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * static_cast<float>(size))); };
        const int32_t x0 = at(window.u0, kVisorScreenWidth), y0 = at(window.v0, kVisorScreenHeight);
        const int32_t x1 = std::max(at(window.u1, kVisorScreenWidth), x0 + 1);
        const int32_t y1 = std::max(at(window.v1, kVisorScreenHeight), y0 + 1);
        return {{x0, y0}, {x1 - x0, y1 - y0}};
    }

    inline bool IsExactWaitRecord(const FramePacingRecord& record)
    {
        return record.waitPacketCoherent && record.waitSequence != 0 && record.nextWaitDispatchObservedBeforeBegin &&
               record.nextWaitEventResult == WAIT_OBJECT_0;
    }

    inline bool IsExactWaitPair(const FramePacingRecord& previous, const FramePacingRecord& current)
    {
        return IsExactWaitRecord(previous) && IsExactWaitRecord(current) &&
               current.waitSequence == previous.waitSequence + 1;
    }

    // presented = live (x) correction, in exactly the frame finishAimPose
    // calibrated: the correction is local to the live pose, so the composition
    // needs no re-calibration and never moves the aim position. Returns the
    // input orientation untouched whenever anything cannot be proven valid.
    inline XrQuaternionf ComposeAimContinuityCorrection(
        XrQuaternionf orientation, virtual_stock::Quat4 correction) noexcept
    {
        virtual_stock::Quat4 live{};
        virtual_stock::Quat4 normalizedCorrection{};
        if (!virtual_stock::TryNormalizeQuaternion(
                {orientation.x, orientation.y, orientation.z, orientation.w}, live) ||
            !virtual_stock::TryNormalizeQuaternion(correction, normalizedCorrection))
            return orientation;
        virtual_stock::Quat4 presented{};
        if (!virtual_stock::TryNormalizeQuaternion(
                virtual_stock::MultiplyQuat4(live, normalizedCorrection), presented) ||
            !virtual_stock::Finite(presented))
            return orientation;
        return XrQuaternionf{presented.x, presented.y, presented.z, presented.w};
    }

    inline bool IsSrgb(DXGI_FORMAT f)
    {
        return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }

    // Only used to report how much VRAM the intermediate pool is holding, so
    // the log states a real byte total instead of a guess. The four-byte
    // default covers every format this path has been observed to carry; a
    // wider format is reported low rather than wrongly precise.
    inline uint32_t DxgiBytesPerPixel(DXGI_FORMAT f)
    {
        if (f == DXGI_FORMAT_R32G8X24_TYPELESS || f == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
            f == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS)
            return 8;
        switch (VrBlitFormatFamily(f))
        {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return 8;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        default:
            return 4;
        }
    }

    inline DXGI_FORMAT UnormSibling(DXGI_FORMAT f)
    {
        if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_TYPELESS)
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        if (f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_TYPELESS)
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        return f;
    }

    inline XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
    {
        const XrVector3f u{q.x, q.y, q.z};
        auto cross = [](const XrVector3f& a, const XrVector3f& b)
        { return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
        XrVector3f c1 = cross(u, v);
        c1.x += q.w * v.x;
        c1.y += q.w * v.y;
        c1.z += q.w * v.z;
        const XrVector3f c2 = cross(u, c1);
        return {v.x + 2 * c2.x, v.y + 2 * c2.y, v.z + 2 * c2.z};
    }

    inline bool NormalizeTrackedPose(XrPosef& pose)
    {
        const float ql2 = pose.orientation.x * pose.orientation.x + pose.orientation.y * pose.orientation.y +
                          pose.orientation.z * pose.orientation.z + pose.orientation.w * pose.orientation.w;
        if (!std::isfinite(ql2) || ql2 < 1e-8f || !std::isfinite(pose.position.x) || !std::isfinite(pose.position.y) ||
            !std::isfinite(pose.position.z))
            return false;
        const float inv = 1.0f / sqrtf(ql2);
        pose.orientation.x *= inv;
        pose.orientation.y *= inv;
        pose.orientation.z *= inv;
        pose.orientation.w *= inv;
        return true;
    }

    // One-pole previous-frame blend. `historyWeight` is deliberately expressed
    // as the UI percentage: 0 = raw current pose, .05 = 5% previous + 95% current.
    // Quaternion sign correction keeps equivalent q/-q samples from cancelling.
    inline XrPosef SmoothTrackedPose(const XrPosef& current, const XrPosef& previous, float historyWeight)
    {
        const float h = std::clamp(historyWeight, 0.0f, 0.95f);
        const float n = 1.0f - h;
        float sign = current.orientation.x * previous.orientation.x + current.orientation.y * previous.orientation.y +
                                 current.orientation.z * previous.orientation.z +
                                 current.orientation.w * previous.orientation.w <
                             0.0f
                         ? -1.0f
                         : 1.0f;
        XrPosef result{};
        result.orientation = {previous.orientation.x * h + current.orientation.x * n * sign,
            previous.orientation.y * h + current.orientation.y * n * sign,
            previous.orientation.z * h + current.orientation.z * n * sign,
            previous.orientation.w * h + current.orientation.w * n * sign};
        result.position = {previous.position.x * h + current.position.x * n,
            previous.position.y * h + current.position.y * n, previous.position.z * h + current.position.z * n};
        NormalizeTrackedPose(result);
        return result;
    }

    inline void ApplyTwoHandSmoothingGeometry(
        AimPoseInputs& inputs, const two_hand_input_smoothing::Sample& sample) noexcept
    {
        inputs.twoHandSmoothingGeometryValid = true;
        inputs.twoHandSmoothedPrimaryOrientation = {sample.primaryOrientation.x, sample.primaryOrientation.y,
            sample.primaryOrientation.z, sample.primaryOrientation.w};
        const auto toXr = [](virtual_stock::Point3 p) { return XrVector3f{p.x, p.y, p.z}; };
        inputs.twoHandSmoothedPrimaryAimPosition = toXr(sample.primaryAimPosition);
        inputs.twoHandSmoothedSupportAimPosition = toXr(sample.supportAimPosition);
        inputs.twoHandSmoothedPrimaryGripValid = sample.primaryGripValid;
        inputs.twoHandSmoothedPrimaryGripPosition = toXr(sample.primaryGripPosition);
        inputs.twoHandSmoothedSupportGripValid = sample.supportGripValid;
        inputs.twoHandSmoothedSupportGripPosition = toXr(sample.supportGripPosition);
    }

    inline bool TelemetryFinite3(const float value[3]) noexcept
    {
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    }
} // namespace vr_core
