// Controller input rules shared by the input hooks and the VR frame loop: poll nesting,
// stick-click chords, the head D-pad gesture, menu pointer hits, haptics and pause input.
// Used by dll/core/game.cpp, input.cpp, vr.cpp and the input tests; bodies in input_logic.cpp.
// Pure logic: no Windows, OpenXR or XInput calls, no globals.
#pragma once

#include <cstdint>
#include "runtime_types.h"

// Import shims may call a hooked export (and exports can forward to each other).
// Only the outermost call may fabricate/merge a pad; inner calls return native
// input so our own locomotion is never mistaken for physical stick movement.
class InputPollMergeScope
{
public:
    explicit InputPollMergeScope(unsigned& depth) noexcept : m_depth(depth), m_outermost(depth++ == 0) {}
    ~InputPollMergeScope() { --m_depth; }
    bool IsOutermost() const noexcept { return m_outermost; }
    InputPollMergeScope(const InputPollMergeScope&) = delete;
    InputPollMergeScope& operator=(const InputPollMergeScope&) = delete;

private:
    unsigned& m_depth;
    bool m_outermost;
};

struct MenuChordResult
{
    bool toggled = false;
    bool consumeClicks = false; // a chord owns both inputs until both are released
    // A lone first press is held back from the game for up
    // to kChordHoldMs (bit 0 = first input, bit 1 = second), so the click that
    // starts a chord never also fires its own action (L3 = ability). A lone tap
    // released inside that time is delivered afterwards as a short pulse.
    uint8_t withhold = 0;
    uint8_t pulse = 0;
};

class MenuChordDetector
{
public:
    static constexpr uint64_t kChordWindowMs = 250; // both down within this = chord
    static constexpr uint64_t kChordHoldMs = 150;   // a lone first press waits this long
    static constexpr uint64_t kTapPulseMs = 60;     // a held-back tap, delivered late
    MenuChordResult Update(uint64_t nowMs, bool leftClick, bool rightClick);
    void Reset();

private:
    uint64_t m_firstDownMs = 0;
    uint8_t m_firstSide = 0;
    bool m_latched = false;
    bool m_expired = false;
    uint64_t m_pulseUntilMs = 0;
    uint8_t m_pulseSide = 0;
};

// The head D-pad gesture's state. The zone has
// hysteresis (enter inside the radius, leave only past radius + margin), and the
// routing (D-pad vs normal stick/click) only changes while neither the stick click
// nor the stick is held, so a press started in one mode ends in it: lowering the
// hand mid-press never turns it into L3/ability or a walk, and raising the hand
// while walking never presses a D-pad direction. Directions latch at 0.5 and
// release at 0.4.
class DpadGesture
{
public:
    static constexpr float kExitMarginM = 0.03f, kSideEnterM = 0.03f, kSideStayM = 0.0f;
    static constexpr float kPress = 0.5f, kRelease = 0.4f, kStickHeld = 0.3f;
    bool Update(bool insideEnter, bool insideStay, bool clickHeld, float stickX, float stickY) noexcept;
    uint16_t Directions(float x, float y) noexcept; // XInput D-pad bits
    bool Mode() const noexcept { return m_mode; }
    void Reset() noexcept
    {
        m_zone = m_mode = false;
        m_directions = 0;
    }

private:
    bool m_zone = false, m_mode = false;
    uint16_t m_directions = 0;
};

struct MenuPointerHit
{
    bool hit = false;
    float u = 0.0f;
    float v = 0.0f;
};

// Ray/panel intersection in anchor-local space. `centerX`/`centerY` place the
// panel off straight-ahead, which is what the grab handle moves; they must match
// the composition quad's own offsets or the pointer lands somewhere the player
// cannot see.
MenuPointerHit IntersectMenuQuad(const float origin[3], const float direction[3], float distance, float width,
    float height, float centerY, float centerX = 0.0f);

float BlendXInputMotors(uint16_t lowFrequencyMotor, uint16_t highFrequencyMotor);

// Distance is from the tracked head centre, preserving the original 30 cm
// gesture by default. Title-specific side-of-head requirements stay at caller.
bool DpadHeadWithinRadius(
    const float head[3], const float controller[3], float radiusMetres, float marginMetres = 0.0f) noexcept;

struct DpadStickInput
{
    bool active = false;
    float x = 0.0f, y = 0.0f;
};
// Consume the physical right stick before any title, turn or scope snapshot.
// Only the optional thumb-rest gesture changes those axes; left movement,
// handedness, buttons and the existing head gesture are independent.
DpadStickInput ConsumeThumbrestDpad(bool enabled, bool touched, bool inputReady, float& rightX, float& rightY) noexcept;
uint16_t DpadDirectionButtons(float x, float y) noexcept;

