// Tests qa_give_logic.h, the automated-testing "give all weapons" one-shot for Halo CE: the
// environment variable parse and the decision sequence. Header-only; built as halomccvr_qa_give_tests.
// Pure logic: no game process.
#include "../src/common/qa_give_logic.h"
#include <cstdio>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace qa_give;

int main()
{
    Check(ParseRequest("all") == Request::All && ParseRequest("ALL") == Request::All &&
              ParseRequest("All") == Request::All,
        "all (any case) requests the give-all");
    Check(ParseRequest(nullptr) == Request::None && ParseRequest("") == Request::None &&
              ParseRequest("al") == Request::None && ParseRequest("all ") == Request::None &&
              ParseRequest("snipe") == Request::None && ParseRequest("0") == Request::None,
        "unset, empty, partial, padded or other values request nothing");

    Check(ParseRequest("sniper") == Request::Sniper && ParseRequest("SNIPER") == Request::Sniper &&
              OneShot(Request::Sniper).Requested() == Request::Sniper,
        "sniper (any case) requests the single sniper rifle");

    const Inputs good{true, true, true};
    {
        OneShot off(Request::None);
        Check(off.Step(good) == Action::Inert && !off.Done(), "no request is inert and never logs");
    }
    {
        OneShot shot(Request::All);
        bool waited = true;
        for (uint32_t i = 0; i + 1 < kStableCalls; ++i)
            waited = waited && shot.Step(good) == Action::Wait;
        Check(waited, "waits for the stable player proof");
        Check(shot.Step(good) == Action::Call && shot.Done(), "calls exactly once after the stable run");
        Check(shot.Step(good) == Action::Inert, "inert after the call");
    }
    {
        OneShot shot(Request::All);
        for (uint32_t i = 0; i + 5 < kStableCalls; ++i)
            shot.Step(good);
        Check(shot.Step({true, true, false}) == Action::Wait, "losing the player proof waits");
        bool waited = true;
        for (uint32_t i = 0; i + 1 < kStableCalls; ++i)
            waited = waited && shot.Step(good) == Action::Wait;
        Check(waited && shot.Step(good) == Action::Call, "the stable run restarts after a lost proof");
    }
    {
        OneShot shot(Request::All);
        Check(shot.Step({false, true, true}) == Action::Skip && shot.Done() && shot.Reason()[0],
            "unverified binding skips once with a reason");
        Check(shot.Step(good) == Action::Inert, "a skip is final");
    }
    {
        OneShot shot(Request::All);
        Check(shot.Step({true, false, true}) == Action::Skip && shot.Reason()[0],
            "network simulation skips with a reason");
    }
    std::printf(g_failures ? "FAILED %d\n" : "qa give tests ok\n", g_failures);
    return g_failures ? 1 : 0;
}
