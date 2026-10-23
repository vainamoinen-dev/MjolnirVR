// Tests weapon_interaction_logic.h with weapon_reload_target.h and binding_cache_logic.h: the
// holster and body zones, physical reload, magazine seating and the binding cache.
// Header-only; built as halomccvr_weapon_interaction_tests. Pure logic: no game process.
#include "../src/common/weapon_interaction_logic.h"
#include "../src/common/weapon_reload_target.h"
#include "../src/common/binding_cache_logic.h"
#include <cstdio>
#include <limits>
#include <initializer_list>

using namespace weapon_interaction;
unsigned checks{}, failures{};
void Check(bool ok, const char* message)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

struct Rig
{
    State state;
    Settings c;
    Sample s;
    Vec pouch{}, holster{};
    Rig(GameTitle title = GameTitle::Halo3, bool left = false, int location = 0)
    {
        c.reload = c.holsters = true;
        c.leftHanded = left;
        c.holsterLocation = location;
        s.title = title;
        s.generation = 3;
        s.space = 7;
        s.ready = true;
        s.now = 1000;
        s.head = {0, 1.65f, 0};
        s.primary = {left ? -0.22f : 0.22f, 1.25f, -0.45f};
        s.support = {left ? 0.25f : -0.25f, 1.20f, -0.40f};
        Zones(s, c, pouch, holster);
        state.Update(s, c);
    }
    Output Step(unsigned delta = 20)
    {
        s.now += delta;
        return state.Update(s, c);
    }
    // The magazine hand's input: support trigger by default, grip in old style.
    void Hold(float value)
    {
        if (c.takeWithGrip)
            s.supportGrip = value;
        else
            s.supportTrigger = value;
    }
    bool Owns(const Output& o) const { return c.takeWithGrip ? o.consumeSupport : o.consumeSupportTrigger; }
    Output GrabMagazine()
    {
        s.support = pouch;
        Hold(1);
        return Step();
    }
    // Moves to the well (it seats on entry), then lets go. Reports the seat
    // frame's result together with the release frame's held pulse.
    Output InsertMagazine()
    {
        s.support = s.primary + Rotate(s.primaryRotation, {0, -0.06f, -0.04f});
        Output seat = Step(80);
        const Output second = Step(80);
        seat.reloadRequested |= second.reloadRequested;
        seat.primaryHaptic = std::fmax(seat.primaryHaptic, second.primaryHaptic);
        seat.supportHaptic = std::fmax(seat.supportHaptic, second.supportHaptic);
        Hold(0);
        Output released = Step();
        released.reloadRequested |= seat.reloadRequested;
        released.primaryHaptic = std::fmax(released.primaryHaptic, seat.primaryHaptic);
        released.supportHaptic = std::fmax(released.supportHaptic, seat.supportHaptic);
        return released;
    }
    Output GrabHolster()
    {
        s.primary = holster;
        s.primaryGrip = 1;
        return Step();
    }
    Output DrawHolster()
    {
        s.primary = {c.leftHanded ? -0.2f : 0.2f, 1.25f, -0.55f};
        Step(80);
        return Step(80);
    }
};

