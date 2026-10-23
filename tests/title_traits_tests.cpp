// TitleTraits: every supported title has exactly one row, and the rows hold the
// behaviour the shared code had inline before it moved here (a change to a row
// is a behaviour change and must update this test deliberately).
#include "../src/common/title_traits.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
    const GameTitle supported[] = {GameTitle::Halo3, GameTitle::Halo3ODST, GameTitle::HaloReach, GameTitle::Halo4,
        GameTitle::HaloCE, GameTitle::Halo2};
    for (GameTitle title : supported)
    {
        int rows = 0;
        for (const TitleTraits& t : kTitleTraits)
            rows += t.title == title ? 1 : 0;
        Check(rows == 1, "each supported title has exactly one traits row");
        Check(FindTitleTraits(title) && FindTitleTraits(title)->title == title, "FindTitleTraits finds it");
    }
    Check(FindTitleTraits(GameTitle::None) == nullptr && FindTitleTraits(GameTitle::Unknown) == nullptr,
        "None/Unknown have no row (shared defaults apply)");
    Check(sizeof(kTitleTraits) / sizeof(kTitleTraits[0]) == 6, "six rows");

    using P = AuthoredReticleRefreshPolicy;
    Check(FindTitleTraits(GameTitle::Halo3)->crosshairRefresh == P::IdentityAndColorState &&
              FindTitleTraits(GameTitle::Halo3)->crosshairAnimatesWhenConfigured,
        "Halo 3: identity+colour, bounded animation only while animation frames are configured");
    Check(FindTitleTraits(GameTitle::Halo3ODST)->crosshairRefresh == P::IdentityImmediate, "ODST: identity-immediate");
    for (GameTitle title : {GameTitle::HaloReach, GameTitle::Halo4, GameTitle::HaloCE, GameTitle::Halo2})
        Check(FindTitleTraits(title)->crosshairRefresh == P::BoundedAnimation &&
                  !FindTitleTraits(title)->crosshairAnimatesWhenConfigured,
            "Reach/Halo 4/CE/Halo 2: bounded animation always");
    for (GameTitle title : supported)
        Check(FindTitleTraits(title)->crosshairKeyPerDrawnWidget == (title == GameTitle::HaloReach),
            "only Reach folds its crosshair key per drawn widget");

    for (GameTitle title : supported)
    {
        const TitleTraits* t = FindTitleTraits(title);
        Check(t->dpadLeftHandBesideHead == (title == GameTitle::HaloCE),
            "only CE uses the left hand beside the left of the head");
        Check(t->dpadClickIsGraphicsSwitch == (title == GameTitle::HaloCE || title == GameTitle::Halo2),
            "only CE and Halo 2 switch graphics on the gesture click");
    }
    Check(!std::strcmp(FindTitleTraits(GameTitle::HaloCE)->crosshairLogName, "CE") &&
              !std::strcmp(FindTitleTraits(GameTitle::HaloCE)->gestureLogName, "Halo CE") &&
              !std::strcmp(FindTitleTraits(GameTitle::Halo2)->gestureLogName, "Halo 2"),
        "log names match the existing log lines");

    if (g_failures == 0)
        std::printf("title traits tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
