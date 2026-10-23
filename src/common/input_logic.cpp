// Out-of-line parts of input_logic.h: head D-pad zone and directions, menu chords, menu-quad
// hits, haptic mixing and pause-input rules.
// Pure logic: no Windows, OpenXR or XInput calls, no globals.
#include "input_logic.h"

#include <cmath>
#include <algorithm>

bool DpadHeadWithinRadius(
    const float head[3], const float controller[3], float radiusMetres, float marginMetres) noexcept
{
    if (!head || !controller)
        return false;
    const float radius = (std::isfinite(radiusMetres) ? std::clamp(radiusMetres, 0.10f, 0.50f) : 0.30f) +
                         (std::isfinite(marginMetres) ? std::clamp(marginMetres, 0.0f, 0.10f) : 0.0f);
    const float dx = head[0] - controller[0], dy = head[1] - controller[1], dz = head[2] - controller[2];
    return dx * dx + dy * dy + dz * dz < radius * radius;
}

DpadStickInput ConsumeThumbrestDpad(bool enabled, bool touched, bool inputReady, float& rightX, float& rightY) noexcept
{
    if (!enabled || !touched || !inputReady)
        return {};
    DpadStickInput result{true, std::isfinite(rightX) ? std::clamp(rightX, -1.0f, 1.0f) : 0.0f,
        std::isfinite(rightY) ? std::clamp(rightY, -1.0f, 1.0f) : 0.0f};
    rightX = rightY = 0.0f;
    return result;
}

uint16_t DpadDirectionButtons(float x, float y) noexcept
{
    // XInput's UP/DOWN/LEFT/RIGHT bits, with the existing gesture threshold.
    if (!std::isfinite(x))
        x = 0.0f;
    if (!std::isfinite(y))
        y = 0.0f;
    uint16_t result = 0;
    if (y > 0.5f)
        result |= 0x0001;
    if (y < -0.5f)
        result |= 0x0002;
    if (x < -0.5f)
        result |= 0x0004;
    if (x > 0.5f)
        result |= 0x0008;
    return result;
}

void MenuChordDetector::Reset()
{
    m_firstDownMs = 0;
    m_firstSide = 0;
    m_latched = false;
    m_expired = false;
    m_pulseUntilMs = 0;
    m_pulseSide = 0;
}

MenuChordResult MenuChordDetector::Update(uint64_t nowMs, bool leftClick, bool rightClick)
{
    if (m_latched)
    {
        if (!leftClick && !rightClick)
            Reset();
        else
            return {false, true, 3, 0};
        return {};
    }

    const uint8_t pulse = m_pulseSide && nowMs < m_pulseUntilMs ? m_pulseSide : 0;
    if (!leftClick && !rightClick)
    {
        // A lone tap released while still held back: deliver it now, briefly.
        if ((m_firstSide == 1 || m_firstSide == 2) && !m_expired && nowMs - m_firstDownMs <= kChordHoldMs)
        {
            m_pulseSide = m_firstSide;
            m_pulseUntilMs = nowMs + kTapPulseMs;
            m_firstDownMs = 0;
            m_firstSide = 0;
            m_expired = false;
            return {false, false, 0, m_pulseSide};
        }
        m_firstDownMs = 0;
        m_firstSide = 0;
        m_expired = false;
        return {false, false, 0, pulse};
    }

    if (m_firstSide == 0)
    {
        m_firstDownMs = nowMs;
        m_firstSide = leftClick && rightClick ? 3 : (leftClick ? 1 : 2);
    }

    if (!m_expired && nowMs - m_firstDownMs > kChordWindowMs)
        m_expired = true;
    if (!m_expired && leftClick && rightClick)
    {
        m_latched = true;
        m_pulseSide = 0;
        m_pulseUntilMs = 0;
        return {true, true, 3, 0};
    }
    const uint8_t held = (leftClick ? 1 : 0) | (rightClick ? 2 : 0);
    const uint8_t withhold = !m_expired && nowMs - m_firstDownMs <= kChordHoldMs ? held : 0;
    return {false, false, withhold, pulse};
}

