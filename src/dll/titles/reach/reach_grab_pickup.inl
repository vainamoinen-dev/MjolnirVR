// Halo Reach grab pickup: lets the support hand pick a weapon off the floor by steering the game's own pickup search.
// Single-player only; hooks forward stock when the switch is off or any check fails.
#pragma once
// Included once, by src/dll/titles/reach/reach_camera_core.cpp. Not a standalone header: it uses what the
//   host defines above its include line, namely GetCurrentThreadId, InstallReachGrabPickup,
//   RemoveReachGrabPickup, ReportReachGrabPickup.
// Reach single-player "grab a gun off the floor".
// The game keeps choosing and picking up the weapon itself; while the support
// hand is lowered and gripping, its own target search is centred on that hand
// instead of the unit,
// and only weapons within the grab radius of the hand stay candidates. The
// pickup itself is the game's Use press (input thread, Game_UpdateGrabPickup).
//   probe    0x4E00C(unit, out vec3)  hooked; steered only from the search's
//            call 0x4EFCE (returns to 0x4EFD3). The other caller 0x4E118 is stock.
//   weapon   0x4FF4C(player, weapon, probe*, accum*)  hooked; returns whether
//            the weapon became a candidate (search call 0x4F115 -> 0x4F11A).
//   pick     0x4F280(player, targets*)  hooked (one caller, player control
//            0x626BB): runs the search 0x4F314 then the final pick 0x4EB54.
//            targets[0] = {word type @+0, dword object @+8}; types 2 and 10
//            are weapons (the pickup handler's own test). Only a weapon the
//            game itself chose after a hand-steered search is published.
//   gate     sim type byte [TLS[0xC17B18]+0x48]+0x11 (proof 0x506DE) and, as
//            the game's own 0x58E70 reads it, this machine's index
//            [[TLS+0x118]+0x78] and the session mask [..+4]. Steering only
//            while sim is 1 (local), or 3 with no other machine (MCC hosts a
//            solo campaign as a one-machine session). docs/GRAB-G1-PLAN.md 7.1.
// Holster primitives (docs/GRAB-G1-PLAN.md 7.2-7.4, 9), a second hook set: the
// G1b hand owner (grip-held drop, stow, grab-back) and the F1-hidden rig probe:
//   action   0x4BC0C(player, unit, player_action*)  hooked (one caller 0x47998,
//            the simulation tick): requests start here, on the game thread.
//   control  the same action, applied next by 0x4D7D0 -> 0x483468, which writes
//            +0x34E from the action's desired slot +0x46 (9.1). While the mod
//            owns the hand, the detour writes +0x46 before that apply.
//   allow    0x48E37C(unit, ?, weapon)  hooked; "may this weapon be selected".
//            A holstered weapon is refused only for the next-weapon chooser
//            (call 0x48EDFA) and the switch handler (call 0x50535F); pickup
//            and add-weapon always get the stock answer.
//   switch   +0x34E = target slot (-1 = put away), then 0x49F210(unit, 0x38),
//            as add-weapon does at 0x48E07B. Drop: 0x48F0FC(unit, weapon,
//            reselect false, skip false): the hand is empty after it.
//   touch    0x4E710(player, object, probe*)  hooked (one caller 0x4E19C, the
//            per-tick walk-over pass 0x4E080): ammo from a gun touched, and a
//            free-slot pickup (0x48E534, then action 0x2E mode 3). A gun the
//            player dropped with grip-held is skipped until the hand takes it.
//   limit    the player-only 2-weapon compare `cmp r12d,2` at 0x4E492 (imm8
//            0x4E495; its one caller 0x48EBFB skips AI units) reads 3 while
//            reach_third_weapon (or the probe) asks for it and the gate holds.
//   survey   rig probe script 2 only: alternate searches centred on a 3 m grid
//            out to 15 m; accepted weapons noted, refused, pick cleared.
// Hooks forward stock when the switch is off or any check fails.
bool RemoveReachGrabPickup();
MCCVR_MUZZLE_HOOK_OPS(ReachGrabHookOps, RemoveReachGrabPickup, MCCVR_MUZZLE_SIGNATURE_BOUND);
constexpr MuzzleLogText kReachGrabLog{"Reach grab pickup StockFallback: missing/ambiguous binding +%X",
    "Reach grab pickup StockFallback: changed native edge +%X", "Reach grab pickup StockFallback: hook creation failed",
    "Reach grab pickup StockFallback: hook enable failed", "Reach grab pickup CleanupRequired: disable failed",
    "Reach grab pickup CleanupRequired: callbacks or ingress busy",
    "Reach grab pickup CleanupRequired: removal failed"};
bool RemoveReachHolsterHooks();
MCCVR_MUZZLE_HOOK_OPS(ReachHolsterHookOps, RemoveReachHolsterHooks, MCCVR_MUZZLE_SIGNATURE_BOUND);
constexpr MuzzleLogText kReachHolsterLog{"Reach holster StockFallback: missing/ambiguous binding +%X",
    "Reach holster StockFallback: changed native edge +%X", "Reach holster StockFallback: hook creation failed",
    "Reach holster StockFallback: hook enable failed", "Reach holster CleanupRequired: disable failed",
    "Reach holster CleanupRequired: callbacks or ingress busy", "Reach holster CleanupRequired: removal failed"};

struct ReachGrabPickupFeature
{
    void* targets[3]{};
    uint64_t(__fastcall* probeOriginal)(uint32_t, float*) = nullptr;
    uint64_t(__fastcall* weaponOriginal)(uint32_t, uint32_t, const float*, void*) = nullptr;
    uint64_t(__fastcall* pickOriginal)(uint32_t, void*) = nullptr;
    std::atomic<uint32_t> callbacks{};
    std::atomic<bool> enabled{};
    uintptr_t probeReturn = 0, weaponReturn = 0, tlsIndex = 0;
    // Input thread -> game thread: grab intent is live.
    std::atomic<bool> steer{};
    // Render prep -> game thread: the support hand in world units and the
    // tracking-to-world axes, under a sequence counter (odd while writing; A4-21).
    std::atomic<uint32_t> handSeq{};
    std::atomic<float> hand[3]{}, unitsPerMetre{}, axis[9]{};
    std::atomic<uint64_t> handAtMs{};
    // Input thread -> game thread: the head orientation in tracking space.
    std::atomic<uint32_t> headSeq{};
    std::atomic<float> headQ[4]{};
    // Game thread: the thread whose current search was steered, the weapon
    // the game finally chose from it (pickTarget is written before pickAtMs),
    // and the gate as last read.
    std::atomic<uint32_t> steeredThread{};
    std::atomic<uint32_t> pickTarget{grab_pickup::kNoTarget};
    std::atomic<uint64_t> pickAtMs{};
    std::atomic<int> simType{-1}, otherMachines{-1};
    std::atomic<bool> gateOpen{};
    bool machinesProven = false;
    std::atomic<uint64_t> steered{}, filtered{}, inReach{}, picked{};
    // Game thread only: the nearest weapon candidate of the current search.
    uint32_t nearestWeapon = grab_pickup::kNoTarget;
    float nearestD2 = 0, nearestCentre[3]{};
    grab_pickup::NearestWeaponLog nearestLog;

    // Rig survey (probe script 2, game thread): the thread whose current search
    // was moved to a survey point; its candidates are noted, never offered.
    std::atomic<uint32_t> surveyThread{};
    uint32_t lastNormalNearest = grab_pickup::kNoTarget;
    uint32_t heldSlots[4]{~0u, ~0u, ~0u, ~0u}; // the local unit's slots, from the hand tick
    float surveyBase[3]{};                     // the stock probe point of the current survey search

    // Holster hook set (second set; GRAB-G1-PLAN.md 7.3-7.4, 8).
    void* holsterTargets[3]{};
    uint64_t(__fastcall* actionOriginal)(uint32_t, uint32_t, void*) = nullptr;
    uint64_t(__fastcall* allowOriginal)(uint64_t, uint64_t, uint64_t) = nullptr;
    uint64_t(__fastcall* touchOriginal)(uint32_t, uint32_t, const float*) = nullptr;
    // Input thread -> game thread: one hand request, (sequence << 16) | (kind << 8) | slot.
    std::atomic<uint32_t> requestPacked{};
    // Game thread -> input thread: the local unit and the plan, under a sequence counter.
    std::atomic<uint32_t> unitSeq{}, unitSlots[4]{};
    std::atomic<int> unitRole{-1}, planMode{};
    std::atomic<uint64_t> unitAtMs{};
    uint64_t(__fastcall* actionStart)(uint32_t, uint32_t) = nullptr;                  // 0x49F210
    uint64_t(__fastcall* dropWeapon)(uint32_t, uint32_t, uint8_t, uint8_t) = nullptr; // 0x48F0FC
    std::atomic<uint32_t> holsterCallbacks{};
    std::atomic<bool> holsterEnabled{};
    uintptr_t chooserReturn = 0, switchReturn = 0;
    uint8_t* limitImm = nullptr;
    bool limitRaised = false;
    std::atomic<uint32_t> probeUnit{grab_pickup::kNoTarget}, modSwitchThread{};
    std::atomic<uint32_t> holstered[4]{}; // weapon handle per slot the mod holstered, 0 = none
    std::atomic<uint64_t> excluded{};
} g_reachGrab;

