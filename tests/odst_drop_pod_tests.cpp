// ODST intro drop pod recognition: the runtime census shape (one-seat
// human_jeep driver seat that is no Warthog/Mongoose) and nothing else.
#include "../src/common/titles/odst/odst_vehicle_logic.h"
#include "../src/common/head_look_test.h"
#include <cstdio>
#include <initializer_list>
#include <cmath>
#include <limits>
#include <string>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

int main()
{
    OdstDefinitionFields pod{};
    pod.physicsType = 1;
    pod.seatFlagsValid = true;
    pod.seat0Flags = 0x8004; // census: def 0x1450, seat0Flags=0x8004
    pod.inVehicle = true;
    Check(OdstIsDropPodSeat(pod, 1), "census pod shape (engine moment unread)");
    pod.jeepValid = true;
    pod.engineMoment = 123.0f;
    Check(OdstIsDropPodSeat(pod, 1), "census pod shape (other engine moment)");

    OdstDefinitionFields hog = pod;
    hog.engineMoment = kOdstWarthogEngineMoment;
    Check(!OdstIsDropPodSeat(hog, 1) && !OdstIsDropPodSeat(hog, 6), "warthog never");
    OdstDefinitionFields goose = pod;
    goose.engineMoment = kOdstMongooseEngineMoment;
    Check(!OdstIsDropPodSeat(goose, 1), "mongoose never");
    Check(!OdstIsDropPodSeat(pod, 2) && !OdstIsDropPodSeat(pod, 0), "only a one-seat vehicle");
    OdstDefinitionFields passenger = pod;
    passenger.seat0Flags = 0x8000;
    Check(!OdstIsDropPodSeat(passenger, 1), "seat 0 must be a driver seat");
    OdstDefinitionFields invalid = pod;
    invalid.seat0Flags |= kOdstSeatInvalidForPlayerBit;
    Check(!OdstIsDropPodSeat(invalid, 1), "invalid-for-player seat never");
    OdstDefinitionFields unread = pod;
    unread.seatFlagsValid = false;
    Check(!OdstIsDropPodSeat(unread, 1), "unread seat flags never");
    for (int type : {-1, 0, 2, 3, 4, 7, 8, kOdstPhysicsTypeTurret, kOdstPhysicsTypeNoneAuthored})
    {
        OdstDefinitionFields other = pod;
        other.physicsType = type;
        Check(!OdstIsDropPodSeat(other, 1), "only human_jeep physics");
    }

    // Head look counts as the pod's look-around input.
    {
        head_look_test::Anchor anchor;
        Check(head_look_test::Step(anchor, 0.0f, 0.0f) == 0, "first sample only arms");
        Check(head_look_test::Step(anchor, 0.05f, 0.05f) == 0, "under 5 degrees counts nothing");
        Check(head_look_test::Step(anchor, 0.10f, 0.0f) == head_look_test::kRightBit, "head right = look right");
        Check(head_look_test::Step(anchor, 0.10f, 0.0f) == 0, "no repeat without new travel");
        Check(head_look_test::Step(anchor, 0.0f, 0.0f) == head_look_test::kLeftBit, "head back left = look left");
        Check(head_look_test::Step(anchor, 0.0f, 0.2f) == head_look_test::kUpBit, "head up = look up");
        Check(head_look_test::Step(anchor, 0.0f, 0.0f) == head_look_test::kDownBit, "head down = look down");
        const uint64_t both = head_look_test::Step(anchor, -0.2f, -0.2f);
        Check(both == (head_look_test::kLeftBit | head_look_test::kDownBit), "both axes in one sample");
        head_look_test::Anchor wrap;
        head_look_test::Step(wrap, 3.0f, 0.0f);
        Check(head_look_test::Step(wrap, -3.0f, 0.0f) == head_look_test::kRightBit, "yaw wraps across +/-pi");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(head_look_test::Step(wrap, nan, 0.0f) == 0 && !wrap.armed, "non-finite disarms");
        Check(head_look_test::kAllBits == 0x780, "bits 7..10");
        float yaw = 1, pitch = 1;
        const float identity[4] = {0, 0, 0, 1};
        head_look_test::YawPitchFromQuat(identity, yaw, pitch);
        Check(std::fabs(yaw) < 1e-6f && std::fabs(pitch) < 1e-6f, "identity head looks straight ahead");
        const float s45 = 0.38268343f, c45 = 0.92387953f; // 45 deg about -Y: turn right
        const float right[4] = {0, -s45, 0, c45};
        head_look_test::YawPitchFromQuat(right, yaw, pitch);
        Check(yaw > 0.7f && yaw < 0.87f, "head turned right = + yaw");
        const float up[4] = {s45, 0, 0, c45}; // 45 deg about +X: look up
        head_look_test::YawPitchFromQuat(up, yaw, pitch);
        Check(pitch > 0.7f && pitch < 0.87f, "head tilted up = + pitch");
        {
            using namespace head_look_test;
            PollSnapshot none{};
            Check(FeedBits(none, kAllBits, 0) == 0, "nothing polled: nothing fed");
            PollSnapshot all{};
            all.polled[PollAll] = true;
            Check(FeedBits(all, kDownBit, kUpBit) == kDownBit, "all_directions: down may follow up");
            Check(FeedBits(all, kAllBits, kAllBits) == kAllBits, "all_directions: every direction");
            PollSnapshot invert{};
            invert.polled[PollUp] = invert.polled[PollDown] = true;
            Check(FeedBits(invert, kDownBit, kUpBit) == 0, "up-vs-down check: never add down after up");
            Check(FeedBits(invert, kUpBit, 0) == kUpBit, "up-vs-down check: first direction passes");
            Check(FeedBits(invert, kLeftBit | kUpBit, 0) == kUpBit, "only polled directions are fed");
            PollSnapshot bothInner = invert;
            bothInner.polled[PollAll] = true;
            Check(FeedBits(bothInner, kDownBit, kUpBit) == kDownBit, "all_directions wins over the comparison");
            PollSnapshot single{};
            single.polled[PollLeft] = true;
            Check(FeedBits(single, kLeftBit | kRightBit, kRightBit) == kLeftBit, "single poll: just that one");
            PollTracker tracker;
            tracker.Note(PollUp, 1000);
            Check(Snapshot(tracker, 2999).polled[PollUp] && !Snapshot(tracker, 3001).polled[PollUp],
                "a poll counts for two seconds");
            Check(!Snapshot(tracker, 1500).polled[PollDown], "other polls stay quiet");
        }
        {
            using namespace head_look_test;
            Check(OnlyIdleAxes(kAllBits, 0.0f, 0.0f) == kAllBits, "idle axes are fed");
            Check(OnlyIdleAxes(kAllBits, 0.01f, 0.0f) == (kUpBit | kDownBit), "moving yaw: yaw not fed");
            Check(OnlyIdleAxes(kAllBits, 0.0f, -0.02f) == (kLeftBit | kRightBit), "moving pitch: pitch not fed");
            Check(OnlyIdleAxes(kLeftBit, -0.0f, 1.0f) == kLeftBit, "negative zero counts as idle");
        }
        char letters[5];
        head_look_test::Letters(head_look_test::kUpBit | head_look_test::kRightBit, letters);
        Check(std::string(letters) == "UR", "letters");
    }

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("odst_drop_pod_tests: all passed\n");
    return 0;
}
