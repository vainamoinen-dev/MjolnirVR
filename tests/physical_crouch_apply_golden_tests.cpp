// Golden fixture: pins the Halo 3 and ODST physical-crouch camera Apply paths
// (halo3_physical_crouch.inl, odst_physical_crouch.inl, both built on the shared
// native_physical_crouch.inl): the gates, the native reduction, the lease, the
// post-correction identity recheck and the SEH fallback. Prepare (witness proofs)
// is per title and not covered here. Compiled with sigscan.cpp against fake natives.
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../src/common/runtime_types.h"
#include "../src/common/physical_crouch_camera_logic.h"
#include "../src/common/physical_crouch_native_read.h"
#include "../src/common/physical_crouch_additional_witnesses.h"
#include "../src/common/titles/odst/odst_vehicle_logic.h"
#include "../src/dll/core/sigscan.h"
#include "../src/dll/core/guard_count.h"

#define LOG(...) ((void)0)
struct
{
    bool physical_crouch = true;
} g_config;
std::atomic<bool> g_positional{true};
std::atomic<uint32_t> g_halo3RuntimeGeneration{7}, g_odstRuntimeGeneration{9};
uint64_t g_epoch = 5, g_now = 1000;
uint64_t VR_PhysicalCrouchEpoch()
{
    return g_epoch;
}
uint64_t MockNow()
{
    return g_now;
}
#define GetTickCount64 MockNow
PhysicalCrouchCameraRequest g_request{};
bool PhysicalCrouchCamera_Read(PhysicalCrouchCameraRequest& out)
{
    out = g_request;
    return g_request.available;
}

// Owner/unit seams: the getter can switch owner after N calls; a unit can be made to fault.
uint32_t g_owner = 0x00010001, g_switchOwnerAfter = 0;
unsigned g_getterCalls = 0;
int64_t __fastcall PlayerUnitGetter(int)
{
    ++g_getterCalls;
    return (g_switchOwnerAfter && g_getterCalls > g_switchOwnerAfter) ? 0x00020002 : g_owner;
}
int64_t(__fastcall* g_halo3PlayerUnitGetter)(int) = PlayerUnitGetter;
int64_t(__fastcall* g_odstPlayerUnitGetter)(int) = PlayerUnitGetter;
alignas(16) uint8_t h3Unit[0x600]{}, h3Def[0x400]{}, odUnit[0x600]{}, odDef[0x400]{};
bool g_faultUnit = false;
const uint8_t* Halo3MeleeSelectionObject(uint32_t handle, uint8_t)
{
    if (g_faultUnit)
        return reinterpret_cast<const uint8_t*>(uintptr_t(0x10)); // reading it faults
    return handle == g_owner ? h3Unit : nullptr;
}
uint8_t* Halo3LoadedTagDefinition(uint32_t tag)
{
    return tag == 0x1234 ? h3Def : nullptr;
}
// ODST resolves its unit through the contact object table.
alignas(16) uint8_t odTls[0x100]{}, odTable[0x60]{}, odEntries[4 * 0x18]{};
bool OdstContactObject(uint32_t handle, bool)
{
    return handle == g_owner;
}
const uint8_t* OdstContactTls()
{
    return odTls;
}
uint8_t* OdstLoadedTagDefinition(uint32_t tag)
{
    return tag == 0x5678 ? odDef : nullptr;
}

