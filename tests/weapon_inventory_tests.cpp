// Shared weapon inventory decision: role validation, explicit-empty primary
// reporting, lazy handle reads in the titles' order, ownership, reserve.
#include "../src/common/weapon_inventory_logic.h"
#include <cstdio>
#include <vector>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace weapon_inventory;

int main()
{
    const uint32_t A = 0x00110001, B = 0x00220002, C = 0x00330003;
    uint32_t inventory[4]{A, B, C, UINT32_MAX};
    std::vector<int> reads;
    const auto at = [&](uint8_t i)
    {
        reads.push_back(i);
        return inventory[i];
    };
    const auto all = [](uint32_t) { return true; };
    const auto onlyA = [&](uint32_t h) { return h == A; };

    uint32_t w[2]{};
    bool absent = true;
    reads.clear();
    Check(ReadRoleWeapons(0, kNoRole, false, w, &absent, at, all) && w[0] == A && w[1] == UINT32_MAX && !absent,
        "primary only");
    Check(reads == std::vector<int>{0}, "only the primary role's handle is read");
    reads.clear();
    Check(ReadRoleWeapons(1, 0, false, w, &absent, at, all) && w[0] == B && w[1] == A, "dual: primary then secondary");
    Check((reads == std::vector<int>{1, 0}), "handles read in role order");
    reads.clear();
    absent = false;
    Check(!ReadRoleWeapons(kNoRole, kNoRole, false, w, &absent, at, all) && absent && reads.empty(),
        "explicit empty primary reported before refusal, nothing read");
    Check(!ReadRoleWeapons(4, kNoRole, false, w, &absent, at, all) && !absent, "primary role 4 refused");
    Check(!ReadRoleWeapons(0, 4, false, w, nullptr, at, all), "secondary role 4 refused");
    Check(!ReadRoleWeapons(0, 0, false, w, nullptr, at, all), "same role twice refused");
    Check(!ReadRoleWeapons(0, kNoRole, true, w, nullptr, at, all), "dual reader refuses missing secondary");
    Check(ReadRoleWeapons(0, 1, true, w, nullptr, at, all), "dual reader accepts two roles");
    reads.clear();
    Check(!ReadRoleWeapons(1, kNoRole, false, w, nullptr, at, onlyA) && w[0] == B && reads.size() == 1,
        "unowned primary refused after its handle is read");
    inventory[1] = A;
    Check(!ReadRoleWeapons(0, 1, false, w, nullptr, at, all), "duplicate handle refused");
    inventory[1] = B;
    Check(ReadRoleWeapons(0, kNoRole, false, w, nullptr, at, all), "null absent pointer ok");

    uint32_t primary = 0, reserve = 0;
    inventory[2] = UINT32_MAX;
    Check(ReadReserveWeapon(0, kNoRole, primary, reserve, at, all) && primary == A && reserve == B,
        "reserve = other owned");
    Check(ReadReserveWeapon(1, kNoRole, primary, reserve, at, all) && primary == B && reserve == A, "swap flips");
    Check(ReadReserveWeapon(0, 1, primary, reserve, at, all) && reserve == reserve_weapon::kNoHandle,
        "both roles held -> no reserve");
    inventory[2] = C;
    Check(ReadReserveWeapon(0, 1, primary, reserve, at, all) && reserve == C, "dual: third weapon is the reserve");
    Check(ReadReserveWeapon(0, kNoRole, primary, reserve, at, all) && reserve == reserve_weapon::kNoHandle,
        "two candidates ambiguous");
    Check(!ReadReserveWeapon(1, kNoRole, primary, reserve, at, onlyA) && primary == reserve_weapon::kNoHandle,
        "unowned primary refused");
    Check(!ReadReserveWeapon(kNoRole, kNoRole, primary, reserve, at, all), "empty primary refused");

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("weapon_inventory_tests: all passed\n");
    return 0;
}
