// Tests pause_debounce.h: a short pause blip (for example at the end of a load) never switches the
// presentation; a real pause switches after the hold and unpause is immediate.
// Header-only; built as halomccvr_pause_debounce_tests. Pure logic: no game process.
#include "../src/common/pause_debounce.h"
#include <cstdio>

static int failures = 0, checks = 0;
static void Check(bool good, const char* label)
{
    ++checks;
    if (!good)
    {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
}

int main()
{
    using pause_debounce::Entry;
    {
        // the owner's headset log: paused for ~100 ms at the end of a load, then gameplay.
        Entry e;
        bool shown = false;
        for (uint64_t t = 37565; t <= 37665; t += 16)
            shown |= e.Update(true, t);
        shown |= e.Update(false, 37666);
        Check(!shown, "a 100 ms pause blip at load end never switches the presentation");
    }
    {
        Entry e;
        Check(!e.Update(true, 1000), "first paused tick waits");
        Check(!e.Update(true, 1100), "still under the hold");
        Check(e.Update(true, 1150), "a real pause switches after 150 ms");
        Check(e.Update(true, 5000), "stays paused");
        Check(!e.Update(false, 5001), "unpause is immediate");
        Check(!e.Update(true, 5002), "a new pause waits again");
    }
    {
        Entry e;
        e.Update(true, 1000);
        e.Update(false, 1100);
        Check(!e.Update(true, 1200) && !e.Update(true, 1340) && e.Update(true, 1350),
            "an interrupted pause restarts its hold");
    }
    {
        Entry e;
        Check(!e.Update(true, 0) && !e.Update(true, 100) && e.Update(true, 150),
            "a zero tick count is not a missing timestamp");
    }
    std::printf("%d pause debounce checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
