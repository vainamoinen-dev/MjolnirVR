// Tests scope_log.h: the text of the scope diagnostic log lines (head FOV, lens and muzzle fields,
// signed distances). Header-only; built as halomccvr_scope_log_tests. Pure logic: no game process.
#include "../src/common/scope_log.h"
#include <cstring>

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
    using namespace scope_log;
    char line[256];
    const float cam[3]{1, 2, 3}, lens[3]{1.1f, 2, 3}, muzzle[3]{1.5f, 2, 3};
    FormatCamOrigin(line, sizeof line, 3, 0x110B141Eull, cam, lens, muzzle, -0.5f, 0.123f);
    Check(
        !std::strcmp(line,
            "scope cam origin: title=3 weapon=00000000110B141E cam=(1.000,2.000,3.000) "
            "lens=(1.100,2.000,3.000) muzzle=(1.500,2.000,3.000) fwd-of-muzzle=-0.500 head-dist=0.123 cam-to-lens=0.100"),
        "L1 text");
    FormatCamOrigin(line, sizeof line, 3, 0, cam, nullptr, nullptr, NAN, 0, 78.04f, 151.0f);
    Check(std::strstr(line, " head-vfov=78.0->151.0") != nullptr, "L1 Reach head FOV before/after the cull widening");
    FormatCamOrigin(line, sizeof line, 1, 0, cam, nullptr, nullptr, NAN, 0);
    Check(!std::strstr(line, "head-vfov"), "no head FOV field by default");
    Check(!std::strstr(line, "cam-to-lens"), "no cam-to-lens without a lens");
    Check(std::strstr(line, "lens=(n/a) muzzle=(n/a) fwd-of-muzzle=n/a") != nullptr, "L1 without lens or muzzle");
    const float offset[3]{0.015f, -0.04f, -0.25f};
    FormatQuad(line, sizeof line, offset, 0.071f, 0.071f, 12.5f, 0x110B141Eull);
    Check(!std::strcmp(line,
              "scope quad: eye-local offsets 0.015/-0.040/-0.250 size 0.071x0.071 angle=12.5 weapon=00000000110B141E"),
        "L2 text");
    FormatReleased(line, sizeof line, Release::Far, 2450);
    Check(!std::strcmp(line, "scope released: reason=far held=2450"), "L3 text");
    Check(!std::strcmp(ReleaseName(Release::Weapon), "weapon"), "live glass: the glass gun put away");
    {
        char hold[16];
        FormatQuadHold(hold, sizeof hold, true);
        Check(!std::strcmp(hold, " hold=1"), "L2 hold tag");
        FormatQuadHold(hold, sizeof hold, false);
        Check(hold[0] == 0, "L2 without the hold tag");
        char raised[96];
        FormatRaised(raised, sizeof raised, 0.2034f, -3.26f, 'R');
        Check(!std::strcmp(raised, "scope glass raised: d=0.203 pitch=-3.3 eye=R"), "raised pose line");
        char shape[32];
        FormatQuadShape(shape, sizeof shape, "rounded-rect");
        Check(!std::strcmp(shape, " shape=rounded-rect"), "L2 shape field");
        FormatQuadShape(shape, sizeof shape, nullptr);
        Check(!std::strcmp(shape, " shape=circle"), "L2 shape field: null is circle");
        char eye[96];
        FormatQuadEye(eye, sizeof eye, 'L', 5.0f, 0.175f, -0.01f, 0.648f);
        Check(!std::strcmp(eye, " eye=L angle=5.00 shift=(0.1750,-0.0100) eyebox=0.648"), "S4 L2 per-eye fields");
    }
    Check(!std::strcmp(ReleaseName(Release::Lower), "lower") && !std::strcmp(ReleaseName(Release::Button), "button"),
        "release names");
    // A camera at the head (behind the gun) is behind the muzzle: negative.
    const float head[3]{0, 0, 0}, muz[3]{0.5f, 0, 0}, dir[3]{1, 0, 0};
    Check(std::fabs(ForwardOfMuzzle(head, muz, dir, 1.0f / 3.048f) + 0.5f * 3.048f) < 1e-4f,
        "a head camera is behind the muzzle (world units -> metres)");
    Check(std::isnan(ForwardOfMuzzle(head, muz, head, 1.0f)), "no direction: n/a");
    std::printf("%d scope log checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