bool DpadGesture::Update(bool insideEnter, bool insideStay, bool clickHeld, float stickX, float stickY) noexcept
{
    m_zone = m_zone ? insideStay : insideEnter;
    if (!std::isfinite(stickX))
        stickX = 0.0f;
    if (!std::isfinite(stickY))
        stickY = 0.0f;
    const bool stickHeld = stickX * stickX + stickY * stickY > kStickHeld * kStickHeld;
    if (m_zone != m_mode && !clickHeld && !stickHeld)
    {
        m_mode = m_zone;
        m_directions = 0;
    }
    return m_mode;
}

uint16_t DpadGesture::Directions(float x, float y) noexcept
{
    if (!std::isfinite(x))
        x = 0.0f;
    if (!std::isfinite(y))
        y = 0.0f;
    const auto axis = [&](uint16_t bit, float value)
    {
        const bool was = (m_directions & bit) != 0;
        if (was ? value > kRelease : value > kPress)
            m_directions |= bit;
        else
            m_directions &= static_cast<uint16_t>(~bit);
    };
    axis(0x0001, y);
    axis(0x0002, -y);
    axis(0x0004, -x);
    axis(0x0008, x);
    return m_directions;
}

MenuPointerHit IntersectMenuQuad(const float origin[3], const float direction[3], float distance, float width,
    float height, float centerY, float centerX)
{
    MenuPointerHit result{};
    if (!origin || !direction || distance <= 0.0f || width <= 0.0f || height <= 0.0f)
        return result;
    if (std::fabs(direction[2]) < 1e-5f)
        return result;

    const float t = (-distance - origin[2]) / direction[2];
    if (t <= 0.0f)
        return result;
    const float x = origin[0] + direction[0] * t;
    const float y = origin[1] + direction[1] * t;
    const float halfWidth = width * 0.5f;
    const float halfHeight = height * 0.5f;
    if (x < centerX - halfWidth || x > centerX + halfWidth || y < centerY - halfHeight || y > centerY + halfHeight)
        return result;

    result.hit = true;
    result.u = (x - centerX) / width + 0.5f;
    result.v = 0.5f - (y - centerY) / height;
    return result;
}

float BlendXInputMotors(uint16_t lowFrequencyMotor, uint16_t highFrequencyMotor)
{
    constexpr float inverseMax = 1.0f / 65535.0f;
    const float low = static_cast<float>(lowFrequencyMotor) * inverseMax;
    const float high = static_cast<float>(highFrequencyMotor) * inverseMax;
    const float blended = low * 0.65f + high * 0.35f;
    return blended > 1.0f ? 1.0f : blended;
}

HapticPeakSample SampleHapticPeak(float peak, float latest)
{
    const float p = peak < 0.0f ? 0.0f : (peak > 1.0f ? 1.0f : peak);
    const float l = latest < 0.0f ? 0.0f : (latest > 1.0f ? 1.0f : latest);
    HapticPeakSample sample;
    sample.apply = p > l ? p : l;
    sample.carry = l;
    return sample;
}

float MergeHapticAmplitude(float game, float contact)
{
    const float g = game < 0.0f ? 0.0f : (game > 1.0f ? 1.0f : game);
    const float c = contact < 0.0f ? 0.0f : (contact > 1.0f ? 1.0f : contact);
    return g > c ? g : c;
}

uint32_t NormalizeVirtualXInputSetStateResult(uint32_t originalResult, uint32_t userIndex, bool hasVibrationRequest)
{
    return userIndex == 0 && hasVibrationRequest ? 0u : originalResult;
}

bool PausePresentationInputAllowed(bool sharedGameplayOwner)
{
    return sharedGameplayOwner;
}

bool PauseToggleInputAllowed(bool sharedGameplayOwner, bool titleSpecificPauseOwner)
{
    return sharedGameplayOwner || titleSpecificPauseOwner;
}

bool UpdateTwoHandHold(bool wasEngaged, bool gripHeld, bool inGrabZone)
{
    return gripHeld && (wasEngaged || inGrabZone);
}

void PauseLevelRecovery::Reset()
{
    m_sawLoading = false;
}

bool PauseLevelRecovery::Update(bool pausePresentation, bool cameraStale, bool levelStable)
{
    if (!pausePresentation)
    {
        Reset();
        return false;
    }
    if (cameraStale)
        m_sawLoading = true;
    if (m_sawLoading && levelStable)
    {
        Reset();
        return true;
    }
    return false;
}
