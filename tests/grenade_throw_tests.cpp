// Tests for the off-hand body slots: chest grenade throw, grenade-type switch and equipment slot
// (src/common/grenade_throw_logic.h). Built as halomccvr_grenade_throw_tests and run by ctest.
// Header-only logic: no game process, no headset.
#include "../src/common/grenade_throw_logic.h"
#include <cstdio>
#include <limits>

using namespace grenade_throw;
namespace
{
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

    constexpr uint32_t kThrow = 1u << 16, kCycle = 0x100;

    struct Rig
    {
        State state;
        Settings c;
        Input in;
        Vec slot{};
        Rig()
        {
            c.enabled = true;
            c.throwButton = kThrow;
            c.cycleButton = kCycle;
            in.now = 1000;
            in.ready = true;
            in.head = {0, 1.65f, 0};
            in.bodyForward = {0, 0, -1};
            in.support = {-0.25f, 1.20f, -0.40f};
            in.pouchActive = true;
            in.pouch = {-0.25f, 1.15f, -0.06f};
            in.pouchRadius = 0.20f;
            SlotPosition(in, c, slot);
            state.Update(in, c);
        }
        Output Step(unsigned dt = 11)
        {
            in.now += dt;
            return state.Update(in, c);
        }
        Output Take()
        {
            in.support = slot;
            Step();
            in.trigger = 1;
            return Step();
        }
        // Moves the hand by `delta` over `frames` frames of 11 ms, then lets go.
        Output Release(Vec delta, int frames)
        {
            const Vec start = in.support;
            for (int i = 1; i <= frames; ++i)
            {
                in.support = start + delta * (float(i) / frames);
                Step();
            }
            in.trigger = 0;
            return Step(1);
        }
    };
} // namespace

