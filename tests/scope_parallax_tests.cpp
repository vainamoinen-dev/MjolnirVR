// Tests scope_parallax.h: the scope picture's parallax shift and eyebox shadow maths per eye,
// including the shift cap, the brightness floor and wide (non-square) glass.
// Header-only logic; built as halomccvr_scope_parallax_tests. No game process or headset needed.
#include "../src/common/scope_parallax.h"
#include <cmath>
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
static bool Near(float a, float b, float e = 1e-4f)
{
    return std::fabs(a - b) <= e;
}

int main()
{
    using namespace scope_parallax;
    // The plain formula (depth 2, no cap, no floor) for the formula checks below.
    Settings s{};
    s.depth = 2.0f;
    s.maxShift = 100.0f;
    s.floor = 0.0f;
    // On axis: no shift, full picture.
    const float onAxis[3]{0, 0, 0.2f};
    Eye e = Compute(onAxis, 0.06f, s);
    Check(e.valid && e.shiftU == 0 && e.shiftV == 0 && e.k == 1.0f && e.shadowU == 0.5f, "on axis: stock picture");
    // |shift| = depth * tan(angle).
    const float angle = 5.0f * 0.017453292519943295f;
    const float right[3]{0.2f * std::tan(angle), 0, 0.2f};
    e = Compute(right, 0.06f, s);
    Check(Near(std::fabs(e.shiftU), 2.0f * std::tan(angle)) && e.shiftU < 0 && e.shiftV == 0,
        "shift = depth x tan(angle), opposite the eye");
    const float up[3]{0, 0.01f, 0.2f};
    e = Compute(up, 0.06f, s);
    Check(e.shiftV > 0 && Near(e.shiftV, 2.0f * 0.05f) && e.shadowV < 0.5f,
        "eye up: picture samples lower, shadow chases up");
    // Eyebox endpoints: full inside the tolerance, black past tolerance + fade, 0.648 at 5 cm.
    const float in[3]{0.03f, 0, 0.2f}, out[3]{0.08f, 0, 0.2f}, five[3]{0.05f, 0, 0.2f};
    Check(Compute(in, 0.06f, s).k == 1.0f, "eyebox: 3 cm keeps the full picture");
    Check(Compute(out, 0.06f, s).k == 0.0f, "eyebox: 8 cm is black");
    Check(Near(Compute(five, 0.06f, s).k, 0.648f, 1e-3f), "eyebox: 5 cm off axis = 0.648 (harness capture value)");
    // Left/right sign and the stereo difference ~ depth * IPD / d.
    const float le[3]{-0.0315f, 0, 0.2f}, re[3]{0.0315f, 0, 0.2f};
    Eye pair[2]{Compute(le, 0.06f, s), Compute(re, 0.06f, s)};
    Check(pair[0].shiftU > 0 && pair[1].shiftU < 0 && Near(pair[0].shiftU - pair[1].shiftU, 2.0f * 0.063f / 0.2f),
        "left/right shift signs and difference");
    // Other eye floor.
    const float aim[3]{0.002f, 0, 0.2f}, other[3]{0.07f, 0, 0.2f};
    Eye eyes[2]{Compute(aim, 0.06f, s), Compute(other, 0.06f, s)};
    const float rawOther = eyes[1].k;
    ApplyOtherEye(eyes, s);
    Check(rawOther < 0.2f && Near(eyes[1].k, 0.2f) && eyes[0].k == 1.0f, "other eye floored at scope_eyebox_other_eye");
    Settings open = s;
    open.otherEye = 1.0f;
    Eye eyes2[2]{Compute(aim, 0.06f, s), Compute(other, 0.06f, s)};
    ApplyOtherEye(eyes2, open);
    Check(eyes2[1].k == 1.0f, "other eye opened up fully at 1.0");
    // Off switches.
    Settings off = s;
    off.parallax = false;
    off.eyebox = false;
    e = Compute(five, 0.06f, off);
    Check(e.valid && e.shiftU == 0 && e.shiftV == 0 && e.k == 1.0f && e.shadowU == 0.5f, "both off: stock picture");
    // Bad input: stock picture, not valid.
    const float nanEye[3]{NAN, 0, 0.2f}, behind[3]{0, 0, 0.005f};
    Check(!Compute(nanEye, 0.06f, s).valid && !Compute(behind, 0.06f, s).valid && !Compute(onAxis, 0, s).valid &&
              Compute(behind, 0.06f, s).k == 1.0f,
        "NaN, d <= 0.01 m or no width: stock picture");
    {
        // Never black: shipped defaults cap the shift and floor the brightness.
        const Settings def{};
        Check(def.depth == 0.5f && def.maxShift == 0.25f && def.floor == 0.35f,
            "defaults: depth 0.5, cap 0.25, floor 0.35");
        const float far[3]{0.15f, 0.0f, 0.275f}; // well off the axis (measured poses were 10-13 cm, 14-27 deg)
        const Eye e2 = Compute(far, 0.06f, def);
        Check(Near(std::sqrt(e2.shiftU * e2.shiftU + e2.shiftV * e2.shiftV), 0.25f, 1e-4f),
            "shift capped at 0.25 glass widths");
        Check(e2.k == 0.0f && Near(e2.floor, 0.35f), "past the eyebox the disc closes but the picture stays at 0.35");
        const float small[3]{0.004f, 0.0f, 0.2f};
        const Eye e3 = Compute(small, 0.06f, def);
        Check(Near(e3.shiftU, -0.5f * 0.004f / 0.2f), "a small offset is not capped");
        Settings bad = def;
        bad.floor = NAN;
        bad.maxShift = NAN;
        const Eye e4 = Compute(far, 0.06f, bad);
        Check(Near(e4.floor, 0.35f) && Near(std::sqrt(e4.shiftU * e4.shiftU + e4.shiftV * e4.shiftV), 0.25f, 1e-4f),
            "bad cap/floor fall back to the defaults");
        Settings noBox = def;
        noBox.eyebox = false;
        Check(Compute(far, 0.06f, noBox).floor == 1.0f, "eyebox off: no shadow at all");
    }
    {
        // A 3:1 glass (0.12 x 0.04 m) moves its picture the same physical distance
        // sideways as up: u in its own width = the square's u / 3; v as the square.
        Settings def{};
        const float side[3]{0.004f, 0.004f, 0.2f};
        const Eye sq = Compute(side, 0.04f, def), wide = Compute(side, 0.12f, def, 0.04f);
        Check(Near(wide.shiftU, sq.shiftU / 3.0f) && Near(wide.shiftV, sq.shiftV),
            "z42: wide glass shift in physical units");
        const float far2[3]{0.2f, 0.0f, 0.2f};
        Check(Near(std::fabs(Compute(far2, 0.12f, def, 0.04f).shiftU), 0.25f / 3.0f),
            "z42: wide glass cap = maxShift heights");
    }
    std::printf("Scope parallax: %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
