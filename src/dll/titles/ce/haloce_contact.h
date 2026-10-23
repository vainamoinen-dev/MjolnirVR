// CE physical contact: tracked-hand melee, world melee and weapon haptics (haloce_contact.cpp).
// Polled by the CE title tick in src/dll/core/game.cpp; the palette calls come from
// haloce_first_person.cpp. Optional: a failure never changes camera or render ownership.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../../common/titles/ce/haloce_first_person_logic.h"
#include "../../../common/titles/ce/haloce_frame_context.h"
#include "../../../common/contact_melee_motion.h"

struct HaloCEContactPublication
{
    contact_melee::Frame frames[2]{};
    uint32_t generation{};
    uint8_t weaponBoundsSamples[2]{};
};

// Independent optional feature; failure never changes camera/render ownership.
bool HaloCEContact_Poll(uintptr_t base, size_t size, uint32_t generation, bool active) noexcept;
// CE contact (swing) melee hooks are installed and not faulted.
bool HaloCEContact_MeleeReady() noexcept;
void HaloCEContact_ApplyPalette(const halo_ce::RenderContext& context, const halo_ce::FirstPersonBinding& binding,
    const halo_ce::NodeMatrix* authored, halo_ce::NodeMatrix* staged, HaloCEContactPublication& publication) noexcept;
// Publish the unblocked pose only after the native palette really commits.
void HaloCEContact_CommitPalette(
    const halo_ce::RenderContext& context, const HaloCEContactPublication& publication) noexcept;