int main()
{
    {
        Rig r;
        Check(!Near(r.slot, r.in.pouch, 0.12f + 0.20f), "default grenade slot keeps clear of the magazine pouch");
        const Output take = r.Take();
        Check(take.took && take.holding && take.consumeTrigger && take.haptic > 0 && !take.buttons,
            "a trigger pull in the slot takes a grenade, hides the trigger and ticks");
        // 0.5 m forward in 5 frames (55 ms) is about 9 m/s.
        const Output thrown = r.Release({0, 0.1f, -0.5f}, 5);
        Check(thrown.thrown && thrown.buttons == kThrow && thrown.haptic > take.haptic,
            "a fast release throws with Halo's own grenade action and a stronger tick");
        Check(r.Step().buttons == kThrow && r.Step(150).buttons == 0, "the throw is one short press");
    }
    {
        Rig r;
        r.Take();
        // 0.3 m in 30 frames (330 ms) is about 0.9 m/s.
        const Output back = r.Release({0, 0.1f, -0.28f}, 30);
        Check(back.returned && !back.thrown && !back.buttons, "a slow release puts the grenade back with no throw");
    }
    {
        Rig r;
        r.Take();
        r.in.trigger = 0;
        const Output tap = r.Step(80);
        Check(tap.returned && !tap.cycled && !tap.buttons, "a quick tap at the chest just puts the grenade back");
    }
    {
        Rig r;
        r.Take();
        for (int i = 0; i < 40; ++i)
            r.Step();
        r.in.trigger = 0;
        const Output held = r.Step();
        Check(held.returned && !held.buttons, "a long hold let go in the slot just puts it back");
    }
    {
        Rig r;
        r.c.throwSpeed = 4.0f;
        r.Take();
        const Output slowForSetting = r.Release({0, 0, -0.3f}, 10); // about 2.7 m/s
        Check(!slowForSetting.thrown && slowForSetting.returned, "the throw speed setting is honoured");
    }
    {
        Rig r;
        r.in.support = r.slot;
        r.Step();
        r.in.trigger = 1;
        r.in.reloadOwnsTrigger = true;
        const Output o = r.Step();
        Check(!o.took && !o.consumeTrigger, "the slot never takes a trigger the magazine gesture owns");
        Rig p;
        p.in.pouch = p.slot;
        p.in.support = p.slot;
        p.Step();
        p.in.trigger = 1;
        Check(!p.Step().took, "a hand inside the pouch is the pouch's, even if the zones were set to overlap");
    }
    {
        Rig r;
        r.Step();
        r.in.trigger = 1;
        Check(!r.Step().consumeTrigger, "away from the slot the trigger stays the game's grenade");
        r.in.trigger = 1;
        r.in.support = r.slot;
        Check(!r.Step().took, "a trigger already held when entering the slot does not take");
    }
    {
        Rig r;
        r.Take();
        r.in.ready = false;
        const Output paused = r.Step();
        Check(!paused.holding && paused.consumeTrigger && !paused.buttons,
            "pausing cancels the grenade, keeps the trigger hidden");
        r.in.ready = true;
        r.in.support = r.in.support + Vec{0, 0, -0.5f};
        const Output after = r.Release({0, 0, -0.5f}, 5);
        Check(!after.thrown && !after.buttons, "a cancelled hold cannot throw after resume");
    }
    {
        Rig r;
        r.Take();
        // A tracking jump of 1 m in one frame resets the speed history.
        r.Step();
        r.in.support = r.in.support + Vec{0, 0, -1.0f};
        r.Step(150);
        r.in.trigger = 0;
        Check(!r.Step(1).thrown, "a tracking gap or jump never reads as a throw");
    }
    {
        Rig r;
        r.c.enabled = false;
        r.in.support = r.slot;
        r.Step();
        r.in.trigger = 1;
        const Output off = r.Step();
        Check(!off.took && !off.consumeTrigger, "with the feature off the slot does nothing");
        Rig n;
        n.in.trigger = std::numeric_limits<float>::quiet_NaN();
        n.in.support = n.slot;
        Check(!n.Step().took, "an unavailable trigger never takes");
    }
    {
        Rig r;
        r.c.haptic = 0;
        const Output t = r.Take();
        Check(t.took && t.haptic == 0, "zero haptic strength silences the ticks");
    }
    // Grenade-type slot (back hip) and Halo 3 equipment slot (front belt), grip.
    {
        Rig r;
        r.c.typeSlot = true;
        r.c.equipmentSlot = true;
        r.c.equipmentButton = 0x4000;
        SlotPositions s{};
        Check(Slots(r.in, r.c, s), "slots resolve");
        r.in.support = s.typeSlot;
        r.Step();
        r.in.grip = 1;
        const Output grab = r.Step();
        Check(grab.cycled && grab.buttons == kCycle && grab.consumeGrip && grab.haptic > 0,
            "a grab at the back hip switches grenade type with a tick and hides the grip");
        Check(!r.Step().cycled && r.Step().consumeGrip, "holding the grab does not repeat; the grip stays hidden");
        r.in.grip = 0;
        r.Step();
        r.Step(400);
        r.in.support = s.equipment;
        r.Step();
        r.in.grip = 1;
        const Output take = r.Step();
        Check(take.holdingEquipment && take.consumeGrip && !take.buttons, "a grab at the belt takes the equipment");
        r.in.support = s.equipment + Vec{0, 0.3f, -0.3f};
        r.Step();
        r.in.grip = 0;
        const Output dropped = r.Step();
        Check(dropped.deployed && dropped.buttons == 0x4000, "tossing or dropping it outside the belt deploys it");
        r.Step(400);
        r.in.support = s.equipment;
        r.Step();
        r.in.grip = 1;
        r.Step();
        r.in.grip = 0;
        const Output kept = r.Step();
        Check(kept.returned && !kept.deployed && !kept.buttons, "letting go back at the belt keeps it");
        Rig busy;
        busy.c.typeSlot = true;
        busy.in.supportBusy = true;
        SlotPositions b{};
        Slots(busy.in, busy.c, b);
        busy.in.support = b.typeSlot;
        busy.Step();
        busy.in.grip = 1;
        Check(!busy.Step().cycled, "a hand already holding the barrel or a magazine never switches grenades");
        Rig noEquip;
        noEquip.c.equipmentSlot = true; // no equipment action in this game
        SlotPositions n{};
        Slots(noEquip.in, noEquip.c, n);
        noEquip.in.support = n.equipment;
        noEquip.Step();
        noEquip.in.grip = 1;
        Check(!noEquip.Step().holdingEquipment, "the belt does nothing in a game without equipment");
    }
    // Support-hand zones that share an input never overlap: the magazine pouch
    // (trigger or grip) against every slot, and the two grip slots against each
    // other, at usual pouch depths, both hands and both pouch locations. (The
    // weapon-hand holsters cannot conflict with support-hand slots; shallower
    // pouch depths rely on the pouch-priority rule tested above.)
    for (float down : {0.45f, 0.50f, 0.65f, 0.85f})
        for (bool left : {false, true})
            for (int pouchLoc : {0, 1})
            {
                weapon_interaction::Settings w{};
                w.leftHanded = left;
                w.pouchDown = down;
                w.pouchLocation = pouchLoc;
                weapon_interaction::Sample ws{};
                ws.head = {0, 1.65f, 0};
                ws.primary = {0, 1.2f, -0.4f};
                ws.support = ws.primary;
                Vec pouch{}, shoulder{}, hip{};
                w.holsterLocation = 0;
                weapon_interaction::Zones(ws, w, pouch, shoulder);
                w.holsterLocation = 1;
                weapon_interaction::Zones(ws, w, pouch, hip);
                Settings c{};
                c.leftHanded = left;
                c.pouchDown = down;
                Input in{};
                in.head = ws.head;
                in.bodyForward = {0, 0, -1};
                SlotPositions s{};
                Slots(in, c, s);
                const auto clear = [](Vec a, Vec b, float ra, float rb) { return !Near(a, b, ra + rb); };
                const float pr = w.zoneRadius, hr = w.holsterRadius;
                Check(clear(s.chest, pouch, c.slotRadius, pr) && clear(s.typeSlot, pouch, c.typeRadius, pr) &&
                          clear(s.equipment, pouch, c.equipmentRadius, pr),
                    "body slots never overlap the magazine pouch");
                Check(clear(s.typeSlot, s.equipment, c.typeRadius, c.equipmentRadius),
                    "the two grip slots never overlap each other");
                (void)shoulder;
                (void)hip;
                (void)hr;
            }
    std::printf("Grenade throw: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
