// Tests vehicle_view_logic.h: the button that switches a vehicle seat between first and third
// person, and the default on entry. Header-only; built as halomccvr_vehicle_view_tests.
// Pure logic: no game process.
#include "../src/common/vehicle_view_logic.h"
#include <cstdio>
#include <cstdlib>

using namespace vehicle_view;
static unsigned checks{};
static void Check(bool ok, const char* name)
{
    ++checks;
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}

int main()
{
    Memory memory;
    State state;
    bool live = true;
    const uint32_t warthog = 0x0301, banshee = 0x0302;
    Event e = Step(state, memory, 0, true, true, live);
    Check(!e.switched && live, "the button does nothing on foot");
    e = Step(state, memory, warthog, false, true, live);
    Check(e.entered && live && !e.switched, "entering follows the configured default");
    e = Step(state, memory, warthog, true, true, live);
    Check(e.switched && e.from && !e.to && !live, "a press switches first person -> third person");
    e = Step(state, memory, warthog, true, true, live);
    Check(!e.switched && !live, "a held button switches once");
    e = Step(state, memory, warthog, false, true, live);
    e = Step(state, memory, warthog, true, true, live);
    Check(e.switched && !e.from && e.to && live, "the next press switches back");
    e = Step(state, memory, warthog, false, true, live);
    e = Step(state, memory, warthog, true, true, live); // -> third
    e = Step(state, memory, warthog, false, true, live);
    Check(!live, "now third person in the warthog");
    e = Step(state, memory, 0, false, true, live);
    Check(live, "leaving the vehicle restores the configured default");
    e = Step(state, memory, banshee, false, true, live);
    Check(live && e.entered, "a different vehicle class starts from the default");
    e = Step(state, memory, warthog, false, true, live);
    Check(!live && e.entered, "the warthog remembers third person");
    bool liveOff = false;
    Memory m2;
    State s2;
    Step(s2, m2, warthog, false, false, liveOff);
    e = Step(s2, m2, warthog, true, false, liveOff);
    Check(e.switched && !e.from && e.to && liveOff, "with the default off a press turns the seat view on");
    Memory many;
    for (uint32_t k = 1; k <= 40; ++k)
        many.Set(k, Mode::Third);
    Check(many.Get(40) == Mode::Third && many.Get(1) == Mode::Unset, "the memory is bounded and drops the oldest");
    {
        XGuard guard;
        Check(!guard.Update(false, true), "X is the game's on foot");
        Check(guard.Update(true, false) && guard.Update(true, true), "X is withheld while seated");
        Check(guard.Update(false, true), "X held past the end of the seat stays withheld");
        Check(guard.Update(false, true) && !guard.Update(false, false), "released: the game gets X again");
        Check(!guard.Update(false, true), "a fresh press on foot reaches the game");
        Check(guard.Update(true, false) && !guard.Update(false, false), "left the seat with X up: nothing withheld");
    }
    std::printf("PASS: %u vehicle view checks\n", checks);
    return 0;
}
