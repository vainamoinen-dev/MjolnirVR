#pragma once
// Shared physical-crouch camera correction for the two Halo 3-engine titles
// (Halo 3, ODST). Each title keeps its own witness
// proofs (Prepare) and supplies a traits struct with only what differs:
//   kTitle, Generation(), CameraGeneration() (the generation its witnesses
//   were verified for), HasPlayerUnit(), PlayerUnit(), Unit(owner),
//   Definition(tag), Reduction(unit, definition, reduction).
// Read-only: no hook, engine call or native write; the correction is bounded
// below the native camera and is applied only if the local owner, its unit,
// tag and definition and the runtime generation are unchanged afterwards.
// One instantiation per title, so each keeps its own thread_local lease.
template <class T>
void ApplyNativePhysicalCrouch(float* position, float physicalDown, bool playerControlled)
{
    static thread_local PhysicalCrouchCameraLease lease;
    PhysicalCrouchCameraRequest request{};
    const auto generation = T::Generation();
    if (!playerControlled || !g_config.physical_crouch || !g_positional.load() || !generation ||
        T::CameraGeneration() != generation || !T::HasPlayerUnit() || !PhysicalCrouchCamera_Read(request))
    {
        lease.Reset();
        return;
    }
    __try
    {
        const uint32_t owner = T::PlayerUnit();
        const auto* unit = T::Unit(owner);
        const auto tag = unit ? *reinterpret_cast<const uint32_t*>(unit) : UINT32_MAX;
        const auto* definition = T::Definition(tag);
        float reduction = 0.f;
        if (!T::Reduction(unit, definition, reduction))
        {
            lease.Reset();
            return;
        }
        const float correction = lease.Update(request, T::kTitle, generation, VR_PhysicalCrouchEpoch(),
            GetTickCount64(), owner, reinterpret_cast<uintptr_t>(unit), reduction, physicalDown);
        if (correction > 0.f && T::PlayerUnit() == owner && T::Unit(owner) == unit &&
            *reinterpret_cast<const uint32_t*>(unit) == tag && T::Definition(tag) == definition &&
            T::Generation() == generation)
            position[2] += correction;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the native crouch correction read freed unit memory");
        lease.Reset();
    }
}