#include "../src/dll/titles/h3/halo3_physical_crouch.inl"
#include "../src/dll/titles/odst/odst_physical_crouch.inl"
#undef GetTickCount64

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool ok, const char* what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", what);
        }
    }
    template <class V>
    void Put(uint8_t* bytes, size_t at, V v)
    {
        std::memcpy(bytes + at, &v, sizeof(v));
    }

    void BuildUnits()
    {
        // Halo 3 (halo3_physical_crouch_reader.inl): standing 0.6, crouched 0.3, fraction 1, scale 1 -> 0.3.
        Put(h3Unit, 0x0, uint32_t(0x1234));
        Put(h3Unit, 0x10, int32_t(-1));
        h3Unit[0x96] = 0;
        Put(h3Unit, 0x110, uint32_t(0));
        Put(h3Unit, 0x162, int16_t(0x200));
        Put(h3Unit, 0x200, uint32_t(0));
        Put(h3Unit, 0x384, 1.0f);
        Put(h3Unit, 0x8C, 1.0f);
        Put(h3Def, 0x2E8, 0.6f);
        Put(h3Def, 0x2EC, 0.3f);
        // ODST (physical_crouch_native::Odst): standing 0.7, crouched 0.2, fraction 0.5, scale 2 -> 0.5.
        Put(odUnit, 0x0, uint32_t(0x5678));
        Put(odUnit, 0x10, uint32_t(UINT32_MAX));
        odUnit[0x96] = 0;
        Put(odUnit, 0x104, uint32_t(0));
        Put(odUnit, 0x15A, int16_t(0x200));
        Put(odUnit, 0x200, int32_t(0));
        Put(odUnit, 0x398, 0.5f);
        Put(odUnit, 0x8C, 2.0f);
        Put(odDef, 0x310, 0.7f);
        Put(odDef, 0x318, 0.2f);
        void* p = odTable;
        std::memcpy(odTls + kOdstTlsObjectTableOffset, &p, 8);
        p = odEntries;
        std::memcpy(odTable + 0x48, &p, 8);
        p = odUnit;
        std::memcpy(odEntries + (g_owner & 0xFFFF) * 0x18 + 0x10, &p, 8);
    }

    using ApplyFn = void (*)(float*, float, bool);
    // One camera frame; returns the Z the title wrote.
    float Frame(
        ApplyFn apply, GameTitle title, uint32_t generation, float down, bool controlled = true, bool requested = true)
    {
        g_request = {title, generation, g_epoch, g_now, true, requested};
        g_getterCalls = 0;
        float position[3]{1, 2, 10};
        apply(position, down, controlled);
        return position[2] - 10.0f;
    }

    void Run(const char* name, ApplyFn apply, GameTitle title, std::atomic<uint32_t>& cameraGeneration,
        std::atomic<uint32_t>& runtimeGeneration, uint8_t* unit, size_t validOffset, float reduction)
    {
        char what[160];
        auto label = [&](const char* text)
        {
            snprintf(what, sizeof(what), "%s: %s", name, text);
            return what;
        };
        const uint32_t generation = runtimeGeneration.load();
        cameraGeneration = generation;
        Check(std::fabs(Frame(apply, title, generation, 1.0f) - reduction) < 1e-6f,
            label("crouched: correction is the native reduction"));
        Check(std::fabs(Frame(apply, title, generation, 0.1f) - 0.1f) < 1e-6f,
            label("never more than the physical drop"));
        Check(std::fabs(Frame(apply, title, generation, 1.0f, true, false) - reduction) < 1e-6f,
            label("lease holds while the native blend returns"));
        Check(Frame(apply, title, generation, 1.0f, false) == 0.0f, label("not player-controlled: stock"));
        Check(Frame(apply, title, generation, 1.0f, true, false) == 0.0f,
            label("after a reset, an unrequested frame does not engage"));
        cameraGeneration = generation + 1;
        Check(
            Frame(apply, title, generation, 1.0f) == 0.0f, label("witnesses not verified for this generation: stock"));
        cameraGeneration = generation;
        g_config.physical_crouch = false;
        Check(Frame(apply, title, generation, 1.0f) == 0.0f, label("option off: stock"));
        g_config.physical_crouch = true;
        g_positional = false;
        Check(Frame(apply, title, generation, 1.0f) == 0.0f, label("positional off: stock"));
        g_positional = true;
        g_switchOwnerAfter = 1;
        Check(Frame(apply, title, generation, 1.0f) == 0.0f, label("owner changed before the recheck: no correction"));
        g_switchOwnerAfter = 0;
        const uint8_t saved = unit[validOffset];
        unit[validOffset] = 0;
        Check(Frame(apply, title, generation, 1.0f) == 0.0f, label("native reduction refused: stock"));
        unit[validOffset] = saved;
        Check(std::fabs(Frame(apply, title, generation, 1.0f) - reduction) < 1e-6f, label("recovers after a refusal"));
    }
}

int main()
{
    BuildUnits();
    Run("Halo 3", Halo3ApplyPhysicalCrouchCamera, GameTitle::Halo3, g_halo3CrouchCameraGeneration,
        g_halo3RuntimeGeneration, h3Unit, 0x10, 0.3f);
    Run("ODST", OdstApplyPhysicalCrouchCamera, GameTitle::Halo3ODST, g_odstCrouchCameraGeneration,
        g_odstRuntimeGeneration, odUnit, 0x10, 0.5f);
    // A faulting unit read is contained by the title's SEH and leaves the camera stock.
    g_faultUnit = true;
    Check(Frame(Halo3ApplyPhysicalCrouchCamera, GameTitle::Halo3, g_halo3RuntimeGeneration.load(), 1.0f) == 0.0f,
        "Halo 3: a faulting unit read leaves the camera stock");
    g_faultUnit = false;
    std::printf("Physical crouch apply golden: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
