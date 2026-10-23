// Tests for fist melee: when a grip makes a fist and when a swing counts (src/common/fist_melee_logic.h).
// Built as halomccvr_fist_melee_tests and run by ctest. Header-only logic: no game process, no headset.
#include "../src/common/fist_melee_logic.h"
#include "../src/common/contact_melee_motion.h"
#include <cstdio>
#include <limits>

using namespace fist_melee;
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

    // Target plane at x = 0.05 (as core_tests' contact scene).
    struct Scene
    {
        unsigned applications = 0;
        bool Query(const contact_melee::Sweep& sweep, contact_melee::Hit& hit) noexcept
        {
            const float dx = sweep.end.x - sweep.start.x;
            if (std::abs(dx) < 1e-6f)
                return false;
            const float t = (0.05f - sweep.start.x) / dx;
            if (t < 0 || t > 1)
                return false;
            hit = {0xCAFE0002, {sweep.start.x + t * dx, 0, 0}, {-1, 0, 0}, t, true};
            return true;
        }
        bool Apply(uint32_t, const contact_melee::Hit&, const contact_melee::Sweep&) noexcept
        {
            ++applications;
            return true;
        }
    };

    // One punch through the target with points 0 (knuckle) and, optionally, 1 (gun).
    unsigned Punch(unsigned gated, bool fist, unsigned count, unsigned movingPoint)
    {
        Scene scene;
        contact_melee::Frame frame;
        frame.referenceEpoch = 1;
        frame.shape = 1;
        frame.unit = 0x12340001;
        frame.serial = 1;
        frame.timeNs = 1'000'000'000;
        frame.count = count;
        frame.bareHandPoints = gated;
        frame.fist = fist;
        contact_melee::Hand hand;
        hand.Process(frame, 5, scene);
        frame.serial++;
        frame.timeNs += 10'000'000;
        frame.points[movingPoint].x = 0.06f;
        hand.Process(frame, 5, scene);
        return scene.applications;
    }
}

int main()
{
    {
        Latch l;
        Check(!l.Update({true, 0.2f, false}), "open hand is no fist");
        Check(l.Update({true, 0.9f, false}), "open-air grip makes a fist");
        Check(l.Update({true, 0.5f, false}), "a half-relaxed grip keeps the fist");
        Check(!l.Update({true, 0.1f, false}), "opening the hand ends the fist");
        Check(!l.Update({true, 0.9f, true}), "a grip a holster or slot claims is no fist");
        Check(!l.Update({true, 0.9f, false}), "and stays no fist while that grip is held");
        l.Update({true, 0.0f, false});
        Check(l.Update({true, 0.9f, false}), "a fresh open-air squeeze is a fist again");
        Check(!l.Update({true, 0.9f, true}), "a fist moved into a claim stops being one");
        l.Reset();
        Check(!l.Update({false, 0.9f, false}) && !l.Update({true, std::numeric_limits<float>::quiet_NaN(), false}),
            "untracked or bad grip is no fist");
    }
    {
        Check(GatedPoints(true, 20, false) == 20 && GatedPoints(true, 20, true) == 0 &&
                  GatedPoints(false, 20, false) == 0,
            "only an empty hand with the option on is gated");
        Check(Punch(0, false, 1, 0) == 1, "ungated: an open hand still strikes (option off)");
        Check(Punch(1, false, 1, 0) == 0, "gated: an open hand passes through");
        Check(Punch(1, true, 1, 0) == 1, "gated: a fist strikes");
        Check(Punch(1, false, 2, 1) == 1, "the held weapon's points are never gated");
    }
    {
        // Left (support) hand: an unclaimed empty hand strikes with no squeeze; a claimed one never does.
        Check(SupportStrikes(false, true, true, false), "open support hand punches when support_free");
        Check(!SupportStrikes(false, false, true, false), "open support hand passes through when off");
        Check(SupportStrikes(true, false, true, false), "a support fist always strikes");
        Check(!SupportStrikes(true, true, true, true), "a claimed support grip never strikes");
        Check(!SupportStrikes(true, true, false, false), "an untracked support hand never strikes");
        Check(Punch(1, SupportStrikes(false, true, true, false), 1, 0) == 1,
            "left open-hand punch lands through the contact gate");
        Check(Punch(1, SupportStrikes(false, false, true, false), 1, 0) == 0, "and is gated without the option");
    }
    std::printf("Fist melee: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