constexpr uint64_t kReachGrabHandFreshMs = 150;

void ReachGrabPublishHand(bool valid, const contact_melee::TrackingToWorld& space, const float position[3])
{
    auto& feature = g_reachGrab;
    if (!g_config.reach_grab_pickup || !feature.enabled.load(std::memory_order_acquire))
        return;
    const uint32_t seq = feature.handSeq.load(std::memory_order_relaxed);
    feature.handSeq.store(seq + 1, std::memory_order_relaxed); // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    feature.handAtMs.store(0, std::memory_order_relaxed);
    bool ok = valid && position && space.Valid();
    contact_melee::Point world{};
    if (ok)
    {
        world = space.World({position[0], position[1], position[2]});
        ok = std::isfinite(world.x) && std::isfinite(world.y) && std::isfinite(world.z);
    }
    if (ok)
    {
        feature.hand[0].store(world.x, std::memory_order_relaxed);
        feature.hand[1].store(world.y, std::memory_order_relaxed);
        feature.hand[2].store(world.z, std::memory_order_relaxed);
        feature.unitsPerMetre.store(space.unitsPerMetre, std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
        {
            feature.axis[i * 3 + 0].store(space.axis[i].x, std::memory_order_relaxed);
            feature.axis[i * 3 + 1].store(space.axis[i].y, std::memory_order_relaxed);
            feature.axis[i * 3 + 2].store(space.axis[i].z, std::memory_order_relaxed);
        }
        feature.handAtMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    feature.handSeq.store(seq + 2, std::memory_order_release); // even: complete
}

// A consistent hand sample (same sequence before and after, never mid-write).
bool ReachGrabReadHand(float out[3], float& unitsPerMetre, float axis[9] = nullptr)
{
    auto& feature = g_reachGrab;
    const uint32_t before = feature.handSeq.load(std::memory_order_acquire);
    if (before & 1)
        return false;
    const uint64_t at = feature.handAtMs.load(std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        out[i] = feature.hand[i].load(std::memory_order_relaxed);
    unitsPerMetre = feature.unitsPerMetre.load(std::memory_order_relaxed);
    if (axis)
        for (int i = 0; i < 9; ++i)
            axis[i] = feature.axis[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (feature.handSeq.load(std::memory_order_relaxed) != before)
        return false;
    const uint64_t now = GetTickCount64();
    return at && now >= at && now - at <= kReachGrabHandFreshMs && std::isfinite(unitsPerMetre) && unitsPerMetre > 0;
}

void ReachGrabPublishHead(const float q[4])
{
    auto& feature = g_reachGrab;
    const uint32_t seq = feature.headSeq.load(std::memory_order_relaxed);
    feature.headSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (int i = 0; i < 4; ++i)
        feature.headQ[i].store(q ? q[i] : 0.0f, std::memory_order_relaxed);
    feature.headSeq.store(seq + 2, std::memory_order_release);
}
bool ReachGrabReadHead(float q[4])
{
    auto& feature = g_reachGrab;
    const uint32_t before = feature.headSeq.load(std::memory_order_acquire);
    if (before & 1)
        return false;
    for (int i = 0; i < 4; ++i)
        q[i] = feature.headQ[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    return feature.headSeq.load(std::memory_order_relaxed) == before &&
           q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] > 0.5f;
}

// Native reads on the game thread (the probe/evaluator's own thread), through
// the same TLS block the proven code uses. -1 / false when unreadable.
__declspec(noinline) int ReachGrabReadSimType(int& otherMachines)
{
    otherMachines = -1;
    __try
    {
        const uint32_t index = *reinterpret_cast<const uint32_t*>(g_reachGrab.tlsIndex);
        const auto* slots = reinterpret_cast<const uintptr_t*>(__readgsqword(0x58));
        const uintptr_t block = slots[index];
        const uintptr_t globals = *reinterpret_cast<const uintptr_t*>(block + 0x48);
        if (!globals)
            return -1;
        const int sim = *reinterpret_cast<const uint8_t*>(globals + 0x11);
        // 0x58E70's own reads (proven at install): index dword +0x78, mask dword +4.
        const uintptr_t session = g_reachGrab.machinesProven ? *reinterpret_cast<const uintptr_t*>(block + 0x118) : 0;
        if (session)
            otherMachines = grab_pickup::MachinesOtherThan(
                *reinterpret_cast<const int32_t*>(session + 0x78), *reinterpret_cast<const uint32_t*>(session + 0x4));
        return sim;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Reach grab sim type faulted");
        otherMachines = -1;
        return -1;
    }
}
// The gate, read on the game thread; logs on change.
bool ReachGrabUpdateGate()
{
    auto& feature = g_reachGrab;
    int others = -1;
    const int sim = ReachGrabReadSimType(others);
    const bool open = grab_pickup::SimAllowsGrab(sim, others);
    const int lastSim = feature.simType.exchange(sim, std::memory_order_acq_rel);
    const int lastOthers = feature.otherMachines.exchange(others, std::memory_order_acq_rel);
    feature.gateOpen.store(open, std::memory_order_release);
    if (lastSim != sim || lastOthers != others)
        LOG("Reach grab pickup: game simulation type %d, other machines %d (grab steers: %s)", sim, others,
            open ? "yes" : "no");
    return open;
}
__declspec(noinline) uintptr_t ReachGrabObjectDatum(uint32_t object)
{
    __try
    {
        const uint32_t index = *reinterpret_cast<const uint32_t*>(g_reachGrab.tlsIndex);
        const auto* slots = reinterpret_cast<const uintptr_t*>(__readgsqword(0x58));
        const uintptr_t table = *reinterpret_cast<const uintptr_t*>(slots[index] + 0x10);
        const uintptr_t data = *reinterpret_cast<const uintptr_t*>(table + 0x50);
        return *reinterpret_cast<const uintptr_t*>(data + uintptr_t(object & 0xFFFF) * 24 + 0x10);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading a Reach grab object datum faulted");
        return 0;
    }
}
__declspec(noinline) bool ReachGrabReadObjectCentre(uint32_t object, float out[3])
{
    const uintptr_t datum = ReachGrabObjectDatum(object);
    if (!datum)
        return false;
    __try
    {
        std::memcpy(out, reinterpret_cast<const void*>(datum + 0x20), 12);
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Reach grab object centre faulted");
        return false;
    }
}

// The search's probe: centre it on the hand while a grab is live.
bool ReachGrabSteerProbe(float* out)
{
    auto& feature = g_reachGrab;
    const bool open = ReachGrabUpdateGate();
    float hand[3], unitsPerMetre = 0;
    if (!open || !feature.steer.load(std::memory_order_acquire) || !ReachGrabReadHand(hand, unitsPerMetre))
        return false;
    std::memcpy(out, hand, sizeof(hand));
    feature.steered.fetch_add(1, std::memory_order_relaxed);
    return true;
}
// ---- Rig survey (probe script 2 only; build 32 gate Q5) ----
// The stock search sees about 2.3 m around the body, so a map weapon farther
// off never shows in the nearest line. While no weapon is within that reach,
// every other search is centred on the next point of a 3 m grid out to 15 m
// (the game's own query and pickup rules). Its accepted weapons are noted and
// refused, and its pick is cleared, so a survey search never offers anything.
constexpr float kSurveyStepM = 3.0f, kSurveyRangeM = 15.0f;
constexpr int kSurveySide = 11; // -15..+15 m in 3 m steps
struct ReachGrabSurvey
{
    int index = 0, points = 0;
    uint32_t parity = 0;
    uint32_t best = grab_pickup::kNoTarget;
    float bestD2 = 0, bestCentre[3]{};
    grab_pickup::NearestWeaponLog log;
} g_reachGrabSurvey;

// End of a sweep: the nearest weapon found, on change of id or 0.25 m bucket.
void ReachGrabSurveyEnd(const float hand[3], float unitsPerMetre, const float axis[9])
{
    auto& survey = g_reachGrabSurvey;
    float offset[3]{}, head[4];
    bool known = false;
    if (survey.best != grab_pickup::kNoTarget && ReachGrabReadHead(head))
    {
        const float world[3]{
            survey.bestCentre[0] - hand[0], survey.bestCentre[1] - hand[1], survey.bestCentre[2] - hand[2]};
        float tracking[3];
        for (int i = 0; i < 3; ++i)
            tracking[i] =
                (world[0] * axis[i * 3] + world[1] * axis[i * 3 + 1] + world[2] * axis[i * 3 + 2]) / unitsPerMetre;
        known = grab_pickup::BodyOffset(tracking, head, offset);
    }
    if (survey.best != grab_pickup::kNoTarget && !known)
        offset[0] = 1000.0f;
    if (survey.log.Changed(survey.best, offset))
    {
        const float metres = std::sqrt(survey.bestD2) / unitsPerMetre;
        if (survey.best == grab_pickup::kNoTarget)
            LOG("Reach grab probe: survey found no weapon within %.0f m (%d points)", kSurveyRangeM, survey.points);
        else if (!known)
            LOG("Reach grab probe: survey nearest weapon %08X at %.1f m, offset from the hand unknown", survey.best,
                metres);
        else
            LOG("Reach grab probe: survey nearest weapon %08X at %.1f m, offset from the hand right %+.2f up %+.2f "
                "forward %+.2f m",
                survey.best, metres, offset[0], offset[1], offset[2]);
    }
    survey.best = grab_pickup::kNoTarget;
    survey.points = 0;
}
// Moves this search to the next survey point; false when this search stays stock.
bool ReachGrabSurveyProbe(float* out)
{
    auto& feature = g_reachGrab;
    auto& survey = g_reachGrabSurvey;
    if (g_config.reach_grab_probe != 2 || !feature.holsterEnabled.load(std::memory_order_acquire) ||
        !feature.gateOpen.load(std::memory_order_acquire) || feature.lastNormalNearest != grab_pickup::kNoTarget ||
        (++survey.parity & 1))
        return false;
    float hand[3], unitsPerMetre = 0, axis[9];
    if (!ReachGrabReadHand(hand, unitsPerMetre, axis))
        return false;
    for (int tries = 0; tries < kSurveySide * kSurveySide; ++tries)
    {
        if (survey.index >= kSurveySide * kSurveySide)
        {
            ReachGrabSurveyEnd(hand, unitsPerMetre, axis);
            survey.index = 0;
        }
        const int i = survey.index++;
        const float x = float(i % kSurveySide - kSurveySide / 2) * kSurveyStepM;
        const float z = float(i / kSurveySide - kSurveySide / 2) * kSurveyStepM;
        if (x * x + z * z > kSurveyRangeM * kSurveyRangeM)
            continue;
        // Tracking x and z are the horizontal axes (axis[i] = tracking axis i in world).
        std::memcpy(feature.surveyBase, out, sizeof(feature.surveyBase));
        for (int c = 0; c < 3; ++c)
            out[c] += (x * axis[c] + z * axis[6 + c]) * unitsPerMetre;
        ++survey.points;
        return true;
    }
    return false;
}
// A weapon a survey search accepted (never one the probe unit carries).
void ReachGrabSurveyNote(uint32_t weapon)
{
    auto& feature = g_reachGrab;
    auto& survey = g_reachGrabSurvey;
    for (const uint32_t held : feature.heldSlots)
        if (held == weapon)
            return;
    float centre[3];
    if (!ReachGrabReadObjectCentre(weapon, centre))
        return;
    const float dx = centre[0] - feature.surveyBase[0], dy = centre[1] - feature.surveyBase[1],
                dz = centre[2] - feature.surveyBase[2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (survey.best != grab_pickup::kNoTarget && d2 >= survey.bestD2)
        return;
    survey.best = weapon;
    survey.bestD2 = d2;
    std::memcpy(survey.bestCentre, centre, sizeof(centre));
}
// The final pick's own empty state (as its prologue writes it): 3 x 12 bytes,
// type/flags qword 0 at +0, object -1 at +8.
__declspec(noinline) void ReachGrabClearPick(void* targets)
{
    __try
    {
        auto* bytes = static_cast<uint8_t*>(targets);
        for (int i = 0; i < 3; ++i)
        {
            *reinterpret_cast<uint64_t*>(bytes + i * 12) = 0;
            *reinterpret_cast<uint32_t*>(bytes + i * 12 + 8) = grab_pickup::kNoTarget;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("clearing the Reach survey pick faulted");
    }
}

__declspec(noinline) uint64_t __fastcall ReachGrabProbeDetour(uint32_t unit, float* out)
{
    auto& feature = g_reachGrab;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        if (feature.probeOriginal)
            result = feature.probeOriginal(unit, out);
        if (out && reinterpret_cast<uintptr_t>(_ReturnAddress()) == feature.probeReturn &&
            feature.enabled.load(std::memory_order_acquire))
        {
            const bool steered = g_config.reach_grab_pickup && ReachGrabSteerProbe(out);
            feature.steeredThread.store(steered ? GetCurrentThreadId() : 0, std::memory_order_relaxed);
            feature.surveyThread.store(
                !steered && ReachGrabSurveyProbe(out) ? GetCurrentThreadId() : 0, std::memory_order_relaxed);
        }
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// While the search is steered, a weapon farther than the grab radius from the
// hand is not offered (stock radius 0.75 world units is about 2.3 m).
bool ReachGrabWithinReach(uint32_t weapon, const float* probe)
{
    float centre[3];
    float hand[3], unitsPerMetre = 0;
    if (!probe || !ReachGrabReadHand(hand, unitsPerMetre) || !ReachGrabReadObjectCentre(weapon, centre))
        return false;
    const float radius = std::clamp(g_config.reach_grab_radius_m, 0.10f, 0.60f) * unitsPerMetre;
    const float dx = centre[0] - probe[0], dy = centre[1] - probe[1], dz = centre[2] - probe[2];
    return dx * dx + dy * dy + dz * dz <= radius * radius;
}
// A weapon the game accepted as a candidate: keep the one nearest the probe.
void ReachGrabNoteCandidate(uint32_t weapon, const float* probe)
{
    auto& feature = g_reachGrab;
    float centre[3];
    if (!probe || !ReachGrabReadObjectCentre(weapon, centre))
        return;
    const float dx = centre[0] - probe[0], dy = centre[1] - probe[1], dz = centre[2] - probe[2];
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (feature.nearestWeapon != grab_pickup::kNoTarget && d2 >= feature.nearestD2)
        return;
    feature.nearestWeapon = weapon;
    feature.nearestD2 = d2;
    std::memcpy(feature.nearestCentre, centre, sizeof(centre));
}
__declspec(noinline) uint64_t __fastcall ReachGrabWeaponDetour(
    uint32_t player, uint32_t weapon, const float* probe, void* accum)
{
    auto& feature = g_reachGrab;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        const bool search = reinterpret_cast<uintptr_t>(_ReturnAddress()) == feature.weaponReturn &&
                            feature.enabled.load(std::memory_order_acquire);
        const bool grab = search && feature.steeredThread.load(std::memory_order_relaxed) == GetCurrentThreadId();
        const bool survey = search && feature.surveyThread.load(std::memory_order_relaxed) == GetCurrentThreadId();
        if (grab && !ReachGrabWithinReach(weapon, probe))
        {
            feature.filtered.fetch_add(1, std::memory_order_relaxed);
            // Never offered, but still the nearest for the log: RigBot steers
            // the hand by it (build 33 rig finding 3). Not a carried weapon.
            if (g_config.reach_grab_pickup && std::find(std::begin(feature.heldSlots), std::end(feature.heldSlots),
                                                  weapon) == std::end(feature.heldSlots))
                ReachGrabNoteCandidate(weapon, probe);
        }
        else if (survey && feature.weaponOriginal)
        {
            // Noted, then refused: a survey search never offers a weapon.
            if (static_cast<uint8_t>(feature.weaponOriginal(player, weapon, probe, accum)))
                ReachGrabSurveyNote(weapon);
        }
        else if (feature.weaponOriginal)
        {
            result = feature.weaponOriginal(player, weapon, probe, accum);
            if (grab && static_cast<uint8_t>(result))
                feature.inReach.fetch_add(1, std::memory_order_relaxed);
            if (search && static_cast<uint8_t>(result) && g_config.reach_grab_pickup)
                ReachGrabNoteCandidate(weapon, probe);
        }
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// After each search: the nearest candidate weapon and its offset from the hand
// in the body frame (right/up/forward, metres), on change of id or 0.25 m
// bucket. RigBot steers the hand by it; it also says whether any weapon lies
// within the stock search radius (GRAB-G1-PLAN.md section 2).
void ReachGrabLogNearest()
{
    auto& feature = g_reachGrab;
    const uint32_t weapon = feature.nearestWeapon;
    float offset[3]{}, hand[3], unitsPerMetre = 0, axis[9], head[4];
    bool handKnown = false;
    if (weapon != grab_pickup::kNoTarget && ReachGrabReadHand(hand, unitsPerMetre, axis) && ReachGrabReadHead(head))
    {
        const float world[3]{
            feature.nearestCentre[0] - hand[0], feature.nearestCentre[1] - hand[1], feature.nearestCentre[2] - hand[2]};
        float tracking[3];
        for (int i = 0; i < 3; ++i)
            tracking[i] =
                (world[0] * axis[i * 3] + world[1] * axis[i * 3 + 1] + world[2] * axis[i * 3 + 2]) / unitsPerMetre;
        handKnown = grab_pickup::BodyOffset(tracking, head, offset);
    }
    if (weapon != grab_pickup::kNoTarget && !handKnown)
        offset[0] = 1000.0f; // its own bucket: "hand unknown"
    if (!feature.nearestLog.Changed(weapon, offset))
        return;
    if (weapon == grab_pickup::kNoTarget)
        LOG("Reach grab pickup: nearest weapon none in the search radius");
    else if (!handKnown)
        LOG("Reach grab pickup: nearest weapon %08X, offset from the hand unknown (hand or head not published)",
            weapon);
    else
        LOG("Reach grab pickup: nearest weapon %08X offset from the hand right %+.2f up %+.2f forward %+.2f m", weapon,
            offset[0], offset[1], offset[2]);
}

__declspec(noinline) uint32_t ReachGrabReadPick(const void* targets)
{
    __try
    {
        const auto* bytes = static_cast<const uint8_t*>(targets);
        const uint16_t type = *reinterpret_cast<const uint16_t*>(bytes);
        const uint32_t object = *reinterpret_cast<const uint32_t*>(bytes + 8);
        return (type == 2 || type == 10) && object != grab_pickup::kNoTarget ? object : grab_pickup::kNoTarget;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Reach grab pick faulted");
        return grab_pickup::kNoTarget;
    }
}
__declspec(noinline) uint64_t __fastcall ReachGrabPickDetour(uint32_t player, void* targets)
{
    auto& feature = g_reachGrab;
    feature.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        const bool live = feature.enabled.load(std::memory_order_acquire);
        if (live)
        {
            feature.steeredThread.store(0, std::memory_order_relaxed);
            feature.surveyThread.store(0, std::memory_order_relaxed);
            feature.nearestWeapon = grab_pickup::kNoTarget;
        }
        if (feature.pickOriginal)
            result = feature.pickOriginal(player, targets);
        const bool surveyed = live && feature.surveyThread.load(std::memory_order_relaxed) == GetCurrentThreadId();
        feature.surveyThread.store(0, std::memory_order_relaxed);
        if (surveyed && targets)
            ReachGrabClearPick(targets); // a survey search leaves no target behind
        else if (live && targets)
        {
            // Unsteered searches publish "no grab target", so a stale pick
            // can never keep a press alive.
            const bool steered = feature.steeredThread.load(std::memory_order_relaxed) == GetCurrentThreadId();
            const uint32_t pick = steered ? ReachGrabReadPick(targets) : grab_pickup::kNoTarget;
            if (pick != grab_pickup::kNoTarget)
                feature.picked.fetch_add(1, std::memory_order_relaxed);
            feature.pickTarget.store(pick, std::memory_order_relaxed);
            feature.pickAtMs.store(GetTickCount64(), std::memory_order_release);
            feature.lastNormalNearest = feature.nearestWeapon;
            if (g_config.reach_grab_pickup && feature.gateOpen.load(std::memory_order_acquire))
                ReachGrabLogNearest();
        }
    }
    __finally
    {
        feature.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// ---- Holster primitives and the build 32 rig probe (GRAB-G1-PLAN.md 7.2-7.6, 8) ----

// The player-only 2-weapon compare: 2 normally, 3 while a probe asks for a
// third weapon and the gate holds. Game thread, or teardown after the hooks.
bool ReachHolsterSetLimit(bool raise)
{
    auto& feature = g_reachGrab;
    if (!feature.limitImm || feature.limitRaised == raise)
        return true;
    const uint8_t from = raise ? 2 : 3, to = raise ? 3 : 2;
    DWORD protect = 0, ignored = 0;
    if (*feature.limitImm != from || !VirtualProtect(feature.limitImm, 1, PAGE_EXECUTE_READWRITE, &protect))
    {
        LOG("Reach holster StockFallback: the weapon-limit byte reads %u, expected %u; not written", *feature.limitImm,
            from);
        feature.limitImm = nullptr;
        return false;
    }
    *feature.limitImm = to;
    VirtualProtect(feature.limitImm, 1, protect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), feature.limitImm, 1);
    feature.limitRaised = raise;
    LOG("Reach holster: player weapon limit %u (%s)", to, raise ? "third weapon allowed" : "stock");
    return true;
}

struct ReachUnitWeapons
{
    int role = -1;       // +0x34A, the hand slot (-1 = unarmed)
    int target = -1;     // +0x34E, the slot a switch is heading for (-1 = put away)
    uint32_t slots[4]{}; // +0x350, weapon handles (0xFFFFFFFF = empty)
};
__declspec(noinline) bool ReachHolsterReadUnit(uint32_t unit, ReachUnitWeapons& out)
{
    const uintptr_t datum = ReachGrabObjectDatum(unit);
    if (!datum)
        return false;
    __try
    {
        out.role = *reinterpret_cast<const int8_t*>(datum + 0x34A);
        out.target = *reinterpret_cast<const int8_t*>(datum + 0x34E);
        std::memcpy(out.slots, reinterpret_cast<const void*>(datum + 0x350), sizeof(out.slots));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Reach unit weapon slots faulted");
        return false;
    }
}
// The game's own switch: +0x34E = slot (-1 = put away), then action 0x38.
// accepted = the switch handler's own verdict (0x505278 returns false when
// the hand is already there or 0x4C7A8C finds an animation it may not cut).
__declspec(noinline) bool ReachHolsterStartSwitch(uint32_t unit, int slot, bool& accepted)
{
    auto& feature = g_reachGrab;
    accepted = false;
    const uintptr_t datum = ReachGrabObjectDatum(unit);
    if (!datum || !feature.actionStart)
        return false;
    feature.modSwitchThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    bool ok = false;
    __try
    {
        *reinterpret_cast<int8_t*>(datum + 0x34E) = static_cast<int8_t>(slot);
        accepted = static_cast<uint8_t>(feature.actionStart(unit, 0x38)) != 0;
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("starting the Reach weapon switch faulted");
    }
    feature.modSwitchThread.store(0, std::memory_order_relaxed);
    return ok;
}
// Rig diagnostic: the four animation channels 0x4C7A8C tests (unit +
// word[+0x1AE], 0x1C each: dword +0 and +8 live when not -1, byte +0x1A on,
// word +0x18 class flags tested against the switch's mask).
__declspec(noinline) void ReachHolsterLogChannels(uint32_t unit)
{
    const uintptr_t datum = ReachGrabObjectDatum(unit);
    if (!datum)
        return;
    uint32_t a[4]{}, b[4]{};
    uint16_t flags[4]{};
    uint8_t on[4]{}, secondTarget = 0;
    __try
    {
        const uintptr_t channels = datum + *reinterpret_cast<const int16_t*>(datum + 0x1AE);
        for (int i = 0; i < 4; ++i)
        {
            const uintptr_t c = channels + uintptr_t(i) * 0x1C;
            a[i] = *reinterpret_cast<const uint32_t*>(c);
            b[i] = *reinterpret_cast<const uint32_t*>(c + 8);
            flags[i] = *reinterpret_cast<const uint16_t*>(c + 0x18);
            on[i] = *reinterpret_cast<const uint8_t*>(c + 0x1A);
        }
        secondTarget = *reinterpret_cast<const uint8_t*>(datum + 0x34F);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Reach animation channels faulted");
        return;
    }
    LOG("Reach grab probe: animation channels (switch mask %X) [%08X %08X %04X %u] [%08X %08X %04X %u] "
        "[%08X %08X %04X %u] [%08X %08X %04X %u]",
        secondTarget != 0xFF ? 4u : 0xCu, a[0], b[0], flags[0], on[0], a[1], b[1], flags[1], on[1], a[2], b[2],
        flags[2], on[2], a[3], b[3], flags[3], on[3]);
}
__declspec(noinline) bool ReachHolsterDrop(uint32_t unit, uint32_t weapon)
{
    auto& feature = g_reachGrab;
    if (!feature.dropWeapon)
        return false;
    __try
    {
        (void)feature.dropWeapon(unit, weapon, 0, 0); // reselect false: the hand stays empty
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("dropping the Reach weapon faulted");
        return false;
    }
}

constexpr uint64_t kReachSwitchWaitMs = 3000;

// The local player's seat: the state the Reach LT/X swap uses, when fresh
// (VehicleSteerContextFresh is not it: Reach publishes that on foot too). The
// VR camera's frame sampler is its only publisher, so the sample goes stale
// while that sampler skips frames (headset tracking lost): Unknown, not a seat.
holster::Seat ReachHandSeat(uint64_t now)
{
    const uint64_t at = g_reachVehicleSampleMs.load();
    if (!at || now < at || now - at > 100)
        return holster::Seat::Unknown;
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloReach);
    const uint64_t snapshot = g_reachVehicleInputSnapshot.load();
    if (snapshot == ReachVehicleInputSnapshot(generation, ReachVehicleInputState::OnFoot))
        return holster::Seat::OnFoot;
    return ReachVehicleInputSnapshotIsVehicle(snapshot, generation) ? holster::Seat::Seated : holster::Seat::Unknown;
}
bool ReachHandOnFoot(uint64_t now)
{
    return ReachHandSeat(now) == holster::Seat::OnFoot;
}

// ---- G1b hand owner (GRAB-G1-PLAN.md 9.2): game thread, in the 0x4BC0C detour ----
// One request path: the grip logic (input thread) and the rig probe post Stow,
// Draw(slot) or Drop; the tick issues it with the game's own switch or drop.
// While the mod owns the hand, the player action's desired slot (+0x46) is
// written before the game's own apply (0x4D7D0 -> 0x483468 writes it to
// +0x34E), and every carried weapon not in the hand is marked holstered.
struct ReachHandState
{
    uint32_t player = grab_pickup::kNoTarget; // the one local player under the gate
    uint32_t consumed = 0;                    // sequence of the last request taken
    holster::HandPlan plan;
    holster::RoleRequest request;
    holster::SeatConfirm seat;
    holster::DroppedGuns dropped; // grip-held drops the walk-over pass skips
    bool switchThenStock = false; // a switch-button draw hands the hand back when it ends
    holster::ProbeAction pending = holster::ProbeAction::None;
    int pendingSlot = -1;
    uint32_t dropHandle = grab_pickup::kNoTarget; // the one weapon a drop request may drop
    ReachUnitWeapons last{};
    bool lastValid = false;
    // Rig probe (reach_grab_probe).
    int script = 0;
    holster::ProbeScript steps;
    uint64_t heldSinceMs = 0, readyAtMs = 0;
    int stowedSlot = -1;
    bool thirdWanted = false;
} g_reachHand;

const char* ReachHolsterActionName(holster::ProbeAction action)
{
    switch (action)
    {
    case holster::ProbeAction::Stow:
        return "stow";
    case holster::ProbeAction::Draw:
        return "draw";
    case holster::ProbeAction::Drop:
        return "drop";
    case holster::ProbeAction::ThirdOn:
        return "third weapon on";
    case holster::ProbeAction::Switch:
        return "switch";
    default:
        return "none";
    }
}

// Input thread (and the probe): one request, packed with a sequence number so
// a request is taken once. kind = ProbeAction Stow / Draw / Drop.
void ReachHandPost(holster::ProbeAction kind, int slot)
{
    auto& feature = g_reachGrab;
    const uint32_t seq = (feature.requestPacked.load(std::memory_order_relaxed) >> 16) + 1;
    feature.requestPacked.store(
        ((seq & 0xFFFF) << 16) | (uint32_t(kind) << 8) | uint8_t(slot), std::memory_order_release);
}

// Game thread -> input thread: the local unit's slots and role and the plan.
void ReachHandPublish(const ReachUnitWeapons& w, const holster::HandPlan& plan)
{
    auto& feature = g_reachGrab;
    const uint32_t seq = feature.unitSeq.load(std::memory_order_relaxed);
    feature.unitSeq.store(seq + 1, std::memory_order_relaxed); // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    for (int i = 0; i < 4; ++i)
        feature.unitSlots[i].store(w.slots[i], std::memory_order_relaxed);
    feature.unitRole.store(w.role, std::memory_order_relaxed);
    feature.planMode.store(static_cast<int>(plan.Current()), std::memory_order_relaxed);
    feature.unitAtMs.store(GetTickCount64(), std::memory_order_relaxed);
    feature.unitSeq.store(seq + 2, std::memory_order_release);
}

// Writes the desired slot into this tick's player action (+0x46, proven 9.8).
__declspec(noinline) bool ReachHandWriteAction(void* action, int8_t& own, bool write, int8_t value)
{
    __try
    {
        auto* desired = static_cast<int8_t*>(action) + 0x46;
        own = *desired;
        if (write)
            *desired = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("writing the Reach player action's desired slot faulted");
        return false;
    }
}

void ReachHandRelease(const char* why)
{
    auto& hand = g_reachHand;
    if (hand.plan.Current() != holster::HandPlan::Mode::Stock || hand.request.Active())
        LOG("Reach hand: back to the game (%s)", why);
    hand.plan.Release();
    hand.request.Cancel();
    hand.pending = holster::ProbeAction::None;
    hand.switchThenStock = false;
}

// Starts a request on the unit as it reads now; false when it does not apply.
bool ReachHandStart(holster::ProbeAction kind, int slot, const ReachUnitWeapons& w, uint64_t now)
{
    auto& hand = g_reachHand;
    const bool armed = w.role >= 0 && w.role < 4 && w.slots[w.role] != grab_pickup::kNoTarget;
    switch (kind)
    {
    case holster::ProbeAction::Stow:
        if (!armed)
            return false;
        hand.plan.Take(holster::HandPlan::Mode::Empty, -1);
        // One issue and a 3 s wait (the build 32 gate).
        hand.request.Start(holster::RoleRequest::kUnarmed, now, kReachSwitchWaitMs, kReachSwitchWaitMs);
        break;
    case holster::ProbeAction::Draw:
        if (slot < 0 || slot > 3 || w.slots[slot] == grab_pickup::kNoTarget || w.role == slot)
            return false;
        hand.plan.Take(holster::HandPlan::Mode::Slot, slot);
        hand.request.Start(slot, now, kReachSwitchWaitMs, kReachSwitchWaitMs);
        break;
    case holster::ProbeAction::Drop:
        // One issue only, on the handle in the hand now: a retry must never
        // drop a different weapon the game drew after the first drop.
        if (!armed)
            return false;
        hand.dropHandle = w.slots[w.role];
        hand.plan.Take(holster::HandPlan::Mode::Empty, -1);
        hand.request.Start(holster::RoleRequest::kUnarmed, now, 1000, 500);
        break;
    default:
        return false;
    }
    hand.pending = kind;
    hand.pendingSlot = slot;
    LOG("Reach hand: %s requested, role %d, slot %d", ReachHolsterActionName(kind), w.role, slot);
    return true;
}

// The probe's scripted steps, posted through the same request path.
void ReachHandProbeStep(const ReachUnitWeapons& w, uint64_t now)
{
    auto& hand = g_reachHand;
    if (!hand.readyAtMs)
    {
        const bool armed = w.role >= 0 && w.role < 4 && w.slots[w.role] != grab_pickup::kNoTarget;
        hand.heldSinceMs = armed ? (hand.heldSinceMs ? hand.heldSinceMs : now) : 0;
        if (!hand.heldSinceMs || now < hand.heldSinceMs + 3000)
            return;
        hand.readyAtMs = now;
        LOG("Reach grab probe: script %d ready", hand.script);
    }
    if (hand.request.Active())
        return;
    const holster::ProbeAction action = hand.steps.Next(now - hand.readyAtMs);
    if (action == holster::ProbeAction::None)
        return;
    LOG("Reach grab probe: step %u %s requested", static_cast<unsigned>(hand.steps.Index()),
        ReachHolsterActionName(action));
    if (action == holster::ProbeAction::ThirdOn)
        hand.thirdWanted = true;
    else if (action == holster::ProbeAction::Stow)
    {
        hand.stowedSlot = w.role;
        (void)ReachHandStart(action, -1, w, now);
    }
    else
        (void)ReachHandStart(action, action == holster::ProbeAction::Draw ? hand.stowedSlot : -1, w, now);
}

void ReachHandTick(uint32_t player, uint32_t unit, void* action, bool gateOpen)
{
    auto& feature = g_reachGrab;
    auto& hand = g_reachHand;
    const int script = g_config.reach_grab_probe;
    const uint64_t now = GetTickCount64();
    if (script != hand.script)
    {
        ReachHandRelease("rig probe script changed");
        hand.steps.Select(script);
        hand.script = script;
        hand.heldSinceMs = hand.readyAtMs = 0;
        hand.stowedSlot = -1;
        hand.thirdWanted = false;
        if (script)
            LOG("Reach grab probe: script %d selected (rig only)", script);
    }
    if (hand.player == grab_pickup::kNoTarget)
        hand.player = player;
    if (player != hand.player)
        return;
    feature.probeUnit.store(unit, std::memory_order_relaxed);
    const bool raise = gateOpen && (hand.thirdWanted || g_config.reach_third_weapon);
    if (!ReachHolsterSetLimit(raise) && raise)
        hand.thirdWanted = false;
    ReachUnitWeapons w{};
    if (!ReachHolsterReadUnit(unit, w))
        return;
    std::memcpy(feature.heldSlots, w.slots, sizeof(w.slots));
    const bool changed =
        !hand.lastValid || w.role != hand.last.role || std::memcmp(w.slots, hand.last.slots, sizeof(w.slots)) != 0;
    if (changed && (script || g_config.reach_grab_rig_log))
        LOG("Reach grab probe: role 34A=%d slots=[%08X %08X %08X %08X] (%s)", w.role, w.slots[0], w.slots[1],
            w.slots[2], w.slots[3], hand.request.Active() ? ReachHolsterActionName(hand.pending) : "game");
    const ReachUnitWeapons before = hand.last;
    const bool beforeValid = hand.lastValid;
    hand.last = w;
    hand.lastValid = true;
    hand.dropped.Carried(w.slots);

    bool carried = false;
    for (const uint32_t slot : w.slots)
        carried |= slot != grab_pickup::kNoTarget;
    const bool owned = g_config.reach_grip_held || script != 0;
    // Only a confirmed seat hands the hand back; a stale sample holds the plan
    // and starts nothing (build 33 rig finding 1).
    const holster::Seat seat = hand.seat.Update(ReachHandSeat(now), now);
    if (!gateOpen || !owned || seat == holster::Seat::Seated || hand.seat.LongUnknown(now))
        ReachHandRelease(!gateOpen                       ? "single-player gate closed"
                         : !owned                        ? "grip-held off"
                         : seat == holster::Seat::Seated ? "in a seat"
                                                         : "seat state unknown for 5 s");
    else if (!carried)
        ReachHandRelease("no weapons carried");
    else if (hand.plan.Current() != holster::HandPlan::Mode::Stock && beforeValid)
    {
        // Slot changes the mod did not ask for, before this tick's write. A
        // pickup (add-weapon sets +0x34E and starts the draw; +0x34A follows
        // only after it) makes the hand follow that slot, and the others stay
        // holstered. Any other change hands the hand back.
        int pickupSlot = -1;
        const holster::SlotChange change =
            holster::ClassifySlots(before.slots, w.slots, hand.request.Active(), pickupSlot);
        const int planned = hand.plan.Current() == holster::HandPlan::Mode::Slot ? hand.plan.Slot() : -1;
        if (change == holster::SlotChange::Pickup)
        {
            hand.plan.Take(holster::HandPlan::Mode::Slot, pickupSlot);
            LOG("Reach hand: picked up into slot %d; the hand draws it, the others stay holstered", pickupSlot);
        }
        else if (change == holster::SlotChange::Other)
            ReachHandRelease("the game changed the carried weapons");
        else if (!hand.request.Active() && w.role != before.role && w.role >= 0 && w.role != planned)
            ReachHandRelease("the game changed the hand");
    }
    ReachHandPublish(w, hand.plan);

    const bool active = gateOpen && owned && carried && seat == holster::Seat::OnFoot;
    if (active)
    {
        // A drop lands when its weapon has left the slots, whatever the game put in the hand.
        if (hand.request.Active() && hand.pending == holster::ProbeAction::Drop &&
            std::find(std::begin(w.slots), std::end(w.slots), hand.dropHandle) == std::end(w.slots))
        {
            hand.request.Cancel();
            hand.dropped.Add(hand.dropHandle);
            LOG("Reach hand: drop landed (%08X left the slots), role %d", hand.dropHandle, w.role);
        }
        if (hand.request.Active())
        {
            const holster::Step step = hand.request.Update(w.role, now);
            if (step == holster::Step::Landed)
            {
                LOG("Reach hand: %s landed, role %d", ReachHolsterActionName(hand.pending), w.role);
                if (hand.switchThenStock)
                    ReachHandRelease("switch button");
            }
            else if (step == holster::Step::Refused)
            {
                LOG("Reach hand: %s did not land in time, role %d, 34E=%d", ReachHolsterActionName(hand.pending),
                    w.role, w.target);
                ReachHandRelease("a request did not land");
            }
            else if (step == holster::Step::Issue)
            {
                bool started = false, accepted = true;
                if (hand.pending == holster::ProbeAction::Stow)
                    started = ReachHolsterStartSwitch(unit, -1, accepted);
                else if (hand.pending == holster::ProbeAction::Draw)
                    started = ReachHolsterStartSwitch(unit, hand.pendingSlot, accepted);
                else if (hand.pending == holster::ProbeAction::Drop)
                    started = ReachHolsterDrop(unit, hand.dropHandle); // issued once (1 s retry, 500 ms window)
                if (started && hand.pending != holster::ProbeAction::Drop)
                {
                    // Not accepted = the switch handler started nothing this tick; the
                    // player action's desired slot (+0x46) still carries the request.
                    LOG("Reach hand: %s issued, %s", ReachHolsterActionName(hand.pending),
                        accepted ? "game switch accepted" : "the player action carries it (switch handler: not now)");
                    if (!accepted && script)
                        ReachHolsterLogChannels(unit);
                }
                if (!started)
                {
                    LOG("Reach hand: %s could not start", ReachHolsterActionName(hand.pending));
                    ReachHandRelease("a request could not start");
                }
            }
        }
        // A new request (grip, or the probe's script), one at a time.
        const uint32_t packed = feature.requestPacked.load(std::memory_order_acquire);
        if ((packed >> 16) != hand.consumed)
        {
            hand.consumed = packed >> 16;
            const auto kind = static_cast<holster::ProbeAction>((packed >> 8) & 0xFF);
            const int slot = static_cast<int8_t>(packed & 0xFF);
            if (kind == holster::ProbeAction::Switch)
            {
                // The game's own switch cannot pick a holstered weapon (the
                // chooser is refused them), so the mod does what it would:
                // draw the next carried weapon, then hand the hand back.
                // In Stock the game owns the hand and its own switch runs.
                if (hand.plan.Current() != holster::HandPlan::Mode::Stock && hand.request.Active())
                    LOG("Reach hand: switch ignored (role %d, a request is running)", w.role);
                else if (hand.plan.Current() != holster::HandPlan::Mode::Stock)
                {
                    const int next = holster::NextCarried(w.slots, w.role);
                    if (next >= 0 && ReachHandStart(holster::ProbeAction::Draw, next, w, now))
                        hand.switchThenStock = true;
                    else
                        ReachHandRelease("switch button");
                }
            }
            else if (hand.request.Active() || !ReachHandStart(kind, slot, w, now))
                LOG("Reach hand: %s ignored (role %d, slot %d%s)", ReachHolsterActionName(kind), w.role, slot,
                    hand.request.Active() ? ", a request is running" : "");
        }
        if (script)
            ReachHandProbeStep(w, now);
    }

    // The desired slot, before the game applies it. A switch-button press (the
    // player's own value moving, not an echo of ours) hands the hand back.
    if (action)
    {
        const bool owning = hand.plan.Current() != holster::HandPlan::Mode::Stock;
        int8_t own = 0, write = 0;
        (void)ReachHandWriteAction(action, own, false, 0);
        const bool apply = hand.plan.Apply(own, write);
        if (owning && !apply)
        {
            // Apply already handed the plan back; one line, and no request left running.
            LOG("Reach hand: back to the game (switch button)");
            hand.request.Cancel();
            hand.pending = holster::ProbeAction::None;
        }
        else if (apply && !ReachHandWriteAction(action, own, true, write))
            ReachHandRelease("the player action could not be written");
    }
    // Every carried weapon not planned for the hand is kept out of the game's chooser.
    for (int i = 0; i < 4; ++i)
        feature.holstered[i].store(
            hand.plan.Holstered(i) && w.slots[i] != grab_pickup::kNoTarget ? w.slots[i] : 0, std::memory_order_relaxed);
}

__declspec(noinline) uint64_t __fastcall ReachHolsterActionDetour(uint32_t player, uint32_t unit, void* action)
{
    auto& feature = g_reachGrab;
    feature.holsterCallbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        if (feature.holsterEnabled.load(std::memory_order_acquire) && feature.enabled.load(std::memory_order_acquire))
            ReachHandTick(player, unit, action, ReachGrabUpdateGate());
        if (feature.actionOriginal)
            result = feature.actionOriginal(player, unit, action);
    }
    __finally
    {
        feature.holsterCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
// "May this weapon be selected": a holstered weapon is refused to the chooser
// and the switch handler only (proven return addresses), never to the mod's own
// draw, and never to pickup or add-weapon.
__declspec(noinline) uint64_t __fastcall ReachHolsterAllowDetour(uint64_t unit, uint64_t arg, uint64_t weapon)
{
    auto& feature = g_reachGrab;
    feature.holsterCallbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        if (feature.allowOriginal)
            result = feature.allowOriginal(unit, arg, weapon);
        const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (static_cast<uint8_t>(result) && feature.holsterEnabled.load(std::memory_order_acquire) &&
            (from == feature.chooserReturn || from == feature.switchReturn) &&
            feature.gateOpen.load(std::memory_order_acquire) &&
            static_cast<uint32_t>(unit) == feature.probeUnit.load(std::memory_order_relaxed) &&
            feature.modSwitchThread.load(std::memory_order_relaxed) != GetCurrentThreadId())
            for (const auto& mark : feature.holstered)
            {
                const uint32_t held = mark.load(std::memory_order_relaxed);
                if (held && (held & 0xFFFF) == (weapon & 0xFFFF))
                {
                    result &= ~uint64_t{0xFF};
                    feature.excluded.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
            }
    }
    __finally
    {
        feature.holsterCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// The walk-over pass (game thread): a gun dropped with grip-held stays on the
// floor until the hand grab takes it; every other gun gets the stock pass.
__declspec(noinline) uint64_t __fastcall ReachHolsterTouchDetour(uint32_t player, uint32_t object, const float* probe)
{
    auto& feature = g_reachGrab;
    feature.holsterCallbacks.fetch_add(1, std::memory_order_acq_rel);
    uint64_t result = 0;
    __try
    {
        auto& hand = g_reachHand;
        if (g_config.reach_grip_held && feature.holsterEnabled.load(std::memory_order_acquire) &&
            feature.enabled.load(std::memory_order_acquire) && hand.dropped.Has(object))
        {
            if (hand.dropped.FirstRefusal(object))
                LOG("grip-held: dropped %08X is not walk-over picked up", object);
        }
        else if (feature.touchOriginal)
            result = feature.touchOriginal(player, object, probe);
    }
    __finally
    {
        feature.holsterCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// Input thread: the local unit as the last game tick read it (fresh within 200 ms).
bool ReachHandRead(uint32_t slots[4], int& role, int& planMode)
{
    auto& feature = g_reachGrab;
    const uint32_t before = feature.unitSeq.load(std::memory_order_acquire);
    if (before & 1)
        return false;
    for (int i = 0; i < 4; ++i)
        slots[i] = feature.unitSlots[i].load(std::memory_order_relaxed);
    role = feature.unitRole.load(std::memory_order_relaxed);
    planMode = feature.planMode.load(std::memory_order_relaxed);
    const uint64_t at = feature.unitAtMs.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return feature.unitSeq.load(std::memory_order_relaxed) == before && at && now >= at && now - at <= 200 &&
           feature.holsterEnabled.load(std::memory_order_acquire);
}

bool RemoveReachHolsterHooks()
{
    auto& feature = g_reachGrab;
    feature.holsterEnabled.store(false, std::memory_order_release);
    const void* functions[]{reinterpret_cast<const void*>(&ReachHolsterActionDetour),
        reinterpret_cast<const void*>(&ReachHolsterAllowDetour),
        reinterpret_cast<const void*>(&ReachHolsterTouchDetour)};
    void** originals[]{reinterpret_cast<void**>(&feature.actionOriginal),
        reinterpret_cast<void**>(&feature.allowOriginal), reinterpret_cast<void**>(&feature.touchOriginal)};
    const bool retired = RetireMuzzleHooks<ReachHolsterHookOps>(
        feature.holsterTargets, originals, 3, functions, 3, feature.holsterCallbacks, kReachHolsterLog);
    // Stock limit and no exclusions even when retirement must be retried:
    // with holsterEnabled false no tick would restore them.
    (void)ReachHolsterSetLimit(false);
    for (auto& mark : feature.holstered)
        mark.store(0, std::memory_order_relaxed);
    if (!retired)
        return false;
    feature.probeUnit.store(grab_pickup::kNoTarget, std::memory_order_relaxed);
    g_reachHand = ReachHandState{};
    return true;
}

bool RemoveReachGrabPickup()
{
    auto& feature = g_reachGrab;
    if (!RemoveReachHolsterHooks())
        return false;
    feature.enabled.store(false, std::memory_order_release);
    feature.steer.store(false, std::memory_order_release);
    feature.gateOpen.store(false, std::memory_order_release);
    feature.pickTarget.store(grab_pickup::kNoTarget, std::memory_order_release);
    const void* functions[]{reinterpret_cast<const void*>(&ReachGrabProbeDetour),
        reinterpret_cast<const void*>(&ReachGrabWeaponDetour), reinterpret_cast<const void*>(&ReachGrabPickDetour)};
    void** originals[]{reinterpret_cast<void**>(&feature.probeOriginal),
        reinterpret_cast<void**>(&feature.weaponOriginal), reinterpret_cast<void**>(&feature.pickOriginal)};
    return RetireMuzzleHooks<ReachGrabHookOps>(
        feature.targets, originals, 3, functions, 3, feature.callbacks, kReachGrabLog);
}

// rip-relative operand at base+rva (displacement at +dispAt, next instruction
// at +length) points exactly at base+target.
inline bool ReachGrabRipOperand(uintptr_t base, uint32_t rva, uint32_t dispAt, uint32_t length, uint32_t target)
{
    return base + rva + length + *reinterpret_cast<const int32_t*>(base + rva + dispAt) == base + target;
}

// Second hook set: the holster primitives. Needs the grab set (its TLS proof).
bool InstallReachHolsterHooks(uintptr_t base, size_t size)
{
    auto& feature = g_reachGrab;
    for (auto* target : feature.holsterTargets)
        if (target)
            return false;
    constexpr MuzzleBinding bindings[]{
        // player-action processor: prologue through its argument moves (player r12d, unit r15d, action r14).
        {0x4BC0C, "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 90 48 81 EC 70 01 00 00 "
                  "0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF "
                  "FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 08 8B 05 ?? ?? ?? ?? 4D 8B F0 44 8B E1"},
        // may-select(unit, ?, weapon): prologue through its weapon word.
        {0x48E37C, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 56 48 83 EC 30 44 8B 0D ?? ?? ?? ?? "
                   "44 8B D2 65 48 8B 04 25 58 00 00 00 45 33 F6 8B E9 B9 10 00 00 00 45 0F B7 C0"},
        // unit_action_start_simple(unit, type): a zeroed 0x44-byte request, type at +0, then 0x49F24C.
        {0x49F210, "48 89 5C 24 08 57 48 83 EC 70 8B DA 8B F9 33 D2 48 8D 4C 24 20 44 8D 42 44 E8 ?? ?? ?? ?? "
                   "48 8D 54 24 20 89 5C 24 20 8B CF E8 0E 00 00 00"},
        // add-weapon's own switch: +0x34E = slot, then action 0x38 (proves the offset and the action number).
        {0x48E07B, "45 88 B5 4E 03 00 00 BA 38 00 00 00 8B CB E8"},
        // unit_drop_weapon(unit, weapon, reselect, skip_client_request).
        {0x48F0FC, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 44 8B 15 ?? ?? ?? ?? 41 8A E9 "
                   "65 48 8B 04 25 58 00 00 00 44 8B CA BB 10 00 00 00 83 CA FF 41 8A F8 8B F1"},
        // the 4 weapon slots at +0x350 (the 2-weapon filter's loop).
        {0x4E3FB, "49 8D BE 50 03 00 00 44 8B 74 24 34 BE 04 00 00 00"},
        // the hand role byte +0x34A (remove-weapon clears it to -1).
        {0x48FC12, "0F BE 83 4A 03 00 00 3B F0 75 0A C6 83 4A 03 00 00 FF"},
        // the player-only 2-weapon compare (imm8 at 0x4E495).
        {0x4E48A, "48 8B 9C 24 A0 00 00 00 41 83 FC 02 0F 8C 15 01 00 00 45 33 C0"},
        // its one call, skipped for a unit without a player (+0x1CC == -1).
        {0x48EBE9, "41 8B 8E CC 01 00 00 83 F9 FF 74 0B 4C 8B C7 41 8B D7 E8"},
        // unit control apply (9.1): word +6 against +0x34C, then +0x34E = byte +8 (bit 27 of +0x10 clear).
        {0x4834EF, "41 0F B7 80 4C 03 00 00 66 39 42 06 75 43 48 8B 4A 10 48 8B C1 48 C1 E8 1B 41 84 C1 75 17 "
                   "8A 42 08 41 38 80 4E 03 00 00 74 0B 41 88 80 4E 03 00 00"},
        // its builder copies the player action's dword +0x44 into control +6 ([rbp-0x73]).
        {0x4D9B3, "8B 43 44 48 8D 4B 7B F3 0F 10 43 38 48 83 E1 FC F3 0F 10 4B 3C 89 45 8D"},
        // the tick hands one action ([rbp-0x80]) to 0x4BC0C and then to 0x4D7D0.
        {0x4798D, "4C 8D 45 80 41 8B 57 28 41 8B CC E8 ?? ?? ?? ?? 83 7D 80 01"},
        {0x479AF, "4C 8D 45 80 41 8B 57 28 41 8B CC E8 ?? ?? ?? ?? 48 8B 7C 24 70"},
        // walk-over touch(player, object, probe): prologue through its argument moves.
        {0x4E710, "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 90 00 00 00 44 8B 0D ?? ?? ?? ?? "
                  "4D 8B D8 65 48 8B 04 25 58 00 00 00 41 BA 18 00 00 00 8B DA"},
    };
    // The two may-select edges give the exclusion's return addresses (call + 5).
    constexpr MuzzleEdge kChooserEdge{0x48EDFA, 0x48E37C}, kSwitchEdge{0x50535F, 0x48E37C};
    constexpr MuzzleEdge edges[]{{0x47998, 0x4BC0C}, kChooserEdge, kSwitchEdge, {0x48E089, 0x49F210},
        {0x5090F9, 0x48F0FC}, {0x48EBFB, 0x4E224}, {0x479BA, 0x4D7D0}, {0x4DAA0, 0x483468}, {0x4E19C, 0x4E710}};
    if (!VerifyMuzzleNative<ReachHolsterHookOps>(
            base, size, bindings, std::size(bindings), edges, std::size(edges), kReachHolsterLog))
        return false;
    if (!ReachGrabRipOperand(base, 0x48E394, 3, 7, 0xC17B18) || !ReachGrabRipOperand(base, 0x48F110, 3, 7, 0xC17B18))
    {
        LOG("Reach holster StockFallback: TLS index operand changed");
        return false;
    }
    feature.chooserReturn = base + kChooserEdge.call + 5;
    feature.switchReturn = base + kSwitchEdge.call + 5;
    feature.actionStart = reinterpret_cast<decltype(feature.actionStart)>(base + 0x49F210);
    feature.dropWeapon = reinterpret_cast<decltype(feature.dropWeapon)>(base + 0x48F0FC);
    feature.limitImm = reinterpret_cast<uint8_t*>(base + 0x4E495);
    feature.limitRaised = false;
    for (auto& mark : feature.holstered)
        mark.store(0, std::memory_order_relaxed);
    feature.unitAtMs.store(0, std::memory_order_relaxed);
    g_reachHand = ReachHandState{};
    // A request posted before this install is not taken.
    g_reachHand.consumed = feature.requestPacked.load(std::memory_order_acquire) >> 16;
    const MuzzleHook hooks[]{{0x4BC0C, reinterpret_cast<void*>(&ReachHolsterActionDetour),
                                 reinterpret_cast<void**>(&feature.actionOriginal)},
        {0x48E37C, reinterpret_cast<void*>(&ReachHolsterAllowDetour), reinterpret_cast<void**>(&feature.allowOriginal)},
        {0x4E710, reinterpret_cast<void*>(&ReachHolsterTouchDetour), reinterpret_cast<void**>(&feature.touchOriginal)}};
    if (!InstallMuzzleHooks<ReachHolsterHookOps>(base, hooks, 3, feature.holsterTargets, kReachHolsterLog))
        return false;
    feature.holsterEnabled.store(true, std::memory_order_release);
    LOG("Reach holster Installed: player action + may-select; probe=%d", g_config.reach_grab_probe);
    return true;
}

bool InstallReachGrabPickup(uintptr_t base, size_t size, uint32_t generation)
{
    auto& feature = g_reachGrab;
    for (auto* target : feature.targets)
        if (target)
            return false;
    if (!generation || size <= 0xC17B1C)
        return false;
    constexpr MuzzleBinding bindings[]{
        // probe(unit, out): TLS index load, object datum centre +0x20.
        {0x4E00C,
            "48 83 EC 38 44 8B 05 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 44 8B D9 B9 10 00 00 00 4E 8B 14 C0 45 0F B7 C3"},
        // the search's probe call (unique; the search's own prologue is not);
        // the query radius constant follows it.
        {0x4EFC7, "48 8D 54 24 60 8B CB E8 ?? ?? ?? ?? F3 0F 10 35 ?? ?? ?? ?? 48 8D 44 24 70 C7 44 24 38 00 01 00 00"},
        // weapon evaluator through its object-table reads and the 0.5625 test.
        {0x4FF4C,
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 40 44 8B 15 ?? ?? ?? ?? 33 DB "
            "65 48 8B 04 25 58 00 00 00 4D 8B E9 44 8B F2 44 8B E1 41 BB 18 00 00 00 4E 8B 3C D0 44 0F B7 D1 4D 69 D2 90 04 00 00 "
            "4F 8B 1C 3B 41 0F B7 CE B8 10 00 00 00 4D 03 53 50 4A 8B 04 38 48 8B 50 50 48 8D 04 49 48 8B 4C C2 10 "
            "38 99 A9 01 00 00 0F 85 ?? ?? ?? ?? 41 8B 42 28 39 81 AC 01 00 00 0F 84 ?? ?? ?? ?? "
            "F3 0F 10 41 28 F3 41 0F 5C 40 08 F3 0F 10 49 20 F3 0F 10 51 24 F3 41 0F 5C 50 04 F3 41 0F 5C 08"},
        // final pick: saves targets[0].type (word @+0), clears 3 x 12-byte
        // targets (object dword @+8 = -1), then calls the search.
        {0x4F280, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 30 44 8B 05 ?? ?? ?? ?? 33 ED "
                  "65 48 8B 04 25 58 00 00 00 48 8B DA 0F B7 32 0F B7 D5 41 B9 18 00 00 00 8B F9 4A 8B 04 C0 "
                  "44 0F B7 C1 4D 69 D0 90 04 00 00 4E 8B 0C 08 4D 03 51 50 41 83 C8 FF 48 0F BF C2 66 FF C2 "
                  "48 8D 0C 40 48 89 2C 8B 44 89 44 8B 08 66 83 FA 03 72 E6"},
        // game simulation type: [TLS+0x48]+0x11 compared with 4 (distributed client).
        {0x506DE, "B9 48 00 00 00 41 BB 01 00 00 00 48 8B 04 31 80 78 11 04"},
    };
    constexpr MuzzleEdge edges[]{
        {0x4EFCE, 0x4E00C}, {0x4F115, 0x4FF4C}, {0x4F314, 0x4EF34}, {0x4F31E, 0x4EB54}, {0x626BB, 0x4F280}};
    if (!VerifyMuzzleNative<ReachGrabHookOps>(
            base, size, bindings, std::size(bindings), edges, std::size(edges), kReachGrabLog))
        return false;
    // Same TLS index in the probe and the evaluator; radius 0.75 at the search.
    float radius = 0;
    std::memcpy(&radius, reinterpret_cast<const void*>(base + 0xA8AE28), sizeof(radius));
    if (!ReachGrabRipOperand(base, 0x4E010, 3, 7, 0xC17B18) || !ReachGrabRipOperand(base, 0x4FF68, 3, 7, 0xC17B18) ||
        !ReachGrabRipOperand(base, 0x4EFD3, 4, 8, 0xA8AE28) || radius != 0.75f)
    {
        LOG("Reach grab pickup StockFallback: TLS index or search radius operand changed");
        return false;
    }
    // The session machine fields, as the game's own 0x58E70 reads them
    // (sim 2..5, index [[TLS+0x118]+0x78], mask [..+4]). Without this proof
    // the gate stays at sim 1 only.
    constexpr MuzzleBinding machines{0x58E70,
        "8B 0D ?? ?? ?? ?? 45 32 C0 65 48 8B 04 25 58 00 00 00 48 8B 14 C8 B8 48 00 00 00 48 8B 04 10 8A 48 11 "
        "80 E9 02 80 F9 03 77 29 B8 18 01 00 00 48 8B 04 10 8B 48 78 83 F9 FF 74 18 41 B9 01 00 00 00 45 0F B6 C0 "
        "41 8B D1 D3 E2 F7 D2 85 50 04"};
    feature.machinesProven =
        ReachGrabHookOps::Bound(base, size, machines) && ReachGrabRipOperand(base, 0x58E70, 2, 6, 0xC17B18);
    if (!feature.machinesProven)
        LOG("Reach grab pickup: session machine fields not proven; grab steers only at simulation type 1");
    feature.probeReturn = base + 0x4EFD3;
    feature.weaponReturn = base + 0x4F11A;
    feature.tlsIndex = base + 0xC17B18;
    feature.simType.store(-1, std::memory_order_relaxed);
    feature.otherMachines.store(-1, std::memory_order_relaxed);
    feature.gateOpen.store(false, std::memory_order_relaxed);
    feature.handAtMs.store(0, std::memory_order_relaxed);
    feature.pickTarget.store(grab_pickup::kNoTarget, std::memory_order_relaxed);
    feature.pickAtMs.store(0, std::memory_order_relaxed);
    feature.nearestWeapon = grab_pickup::kNoTarget;
    feature.nearestLog = {};
    const MuzzleHook hooks[]{
        {0x4E00C, reinterpret_cast<void*>(&ReachGrabProbeDetour), reinterpret_cast<void**>(&feature.probeOriginal)},
        {0x4FF4C, reinterpret_cast<void*>(&ReachGrabWeaponDetour), reinterpret_cast<void**>(&feature.weaponOriginal)},
        {0x4F280, reinterpret_cast<void*>(&ReachGrabPickDetour), reinterpret_cast<void**>(&feature.pickOriginal)}};
    if (!InstallMuzzleHooks<ReachGrabHookOps>(base, hooks, 3, feature.targets, kReachGrabLog))
        return false;
    feature.enabled.store(true, std::memory_order_release);
    LOG("Reach grab pickup Installed: search probe + weapon evaluator + final pick; option=%d",
        g_config.reach_grab_pickup ? 1 : 0);
    (void)InstallReachHolsterHooks(base, size);
    return true;
}

bool ReachGrabPickupReady()
{
    return g_reachGrab.enabled.load(std::memory_order_acquire) && g_reachGrab.gateOpen.load(std::memory_order_acquire);
}
// The weapon the game chose after the latest hand-steered search, or kNoTarget
// when that pick is stale (no game tick in 150 ms) or not a weapon.
uint32_t ReachGrabChosenWeapon(uint64_t now)
{
    const uint64_t at = g_reachGrab.pickAtMs.load(std::memory_order_acquire);
    const uint32_t pick = g_reachGrab.pickTarget.load(std::memory_order_relaxed);
    return at && now >= at && now - at <= 150 ? pick : grab_pickup::kNoTarget;
}
void ReachGrabSetSteer(bool steer)
{
    g_reachGrab.steer.store(steer, std::memory_order_release);
}

void ReportReachGrabPickup()
{
    auto& feature = g_reachGrab;
    if (!feature.targets[0] || !g_config.reach_grab_pickup)
        return;
    const uint64_t steered = feature.steered.exchange(0), filtered = feature.filtered.exchange(0),
                   inReach = feature.inReach.exchange(0), picked = feature.picked.exchange(0),
                   excluded = feature.excluded.exchange(0);
    if (steered || filtered || inReach || picked)
        LOG("Reach grab pickup: steered=%llu outOfReach=%llu inReach=%llu chosenWeapon=%llu sim=%d", steered, filtered,
            inReach, picked, feature.simType.load());
    if (excluded)
        LOG("Reach holster: holstered weapon kept out of selection x%llu", excluded);
}