int main()
{
    {
        // Holster tick re-trigger dead zone: one tick per entry; edge jitter
        // inside the dead zone never re-ticks; past it (or 300 ms out) re-arms.
        HolsterTick t{};
        Check(StepHolsterTick(t, true, false, 1000), "holster entry ticks once");
        Check(!StepHolsterTick(t, true, false, 1010), "staying inside does not tick again");
        bool jitter = false;
        for (uint64_t now = 1020; now < 1300; now += 20)
            jitter |= StepHolsterTick(t, (now / 20) % 2 == 0, false, now);
        Check(!jitter, "edge jitter inside the dead zone never re-ticks");
        StepHolsterTick(t, false, true, 1300);
        Check(StepHolsterTick(t, true, false, 1310), "leaving past the dead zone re-arms the tick");
        StepHolsterTick(t, false, false, 2000);
        Check(!StepHolsterTick(t, true, false, 2100), "back within 300 ms (inside the dead zone) stays quiet");
        StepHolsterTick(t, false, false, 2200);
        StepHolsterTick(t, false, false, 2510);
        Check(StepHolsterTick(t, true, false, 2520), "300 ms outside re-arms even inside the dead zone");
        HolsterTick legacy{};
        StepHolsterTick(legacy, true, true, 10);
        StepHolsterTick(legacy, false, true, 20);
        Check(StepHolsterTick(legacy, true, true, 30), "dead zone 0: any exit re-arms, as before");
    }
    for (int title = 1; title <= 6; ++title)
        for (bool left : {false, true})
            for (int location : {0, 1})
            {
                Rig rig(static_cast<GameTitle>(title), left);
                rig.c.pouchLocation = location;
                Check(
                    Zones(rig.s, rig.c, rig.pouch, rig.holster), "reserve location supports both hands and all titles");
                const Vec original = rig.pouch, originalHolster = rig.holster;
                Check((left ? original.x > 0 : original.x < 0) && (location ? original.z > 0 : original.z < 0),
                    "reserve starts at the support-side front hip or behind its shoulder");
                rig.c.pouchOffset = {.10f, .07f, -.08f};
                Check(Zones(rig.s, rig.c, rig.pouch, rig.holster) && Near(originalHolster, rig.holster, .0001f),
                    "reserve offsets never move the separate weapon holster");
                Check(Near(rig.pouch, original + Vec{left ? -.10f : .10f, .07f, .08f}, .0001f),
                    "reserve XYZ offsets mirror side only, preserving height and forward convention");
                rig.Step();
                rig.GrabMagazine();
                Check(rig.Step().holdingMagazine, "adjusted reserve location can issue an ordinary magazine grab");
                rig.c.pouchOffset.x += .02f;
                const auto changed = rig.Step();
                Check(!changed.holdingMagazine && !changed.reloadRequested && rig.Owns(changed),
                    "moving the reserve cancels an existing grab and drains its held magazine input");
                rig.c.pouchOffset = {
                    std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), -999.f};
                Check(Zones(rig.s, rig.c, rig.pouch, rig.holster) && Finite(rig.pouch),
                    "invalid reserve coordinates never escape finite bounded geometry");
            }
    for (const auto& model : weapon_model::kModels)
        if (model.vertexCount)
            for (bool left : {false, true})
            {
                Rig r(model.title, left);
                r.s.weaponGraph = model.identity;
                r.c.insertRadius = .08f;
                r.Step();
                r.GrabMagazine();
                contact_melee::TrackingToWorld transform{}, controller{};
                transform.unitsPerMetre = .328084f;
                transform.origin = {123, 45, 67};
                transform.axis[0] = {0, 1, 0};
                transform.axis[1] = {0, 0, 1};
                transform.axis[2] = {1, 0, 0};
                const float q[]{0, 0, 0, 1}, p[]{r.s.primary.x, r.s.primary.y, r.s.primary.z};
                Check(controller.SetPose(q, p), "receiver fixture has valid primary controller");
                const auto native = transform.World({p[0] + .30f, p[1] - .04f, p[2] - .22f});
                ReloadTarget target{};
                ReloadTargets targets;
                Check(BuildReloadTarget(model.title, r.s.generation, model.identity, r.s.space, r.s.now, left, native,
                          transform, controller, target) &&
                          targets.Publish(target),
                    "each authored reload model admits a finite native receiver transform");
                Check(targets.Read(r.s, left) && Near(r.s.receiver, {p[0] + .30f, p[1] - .04f, p[2] - .22f}, .0001f),
                    "native rotated/scaled world coordinates reconstruct the actual XR receiver");
                const Vec first = r.s.receiver;
                r.s.primary = r.s.primary + Vec{.05f, .03f, -.02f};
                r.s.primaryRotation = {0, .70710678f, 0, .70710678f};
                Check(targets.Read(r.s, left) && !Near(first, r.s.receiver, .10f),
                    "receiver follows new controller translation and rotation between palette samples");
                r.s.supportRotation = {0, 0, .70710678f, .70710678f};
                r.s.support = r.s.receiver - Rotate(r.s.supportRotation, {0, -.015f, -.035f});
                const auto a = r.Step(80), b = r.Step(80);
                Check((a.reloadRequested && a.supportHaptic > .20f) != (b.reloadRequested && b.supportHaptic > .20f),
                    "bringing the visible magazine centre into its authored receiver seats once with feedback");
                r.Hold(0);
                r.Step();
                for (int invalid = 0; invalid < 6; ++invalid)
                {
                    auto bad = r.s;
                    bad.now = target.at;
                    switch (invalid)
                    {
                    case 0:
                        ++bad.generation;
                        break;
                    case 1:
                        ++bad.space;
                        break;
                    case 2:
                        ++bad.weaponGraph;
                        break;
                    case 3:
                        bad.now = target.at + 101;
                        break;
                    case 4:
                        bad.now = target.at - 1;
                        break;
                    case 5:
                        bad.primaryRotation.w = 2;
                        break;
                    }
                    Check(!targets.Read(bad, left) && !bad.receiverValid,
                        "stale or foreign receiver cannot leak into another gun or tracking epoch");
                }
                auto wrongHand = r.s;
                wrongHand.now = target.at;
                Check(!targets.Read(wrongHand, !left), "handedness change invalidates previous receiver");
                Rig miss(model.title, left);
                miss.s.weaponGraph = model.identity;
                miss.c.insertRadius = .08f;
                miss.Step();
                miss.GrabMagazine();
                miss.s.receiverValid = true;
                miss.s.receiver = miss.s.primary + Vec{.4f, 0, 0};
                const auto dropped = miss.InsertMagazine();
                Check(!dropped.reloadRequested && !dropped.primaryHaptic && !dropped.supportHaptic,
                    "old generic grip location cannot insert a magazine when a different authored receiver is available");
            }
    for (int title = 1; title <= 6; ++title)
        for (bool left : {false, true})
            for (int location : {0, 1})
                for (bool slide : {false, true})
                    for (bool click : {false, true})
                    {
                        Rig r(static_cast<GameTitle>(title), left, location);
                        r.c.holsterSlide = slide;
                        r.c.holsterClick = click;
                        r.Step();
                        auto o = r.GrabHolster();
                        Check(o.swapRequested == click && o.grabbedHolster == (slide || click),
                            "holster checkboxes select click, draw, both or neither in all titles and hands");
                        Check(r.DrawHolster().swapRequested == (slide && !click),
                            "both modes cannot produce a second swap on draw");
                        for (int n = 0; n < 30; ++n)
                            Check(!r.Step().swapRequested, "held holster click cannot repeat after cooldown");
                        r.s.primaryGrip = 0;
                        r.Step();
                        Check(r.GrabHolster().swapRequested == click, "new released click rearms at holster");
                    }
    for (float radius : {0.08f, 0.20f, 0.40f})
    {
        Rig r;
        r.c.holsterClick = true;
        r.c.holsterRadius = radius;
        r.c.zoneRadius = 0.08f;
        r.Step();
        r.s.primary = r.holster + Vec{radius - 0.001f, 0, 0};
        r.s.primaryGrip = 1;
        Check(r.Step().swapRequested, "holster accepts just inside its independent radius");
        Rig outside;
        outside.c.holsterClick = true;
        outside.c.holsterRadius = radius;
        outside.Step();
        outside.s.primary = outside.holster + Vec{radius + 0.001f, 0, 0};
        outside.s.primaryGrip = 1;
        Check(!outside.Step().swapRequested, "holster rejects just outside radius");
        outside.s.primary = outside.holster;
        Check(!outside.Step().swapRequested, "moving already-held grip into click zone does not switch");
        Rig pouch;
        pouch.c.zoneRadius = radius;
        pouch.c.holsterRadius = 0.08f;
        pouch.s.support = pouch.pouch + Vec{radius - 0.001f, 0, 0};
        pouch.Hold(1);
        Check(pouch.Step().pickedMagazine, "pouch radius independent of holster radius");
    }
    for (float radius : {0.06f, 0.18f, 0.30f})
        for (bool inside : {false, true})
        {
            Rig r;
            r.c.insertRadius = radius;
            r.GrabMagazine();
            r.s.support = r.s.primary + Vec{radius + (inside ? -0.001f : 0.001f), -0.06f, -0.04f};
            const auto a = r.Step(80), b = r.Step(80);
            Check((a.reloadRequested || b.reloadRequested) == inside, "insertion uses the selected independent radius");
        }
    {
        Rig r;
        r.GrabHolster();
        r.c.holsterClick = true;
        Check(!r.Step().swapRequested && !r.DrawHolster().swapRequested, "changing holster mode cancels held draw");
        Rig h;
        h.c.holsterClick = true;
        h.Step();
        h.s.otherAction = true;
        Check(!h.GrabHolster().swapRequested, "click holster cannot chord with a trigger or button");
        Rig shortDraw;
        shortDraw.c.holsterRadius = 0.08f;
        shortDraw.c.drawDistance = 0.10f;
        shortDraw.GrabHolster();
        shortDraw.s.primary = shortDraw.holster + Vec{0, 0, -0.23f};
        Check(shortDraw.Step(160).swapRequested, "small holster admits deliberately shortened draw");
        Rig longDraw;
        longDraw.c.drawDistance = 0.50f;
        longDraw.GrabHolster();
        longDraw.s.primary = longDraw.holster + Vec{0, 0, -0.40f};
        Check(!longDraw.Step(160).swapRequested, "long draw threshold prevents premature switching");
    }
    // A single out-and-back is sufficient with either grip state, on any axis.
    auto leg = [](Rig& r, Vec offset, unsigned dt = 11u)
    {
        const Vec start = r.s.primary;
        unsigned requests = 0;
        for (int n = 1; n <= 12; ++n)
        {
            r.s.primary = start + offset * (n / 12.0f);
            const auto o = r.Step(dt);
            requests += o.reloadRequested;
            Check(!o.consumePrimary && !o.consumeSupport && !o.releaseTwoHand,
                "shake never owns grips or changes support aim");
            if (o.reloadRequested)
                Check(o.buttons == r.c.reloadButton && o.pulseUntil == r.s.now + 120,
                    "single shake uses configured native reload pulse");
        }
        return requests;
    };
    auto needle = [](Rig& r)
    {
        r.c.needleShake = true;
        r.s.weaponGraph = 0x55EA2D6F6C10C375ull;
        r.Step();
        r.Step();
    };
    for (bool left : {false, true})
        for (unsigned dt : {8u, 11u, 14u})
            for (float grip : {0.0f, 1.0f})
                for (Vec axis : {Vec{1, 0, 0}, Vec{0, 1, 0}, Vec{0, 0, 1}, Vec{-.57735f, .57735f, -.57735f}})
                {
                    Rig r(GameTitle::HaloCE, left);
                    r.s.primaryGrip = grip;
                    needle(r);
                    Check(leg(r, axis * .14f, dt) == 0, "one-way movement alone cannot reload");
                    Check(leg(r, axis * -.14f, dt) == 1,
                        "one rapid out-and-back reloads without grip in every direction");
                    for (int cycle = 0; cycle < 4; ++cycle)
                    {
                        Check(leg(r, axis * .14f, dt) == 0 && leg(r, axis * -.14f, dt) == 0,
                            "continued shaking produces no repeated reloads before settling");
                    }
                    for (int i = 0; i < 30; ++i)
                        Check(!r.Step().reloadRequested, "settling never requests reload");
                    Check(leg(r, axis * .14f, dt) == 0 && leg(r, axis * -.14f, dt) == 1,
                        "next single shake rearms after settling without releasing grip");
                }
    for (int reason = 0; reason < 16; ++reason)
    {
        Rig r(GameTitle::HaloCE);
        needle(r);
        leg(r, {0, .14f, 0});
        switch (reason)
        {
        case 0:
            r.c.needleShake = false;
            break;
        case 1:
            r.s.weaponGraph = 0;
            break;
        case 2:
            r.s.title = GameTitle::Halo3;
            break;
        case 3:
            r.s.otherAction = true;
            break;
        case 4:
            r.s.ready = false;
            break;
        case 5:
            r.s.dualWield = true;
            break;
        case 6:
            r.s.now += 250;
            break;
        case 7:
            ++r.s.space;
            break;
        case 8:
            ++r.s.generation;
            break;
        case 9:
            r.c.reload = false;
            break;
        case 10:
            r.c.reloadButton = 0;
            break;
        case 11:
            r.c.leftHanded = true;
            break;
        case 12:
            r.state.Cancel();
            break;
        case 13:
            r.s.primary.x = std::numeric_limits<float>::quiet_NaN();
            break;
        case 14:
            r.c.shakeTravel = .20f;
            break;
        case 15:
            r.s.primaryGrip = std::numeric_limits<float>::infinity();
            break;
        }
        r.Step();
        Check(leg(r, {0, -.14f, 0}) == 0, "invalidation discards previous outward stroke");
    }
    for (int motion = 0; motion < 6; ++motion)
    {
        Rig r(GameTitle::HaloCE);
        needle(r);
        for (int i = 0; i < 100; ++i)
        {
            if (motion == 0)
            {
                r.s.primary.y += .04f;
                r.s.head.y += .04f;
            } // body translation
            if (motion == 1)
                r.s.primary.y += .02f; // one-way sweep
            if (motion == 2)
                r.s.primary.y += (i % 2 ? .02f : -.02f); // jitter
            if (motion == 3)
                r.s.primary.y += (i % 2 ? .15f : -.15f); // tracking jumps
            if (motion == 4)
                r.s.support.y += (i % 2 ? .15f : -.15f); // wrong hand
            if (motion == 5)
                r.s.primary.y += (i % 40 < 20 ? .008f : -.008f); // slow out-and-back
            Check(!r.Step(motion == 3 ? 8 : 100).reloadRequested,
                "translation, sweep, jitter, tracking jumps, support hand and slow movement do not reload");
        }
    }
    for (float travel : {.06f, .10f, .20f})
        for (float scale : {.8f, 1.1f})
        {
            Rig r(GameTitle::HaloCE);
            r.c.shakeTravel = travel;
            needle(r);
            leg(r, {0, travel * scale, 0}, 8);
            Check(
                leg(r, {0, -travel * scale, 0}, 8) == (scale > 1 ? 1u : 0u), "stroke slider changes required distance");
        }
    for (int binding = 0; binding < 8; ++binding)
    {
        Rig r(GameTitle::HaloCE);
        r.c.reloadButton = Button(binding);
        needle(r);
        leg(r, {.14f, 0, 0});
        Check(leg(r, {-.14f, 0, 0}) == 1, "shake supports every native reload binding");
    }
    for (int title = 1; title <= 6; ++title)
    {
        Rig r(static_cast<GameTitle>(title));
        r.c.needleShake = true;
        r.s.weaponGraph = 123;
        r.Step();
        Check(leg(r, {0, .14f, 0}) == 0 && leg(r, {0, -.14f, 0}) == 0, "unknown model never shake-reloads");
        Check(NeedleWeapon(static_cast<GameTitle>(title), 0x55EA2D6F6C10C375ull) == (title == 5),
            "CE fingerprint cannot classify a different engine's weapon");
    }
    for (bool holster : {false, true})
    {
        Rig r(GameTitle::HaloCE);
        needle(r);
        if (holster)
            r.GrabHolster();
        else
            r.GrabMagazine();
        const Vec start = r.s.primary;
        for (int i = 1; i <= 24; ++i)
        {
            r.s.primary = start + Vec{0, .14f * (i <= 12 ? i : 24 - i) / 12, 0};
            Check(!r.Step(11).reloadRequested, "pouch and holster transactions take priority over shake");
        }
    }
    for (int title = 1; title <= 6; ++title)
        for (bool left : {false, true})
            for (int location : {0, 1})
                for (bool grip : {false, true})
                {
                    Rig r(static_cast<GameTitle>(title), left, location);
                    r.c.takeWithGrip = grip;
                    r.Step();
                    auto o = r.GrabMagazine();
                    Check(o.pickedMagazine && r.Owns(o) && (grip || !o.consumeSupport) && !o.consumePrimary &&
                              o.releaseTwoHand && o.supportHaptic > 0 && !o.buttons,
                        "all titles/hands/locations/inputs: pouch take owns only the magazine input, frees support aim, gives feedback");
                    o = r.InsertMagazine();
                    Check(o.reloadRequested && !o.swapRequested && o.buttons == 0x4000 && o.pulseUntil > r.s.now &&
                              o.pulseUntil <= r.s.now + 120,
                        "pouch-to-weapon release produces one reload request with finite lifetime");
                    Check(o.primaryHaptic > 0 && o.supportHaptic > .20f,
                        "insertion acknowledges both hands with a stronger support pulse than grabbing");
                    for (int n = 0; n < 5; ++n)
                    {
                        const auto follow = r.Step();
                        Check(!follow.reloadRequested && !follow.primaryHaptic && !follow.supportHaptic,
                            "completed insertion never repeats reload or haptic edges");
                    }
                    Check(!r.Step(20).buttons, "reload pulse ends at exact deadline");
                    r.Step(100);
                    r.Step(100);
                    r.Step(100);
                    r.Step(100);
                    o = r.GrabHolster();
                    Check(o.grabbedHolster && o.consumePrimary && !o.consumeSupport && o.releaseTwoHand && !o.buttons,
                        "holster acquisition owns only primary grip and does not switch early");
                    o = r.DrawHolster();
                    Check(o.swapRequested && o.buttons == 0x8000 && o.primaryHaptic > 0,
                        "drawing switches once using native input");
                    for (int n = 0; n < 35; ++n)
                        Check(!r.Step().swapRequested, "held grip outside holster cannot repeatedly cycle weapons");
                    r.s.primaryGrip = 0;
                    Check(r.Step().consumePrimary, "release frame still belongs to claimed holster press");
                    Check(!r.Step().consumePrimary, "holster ownership ends after release");
                }
    for (int binding = 0; binding < 8; ++binding)
    {
        Rig r;
        r.c.reloadButton = Button(binding);
        r.Step();
        r.GrabMagazine();
        Check(r.InsertMagazine().buttons == Button(binding), "all configured native reload transports preserved");
        Rig h;
        h.c.swapButton = Button(binding);
        h.Step();
        h.GrabHolster();
        Check(h.DrawHolster().buttons == Button(binding), "all configured weapon switch transports preserved");
    }
    Check(!Button(-1) && !Button(8) && !Button(1000), "invalid layouts never invent an input");
    for (bool reload : {false, true})
        for (bool holsters : {false, true})
        {
            Rig r;
            r.c.reload = reload;
            r.c.holsters = holsters;
            r.Step();
            Check(r.GrabMagazine().pickedMagazine == reload, "manual reload independently opt-in");
            Rig h;
            h.c.reload = reload;
            h.c.holsters = holsters;
            h.Step();
            Check(h.GrabHolster().grabbedHolster == holsters, "holsters independently opt-in");
        }
    {
        Rig r;
        r.Hold(1);
        r.Step();
        r.s.support = r.pouch;
        Check(!r.Step().pickedMagazine, "entering pouch with an already held input does not claim it");
        r.Hold(0);
        r.Step();
        Check(r.GrabMagazine().pickedMagazine, "fresh press after release can acquire pouch");
        r.Hold(0);
        auto o = r.Step(160);
        Check(!o.buttons && !o.reloadRequested && !o.primaryHaptic && !o.supportHaptic,
            "dropping magazine at pouch cannot reload or acknowledge insertion");
    }
    {
        Rig h;
        h.GrabHolster();
        h.s.primaryGrip = 0;
        Check(!h.Step(160).buttons, "letting go before draw cancels swap");
        Rig r;
        r.GrabMagazine();
        r.s.support = {0, 0, 0};
        r.Hold(0);
        const auto drop = r.Step(160);
        Check(!drop.buttons && !drop.primaryHaptic && !drop.supportHaptic,
            "release away from weapon discards gesture without native input or insertion haptics");
    }
    // Each unavailable state cancels both partial interactions and blocks the
    // held grip on recovery. These correspond to shipping adapter admission.
    for (int reason = 0; reason < 14; ++reason)
        for (bool holster : {false, true})
        {
            Rig r;
            if (holster)
                r.GrabHolster();
            else
                r.GrabMagazine();
            const Sample previous = r.s;
            const Settings settings = r.c;
            switch (reason)
            {
            case 0:
                r.s.ready = false;
                break; // focus, pause, menu, vehicle, death, tracking
            case 1:
                r.s.dualWield = true;
                break;
            case 2:
                ++r.s.generation;
                break;
            case 3:
                ++r.s.space;
                break;
            case 4:
                r.s.title = GameTitle::HaloCE;
                break;
            case 5:
                r.s.primary.x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 6:
                r.s.headRotation.w = 0;
                break;
            case 7:
                r.s.now += 250;
                break;
            case 8:
                r.c.leftHanded = true;
                break;
            case 9:
                r.c.reload = r.c.holsters = false;
                break;
            case 10:
                r.c.reloadButton = r.c.swapButton = Button(2);
                break;
            case 11:
                r.s.primaryGrip = std::numeric_limits<float>::infinity();
                break;
            case 12:
                r.s.primaryGrip = 2;
                break;
            case 13:
                r.s.supportGrip = -1;
                break;
            }
            auto o = r.Step();
            Check(!o.buttons && !o.reloadRequested && !o.swapRequested, "state invalidation cancels in-flight request");
            const auto now = r.s.now;
            r.s = previous;
            r.c = settings;
            r.s.now = now;
            r.Step();
            o = holster ? r.DrawHolster() : r.InsertMagazine();
            Check(!o.buttons && !o.reloadRequested && !o.swapRequested, "recovery cannot complete stale gesture");
        }
    {
        Rig r;
        r.GrabMagazine();
        r.s.otherAction = true;
        Check(r.Step().holdingMagazine, "incidental trigger/button input cannot detach held magazine");
        r.s.otherAction = false;
        Check(r.InsertMagazine().reloadRequested, "held magazine still inserts after unrelated input releases");
        Rig h;
        h.GrabHolster();
        h.s.otherAction = true;
        Check(!h.DrawHolster().buttons, "shooting/button chord cancels holster action");
    }
    {
        Rig r;
        r.GrabMagazine();
        for (int i = 0; i < 42; ++i)
            r.Step(100);
        Check(r.InsertMagazine().reloadRequested, "held magazine remains insertable beyond four seconds");
        Rig h;
        h.GrabHolster();
        for (int i = 0; i < 42; ++i)
            h.Step(100);
        Check(!h.DrawHolster().buttons, "stale holster grab cannot become a delayed switch");
    }
    // Rigid translation/yaw leave both body zones and the physical reload
    // gesture unchanged, including a rolled primary controller.
    for (float yaw : {-2.2f, -0.5f, 0.5f, 2.8f})
    {
        Rig r;
        const Quat q{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
        const Vec offset{3, 0.4f, -2};
        r.s.head = Rotate(q, r.s.head) + offset;
        r.s.primary = Rotate(q, r.s.primary) + offset;
        r.s.support = Rotate(q, r.s.support) + offset;
        r.s.headRotation = q;
        r.s.primaryRotation = q;
        Zones(r.s, r.c, r.pouch, r.holster);
        r.Step();
        r.GrabMagazine();
        Check(r.InsertMagazine().reloadRequested, "body gestures are tracking-space yaw/translation invariant");
    }
    for (int title = 1; title <= 6; ++title)
    {
        const auto t = static_cast<GameTitle>(title);
        Check(Fresh(1100, 1000, t, t, 3, 3, true), "fresh same-title/generation publication admitted");
        Check(!Fresh(1151, 1000, t, t, 3, 3, true) && !Fresh(999, 1000, t, t, 3, 3, true),
            "stale/future sample rejected");
        Check(!Fresh(1100, 1000, t, t, 3, 4, true) && !Fresh(1100, 1000, t, GameTitle::None, 3, 3, true),
            "foreign title/generation rejected");
        Check(!Fresh(1100, 1000, t, t, 3, 3, false) && !Fresh(1100, 0, t, t, 3, 3, true),
            "unavailable or absent publication rejected");
    }
    for (float pitch : {-1.57079633f, 1.57079633f})
    {
        Rig r;
        r.s.headRotation = {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)};
        Check(
            Zones(r.s, r.c, r.pouch, r.holster), "looking vertically at pouch retains yaw from horizontal head right");
        r.GrabMagazine();
        Check(r.InsertMagazine().reloadRequested, "looking straight down/up does not lose reload zone");
    }
    for (bool grip : {false, true})
    {
        Rig r;
        r.c.takeWithGrip = grip;
        r.Step();
        r.GrabMagazine();
        r.state.Cancel();
        auto o = r.Step();
        Check(r.Owns(o) && !o.buttons, "lost XR sync cancels command but retains claimed input ownership");
        r.c.reload = r.c.holsters = false;
        o = r.Step();
        Check(r.Owns(o) && !o.buttons, "disabling options does not turn a held magazine input into a game press");
        r.Hold(0);
        r.Step();
        Check(!r.Owns(r.Step()), "cancelled press relinquishes ownership only after release");
    }
    for (bool grip : {false, true})
    {
        Rig r;
        r.c.takeWithGrip = grip;
        r.Step();
        r.s.ready = false;
        r.Hold(0);
        r.Step();
        r.s.ready = true;
        Check(!r.GrabMagazine().pickedMagazine,
            "held input on restored availability cannot use an earlier unavailable release");
        r.Hold(0);
        r.Step();
        Check(r.GrabMagazine().pickedMagazine, "release after restored availability rearms normally");
        r.Hold(std::numeric_limits<float>::quiet_NaN());
        r.s.ready = false;
        Check(r.Owns(r.Step()), "inactive XR action cannot fabricate a release");
        r.s.ready = true;
        r.Hold(1);
        Check(r.Owns(r.Step()) && !r.InsertMagazine().buttons,
            "inactive-action recovery cannot resurrect magazine transaction");
    }
    {
        Rig r;
        r.Step();
        Check(BlocksSupportGrab(r.GrabHolster()), "active holster draw owns two-hand release");
        Check(r.DrawHolster().swapRequested, "holster draw completes normally");
        const auto held = r.Step();
        Check(held.consumePrimary && !BlocksSupportGrab(held),
            "completed draw can retain primary grip without blocking support hand");
        r.s.primaryGrip = 0;
        r.Step();
        r.Step(200);
        r.Step(200);
        r.Step(200); // allow the completed draw's cooldown
        Check(BlocksSupportGrab(r.GrabMagazine()), "held reload magazine still blocks support acquisition");
    }
    // Holster feedback: one light tick when the weapon hand enters the zone,
    // none while it stays, a new one after leaving and coming back, none when
    // holsters are off, and a tap (grip squeeze in the zone) still swaps.
    for (bool left : {false, true})
        for (int location : {0, 1})
        {
            Rig r(GameTitle::HaloReach, left, location);
            r.c.holsterClick = true;
            r.Step();
            const Vec away = r.s.primary;
            r.s.primary = r.holster;
            const auto enter = r.Step();
            Check(enter.primaryHaptic > 0 && enter.primaryHaptic < 0.2f && !enter.swapRequested,
                "entering the holster zone gives one light tick and no swap");
            Check(r.Step().primaryHaptic == 0, "staying in the zone does not tick again");
            r.s.primary = away;
            r.Step();
            r.s.primary = r.holster;
            Check(r.Step().primaryHaptic > 0, "leaving and re-entering ticks again");
            r.s.primaryGrip = 1;
            Check(r.Step().swapRequested, "a grip tap in the zone swaps");
            Rig off(GameTitle::HaloReach, left, location);
            off.c.holsters = false;
            off.c.reload = true;
            off.Step();
            off.s.primary = off.holster;
            Check(off.Step().primaryHaptic == 0, "no holster tick when holsters are off");
        }
    // Snap-in reload v2 (support trigger): takes at the game's 0.15 press point,
    // the trigger is the game's own input away from the pouch, the magazine seats
    // on entering the well while still held, the trigger stays hidden until let
    // go, a release elsewhere drops it silently, and the pouch rests for 1 s.
    {
        Settings defaults{};
        Check(!defaults.takeWithGrip && defaults.insertRadius == 0.13f,
            "trigger take and a 0.13 m well are the defaults");

        Rig light;
        light.Step();
        light.s.support = light.pouch;
        light.s.supportTrigger = 0.2f;
        const auto lightTake = light.Step();
        Check(lightTake.pickedMagazine && lightTake.consumeSupportTrigger,
            "a light pull past the game's press point takes the magazine, so no grenade leaks");

        Rig away;
        away.Step();
        away.s.supportTrigger = 1;
        const auto grenade = away.Step();
        Check(!grenade.pickedMagazine && !grenade.consumeSupportTrigger,
            "away from the pouch the trigger stays the game's grenade");
        Rig draw;
        draw.Step();
        draw.GrabHolster();
        draw.s.supportTrigger = 1;
        Check(!draw.DrawHolster().buttons, "a grenade pull away from the pouch still cancels a holster draw");

        Rig gripHeld;
        gripHeld.Step();
        gripHeld.GrabMagazine();
        gripHeld.s.supportGrip = 1;
        const auto holding = gripHeld.Step();
        Check(holding.holdingMagazine && holding.consumeSupport && holding.consumeSupportTrigger,
            "while a trigger-held magazine is in hand the support grip is hidden too");
        gripHeld.s.support = gripHeld.s.primary + Rotate(gripHeld.s.primaryRotation, {0, -0.06f, -0.04f});
        gripHeld.Step(80);
        gripHeld.Step(80);
        gripHeld.Hold(0);
        gripHeld.Step();
        Check(gripHeld.Step().consumeSupport, "after the seat a still-held support grip stays hidden");
        gripHeld.s.supportGrip = 0;
        gripHeld.Step();
        Check(!gripHeld.Step().consumeSupport, "the grip is returned once let go");

        Rig shallow;
        shallow.Step();
        shallow.s.support = shallow.pouch;
        shallow.s.supportTrigger = 0.12f;
        const auto below = shallow.Step();
        Check(below.consumeSupportTrigger && !below.pickedMagazine,
            "at the pouch even a pull below the take point is hidden from the game");
        Rig unbound;
        unbound.c.reloadButton = 0;
        unbound.Step();
        unbound.Step();
        unbound.s.support = unbound.pouch;
        unbound.s.supportTrigger = 1;
        const auto noReload = unbound.Step();
        Check(!noReload.consumeSupportTrigger && !noReload.pickedMagazine,
            "with no Reload binding the pouch lets the grenade trigger through");

        Rig r;
        r.Step();
        r.GrabMagazine();
        r.s.support = r.s.primary + Rotate(r.s.primaryRotation, {0, -0.06f, -0.04f});
        const auto early = r.Step(80);
        const auto seated = r.Step(80);
        Check(!early.reloadRequested && seated.reloadRequested && seated.supportHaptic >= 0.45f &&
                  seated.primaryHaptic > 0,
            "the magazine seats on entering the well, while still held, with a strong tick");
        Check(r.Step().consumeSupportTrigger && !r.Step().reloadRequested,
            "after seating the held trigger stays hidden and cannot reload again");
        r.Hold(0);
        r.Step();
        r.s.support = r.pouch;
        r.Hold(1);
        const auto resting = r.Step();
        Check(!resting.pickedMagazine && !resting.holdingMagazine, "the pouch rests for a second after a reload");
        Check(resting.consumeSupportTrigger && r.Step().consumeSupportTrigger,
            "a pull at the resting pouch is swallowed until release, so no grenade leaks");
        r.Hold(0);
        r.Step(1000);
        r.Hold(1);
        Check(r.Step().pickedMagazine, "after the rest the pouch gives a new magazine");

        Rig drop;
        drop.Step();
        drop.GrabMagazine();
        drop.s.support = drop.s.support + Vec{0, 0.4f, 0};
        drop.Step(200);
        drop.Hold(0);
        const auto gone = drop.Step();
        Check(!gone.reloadRequested && !gone.holdingMagazine && !gone.supportHaptic && !gone.buttons,
            "a magazine let go outside the well vanishes quietly");
    }
    // Holster tick strength is a setting; zero turns the tick off.
    for (float strength : {0.0f, 0.30f})
    {
        Rig r(GameTitle::Halo3);
        r.c.holsterClick = true;
        r.c.zoneHaptic = strength;
        r.Step();
        r.s.primary = r.holster;
        const float tick = r.Step().primaryHaptic;
        Check(strength > 0 ? tick == strength : tick == 0, "holster tick follows its strength setting");
    }
    // Body yaw ("inventory lock"): glances inside the dead zone leave the body
    // put, a far turn drags it to the dead-zone edge, and it settles back under
    // the head over time unless a hand is working a zone.
    {
        const auto yawQ = [](float degrees)
        {
            const float h = degrees * 3.14159265f / 360.0f;
            return Quat{0, std::sin(h), 0, std::cos(h)};
        };
        const auto yawOf = [](Vec f) { return std::atan2(-f.x, -f.z) * 180.0f / 3.14159265f; };
        BodyYaw body;
        Vec f{};
        Check(body.Update(yawQ(0), 1000, false, f) && std::fabs(yawOf(f)) < 0.01f, "body yaw seeds from the head");
        Check(body.Update(yawQ(30), 1020, false, f) && std::fabs(yawOf(f)) < 1.0f,
            "a 30 degree glance barely moves the body");
        Check(body.Update(yawQ(90), 1040, false, f) && std::fabs(yawOf(f) - 49.9f) < 1.0f,
            "a 90 degree turn drags the body to the dead-zone edge");
        uint64_t t = 1040;
        for (int i = 0; i < 250; ++i)
            body.Update(yawQ(90), t += 20, false, f);
        Check(std::fabs(yawOf(f) - 90) < 2.0f, "the body settles under a held head turn");
        const float settled = yawOf(f);
        for (int i = 0; i < 250; ++i)
            body.Update(yawQ(120), t += 20, true, f);
        Check(std::fabs(yawOf(f) - settled) < 0.01f, "hold freezes the settle inside the dead zone");
        body.Update(yawQ(-170), t += 20, false, f);
        Check(std::fabs(yawOf(f) - 150) < 1.0f, "the dead zone takes the short way across +/-180 degrees");
        body.Update(yawQ(10), t += 500, false, f);
        Check(std::fabs(yawOf(f) - 10) < 0.01f, "a tracking gap reseeds body yaw from the head");
        Check(!body.Update(Quat{0, 0, 0, 0}, t += 20, false, f), "an invalid head pose is rejected");
    }
    // Through the gesture state: after a 60 degree head turn the body-anchored
    // holster is still where the hand left it; the head-anchored one has moved.
    for (bool anchor : {true, false})
    {
        Rig r(GameTitle::HaloReach);
        r.c.holsterClick = true;
        r.c.bodyAnchor = anchor;
        r.Step();
        const Vec original = r.holster;
        r.s.headRotation = {0, std::sin(30 * 3.14159265f / 180), 0, std::cos(30 * 3.14159265f / 180)};
        const auto turned = r.Step();
        Check(anchor == (Dot(turned.bodyForward, turned.bodyForward) > 0),
            "output reports the body basis only when anchored");
        r.s.primary = original;
        r.Step();
        r.s.primaryGrip = 1;
        Check(r.Step().swapRequested == anchor, "body-anchored holster survives a head turn; head-anchored one moves");
    }
    // The settle freezes only for a reach toward a zone, not for a hand that
    // rests near one; a new tracking space recentres; pausing with the hand
    // in the holster does not tick on resume.
    {
        const float h = 15 * 3.14159265f / 180; // 30 degree glance
        const Quat glance{0, std::sin(h), 0, std::cos(h)};
        const auto yawOf = [](Vec f) { return std::atan2(-f.x, -f.z) * 180.0f / 3.14159265f; };
        Rig rest(GameTitle::Halo3);
        rest.Step();
        rest.s.support = rest.pouch;
        rest.s.headRotation = glance;
        Output o{};
        for (int i = 0; i < 200; ++i)
            o = rest.Step();
        Check(yawOf(o.bodyForward) > 25, "a relaxed hand resting at the pouch does not pin body yaw");
        rest.s.space++;
        o = rest.Step();
        Check(std::fabs(yawOf(o.bodyForward) - 30) < 0.01f, "a new tracking space recentres body yaw under the head");

        for (bool reaching : {true, false})
        {
            Rig r(GameTitle::Halo3);
            r.Step();
            r.s.headRotation = glance;
            const Vec target = r.holster, start = target + (r.s.primary - target) * 0.6f;
            r.s.primary = start;
            for (int i = 0; i < 25; ++i)
                r.Step(); // let the jump's own hold lapse
            const float before = yawOf(r.Step().bodyForward);
            for (int i = 1; i <= 10; ++i)
            {
                if (reaching)
                    r.s.primary = start + (target - start) * (i / 10.0f);
                o = r.Step();
            }
            Check((std::fabs(yawOf(o.bodyForward) - before) < 1.0f) == reaching,
                "a reach toward the holster freezes the settle");
        }

        Rig pause(GameTitle::Halo3);
        pause.Step();
        pause.s.ready = false;
        pause.s.primary = pause.holster;
        pause.Step();
        pause.s.ready = true;
        pause.Step();
        Check(pause.Step().primaryHaptic == 0, "resuming with the hand already in the holster does not tick");
    }
    // Swap, then the stowed weapon's class moves the holster (shoulder -> hip).
    // The slot change resets the gesture like any settings change; the held
    // grip stays owned until release, and a fresh grip at the new zone swaps.
    {
        Rig r(GameTitle::HaloReach);
        r.c.holsterClick = true;
        r.s.reserveSlot = HolsterSlot::Shoulder;
        r.Step();
        Vec pouch{}, shoulder{}, hip{};
        Zones(r.s, r.c, pouch, shoulder);
        r.s.reserveSlot = HolsterSlot::Hip;
        Zones(r.s, r.c, pouch, hip);
        r.s.reserveSlot = HolsterSlot::Shoulder;
        r.s.primary = shoulder;
        r.Step();
        r.s.primaryGrip = 1;
        Check(r.Step().swapRequested, "tap at the shoulder swaps");
        r.s.reserveSlot = HolsterSlot::Hip; // the gun just stowed is one-handed
        const auto moved = r.Step();
        Check(!moved.swapRequested && moved.consumePrimary, "slot change keeps the held grip owned and does not swap");
        r.s.primary = hip;
        Check(!r.Step().swapRequested, "still-held grip at the new zone does not swap");
        r.s.primaryGrip = 0;
        r.Step();
        r.Step(500); // release; let the swap cooldown lapse
        r.s.primaryGrip = 1;
        Check(r.Step().swapRequested, "a fresh grip at the hip swaps right after the slot change");
    }
    // The reserve-slot cache resolves once per stowed weapon.
    {
        ReserveSlotCache cache;
        unsigned lookups = 0;
        const auto hipLookup = [&]
        {
            ++lookups;
            return HolsterSlot::Hip;
        };
        Check(cache.Get(GameTitle::HaloReach, 0x10, hipLookup) == HolsterSlot::Hip &&
                  cache.Get(GameTitle::HaloReach, 0x10, hipLookup) == HolsterSlot::Hip && lookups == 1,
            "same reserve weapon resolves once");
        Check(cache.Get(GameTitle::HaloReach, 0x11, hipLookup) == HolsterSlot::Hip && lookups == 2,
            "a new reserve weapon re-resolves");
        Check(cache.Get(GameTitle::HaloReach, 0, hipLookup) == HolsterSlot::Unknown && lookups == 2,
            "an unknown reserve (0) is Unknown without a lookup");
        Check(cache.Get(GameTitle::Halo3, 0x11, hipLookup) == HolsterSlot::Hip && lookups == 3,
            "a title change re-resolves");
    }
    // Per-weapon slot seam: a known reserve slot overrides the configured location.
    {
        Rig r(GameTitle::HaloReach, false, 0);
        Vec pouch{}, shoulder{}, hip{};
        Zones(r.s, r.c, pouch, shoulder);
        r.s.reserveSlot = HolsterSlot::Hip;
        Zones(r.s, r.c, pouch, hip);
        Settings hipConfig = r.c;
        hipConfig.holsterLocation = 1;
        r.s.reserveSlot = HolsterSlot::Unknown;
        Vec configured{};
        Zones(r.s, hipConfig, pouch, configured);
        Check(!Near(shoulder, hip, 0.05f) && Near(hip, configured, 0.0001f), "reserve slot Hip uses the hip holster");
        r.s.reserveSlot = HolsterSlot::Shoulder;
        Zones(r.s, hipConfig, pouch, configured);
        Check(Near(configured, shoulder, 0.0001f), "reserve slot Shoulder overrides a hip configuration");
        using weapon_profile::SizeClass;
        Check(SlotFor(SizeClass::OneHanded) == HolsterSlot::Hip && SlotFor(SizeClass::Long) == HolsterSlot::Shoulder &&
                  SlotFor(SizeClass::Unknown) == HolsterSlot::Unknown &&
                  SlotFor(SizeClass::Melee) == HolsterSlot::Unknown &&
                  SlotFor(SizeClass::Objective) == HolsterSlot::Unknown,
            "pistols map to the hip, long guns to the shoulder");
        Check(
            SlotFor(weapon_profile::WeaponClassFor(GameTitle::HaloReach, 0x0000000015150C0Cull)) == HolsterSlot::Hip &&
                SlotFor(weapon_profile::WeaponClassFor(GameTitle::HaloReach, 0x00000000150D1012ull)) ==
                    HolsterSlot::Shoulder &&
                SlotFor(weapon_profile::WeaponClassFor(GameTitle::Halo3, 0x1234)) == HolsterSlot::Unknown,
            "profile table classes feed the slot seam; unknown weapons keep the configured location");
    }
    // One cue table: every cue has a sane strength, the defaults keep the tuned
    // feel, gestures use the table, and the master scales without exceeding 1.
    {
        using feedback::Cue;
        using feedback::Strength;
        for (const auto& spec : feedback::kCues)
            Check(spec.strength > 0 && spec.strength <= 1 && spec.name && *spec.name,
                "every cue has a strength in (0,1] and a name");
        Check(Strength(Cue::HolsterEnter) == 0.12f && Strength(Cue::WeaponSwap) == 0.45f &&
                  Strength(Cue::MagTake) == 0.20f && Strength(Cue::MagSeatSupport) == 0.45f &&
                  Strength(Cue::MeleeHit) == 0.65f && Strength(Cue::WorldContact) == 0.18f &&
                  Strength(Cue::FlashlightToggle) == 0.15f && Strength(Cue::TwoHandRelease) == 0.10f,
            "cue defaults keep the previously tuned strengths");
        Rig r;
        r.Step();
        Check(r.GrabMagazine().supportHaptic == Strength(Cue::MagTake), "magazine take uses the table");
        Rig h;
        h.c.holsterClick = true;
        h.Step();
        Check(h.GrabHolster().primaryHaptic == Strength(Cue::WeaponSwap), "holster swap uses the table");
        Check(feedback::Scaled(0.45f, 1.0f) == 0.45f && feedback::Scaled(0.45f, 0.0f) == 0.0f &&
                  feedback::Scaled(0.65f, 2.0f) == 1.0f && feedback::Scaled(0.2f, 5.0f) == 0.4f &&
                  feedback::Scaled(0.2f, std::numeric_limits<float>::quiet_NaN()) == 0.2f,
            "the master scales cues, clamps at full strength and ignores bad values");
    }
    // Halo CE: the first holster tap after gameplay starts happened while the
    // game's switch binding was still unknown (reader drop-out), and was lost.
    // The shared reader now caches the last good layout; on a first-ever
    // load (no cache) the tap is held 0.5 s and sent once the binding arrives.
    {
        binding_cache::Cache cache;
        vr_mapping::Transports t{}, out{};
        Check(!cache.Read(GameTitle::HaloCE, 1, out), "no cache on a first-ever load");
        t[vr_mapping::SwitchWeapon] = 0x8000;
        cache.Store(GameTitle::HaloCE, 1, t);
        Check(cache.Read(GameTitle::HaloCE, 1, out) && out[vr_mapping::SwitchWeapon] == 0x8000,
            "a reader drop-out reads the last good layout (map loads do not matter)");
        Check(!cache.Read(GameTitle::Halo3, 1, out), "another title never borrows it");
        {
            binding_cache::Cache c2 = cache;
            c2.ClearIfOtherTitle(GameTitle::Halo3);
            Check(!c2.Read(GameTitle::HaloCE, 1, out), "a title change clears the cache");
        }
        Check(!cache.Read(GameTitle::HaloCE, 2, out), "a controller profile change invalidates it");
        t[vr_mapping::SwitchWeapon] = 0x2000;
        cache.Store(GameTitle::HaloCE, 1, t);
        Check(cache.Read(GameTitle::HaloCE, 1, out) && out[vr_mapping::SwitchWeapon] == 0x2000,
            "a fresh read replaces it");
    }
    {
        Rig r(GameTitle::HaloCE);
        r.c.holsterClick = true;
        r.c.swapButton = 0;
        r.Step();
        const auto tap = r.GrabHolster();
        Check(tap.grabbedHolster && !tap.swapRequested && !tap.buttons,
            "a tap before the binding is known is taken, not lost");
        r.Step();
        r.c.swapButton = 0x8000;
        bool sent = false;
        uint32_t buttons = 0;
        for (int i = 0; i < 10; ++i)
        {
            const auto o = r.Step();
            sent |= o.swapRequested;
            buttons |= o.buttons;
        }
        Check(sent && buttons == 0x8000, "the pending swap is sent as soon as the binding arrives");
        bool twice = false;
        for (int i = 0; i < 20; ++i)
            twice |= r.Step().swapRequested;
        Check(!twice, "and only once");
        Rig late(GameTitle::HaloCE);
        late.c.holsterClick = true;
        late.c.swapButton = 0;
        late.Step();
        late.GrabHolster();
        for (int i = 0; i < 4; ++i)
            late.Step(200); // 0.8 s without a binding
        late.c.swapButton = 0x8000;
        bool stale = false;
        for (int i = 0; i < 10; ++i)
            stale |= late.Step().swapRequested;
        Check(!stale, "a pending swap older than 0.5 s is dropped");
        Rig moved(GameTitle::HaloCE);
        moved.c.holsterClick = true;
        moved.c.swapButton = 0;
        moved.Step();
        moved.GrabHolster();
        ++moved.s.generation;
        moved.c.swapButton = 0x8000;
        bool leaked = false;
        for (int i = 0; i < 10; ++i)
            leaked |= moved.Step().swapRequested;
        Check(!leaked, "a new map or title drops the pending swap");
    }
    std::printf("Weapon interactions: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