// Peak-hold haptic sampling. Halo drives controller rumble as short XInput
// SetState pulses, but the VR frame loop samples the requested amplitude far
// less often than those pulses arrive. A plain last-value latch therefore
// aliases a brief gunfire pulse (SetState(high) then SetState(0) within one
// frame) to zero and drops it, so shooting stops rumbling reliably. Peak-hold
// keeps the maximum amplitude requested since the last applied frame: `peak` is
// that running maximum and `latest` the most recent instantaneous value.
// `apply` = max(peak, latest) and `carry` = latest, so a one-shot pulse fires
// exactly once and a sustained rumble persists. All values are clamped to [0,1].
struct HapticPeakSample
{
    float apply = 0.0f;
    float carry = 0.0f;
};

HapticPeakSample SampleHapticPeak(float peak, float latest);

// Contact feedback is hand-specific while Halo's ordinary XInput rumble is
// shared. Each hand receives the stronger request, clamped to OpenXR's legal
// amplitude range, so collision feedback cannot suppress weapon/vehicle rumble.
float MergeHapticAmplitude(float game, float contact);

// The mod owns virtual slot 0 when no physical pad is present. A valid
// XInputSetState request must therefore observe a connected controller even
// when title capability policy suppresses the actual haptic effect.
uint32_t NormalizeVirtualXInputSetStateResult(uint32_t originalResult, uint32_t userIndex, bool hasVibrationRequest);

// Menu/Start is always passed through as controller input, but only a proven
// Halo 3 gameplay owner may use its edge to change VR pause presentation.
bool PausePresentationInputAllowed(bool sharedGameplayOwner);

// The Y+B fallback may inject Start for either Halo 3's shared gameplay path or
// a title-specific ODST/Reach/Halo 4 owner. This is deliberately separate from
// PausePresentationInputAllowed: ODST and Reach follow their proven native pause
// flags instead of guessing presentation state from the controller edge.
bool PauseToggleInputAllowed(bool sharedGameplayOwner, bool titleSpecificPauseOwner);

// ODST's camera-only ownership may not publish an explicit active title, while
// Reach and Halo 4 do. Keep that title admission in one testable helper so a
// newly explicit title cannot silently lose the shared Y+B chord.
inline constexpr bool TitleSpecificPauseToggleOwner(GameTitle activeTitle, bool odstCameraOnly)
{
    return odstCameraOnly || activeTitle == GameTitle::HaloReach || activeTitle == GameTitle::Halo4 ||
           activeTitle == GameTitle::Halo2 || activeTitle == GameTitle::HaloCE;
}

// Reach's on-foot VR layout trades the physical left trigger and X button so
// armour ability stays on the trigger and grenade moves to X. Vehicles must
// receive Reach's native layout instead. `playerInVehicleProven` is deliberately
// fail-open for the existing remap: an unavailable or stale title sample is
// false, so a failed optional detector cannot silently change on-foot controls.
inline constexpr bool ReachShouldSwapLeftHandActions(bool reachActive, bool playerInVehicleProven)
{
    return reachActive && !playerInVehicleProven;
}

// Hold-mode two-hand aiming uses the barrel zone only to acquire the support
// grip. Once acquired, ordinary movement of either hand must not drop the hold;
// releasing the grip is the explicit disengage action.
bool UpdateTwoHandHold(bool wasEngaged, bool gripHeld, bool inGrabZone);

// Pad-first menus refuse MCC's switch to keyboard/mouse so the pad keeps the
// highlight. Exception (found in headset testing): for the whole pause presentation the
// switch is allowed. MCC's own modal (Save & Quit) takes mouse clicks only if it
// OPENED in keyboard/mouse mode: switching 0.8 s after the modal opened left the
// click on YES still failing; with pad-first off (mode switched before the modal
// opened) the same click quit. A pad press still switches MCC back.
inline bool PadFirstAllowsMouseMode(bool pausePresentation) noexcept
{
    return pausePresentation;
}

// Tracks the loading gap caused by Restart Level while Halo's pause menu owns
// the screen. Restart bypasses the normal Start/unpause edge, so the next
// stable gameplay camera must clear the 2D pause presentation.
class PauseLevelRecovery
{
public:
    bool Update(bool pausePresentation, bool cameraStale, bool levelStable);
    void Reset();

private:
    bool m_sawLoading = false;
};
