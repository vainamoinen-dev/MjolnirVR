#pragma once
// Weapon frame transfer: moves a point from the stock first-person gun onto the controller-aligned gun.
// Used by src/dll/core/game.cpp; tested by tests/weapon_frame_transfer_tests.cpp.
// Pure maths: no globals, no game memory.
//
// Moves a point that rides on the stock first-person weapon onto the
// controller-aligned weapon, keeping its offset in the WEAPON's frame: the
// offset is taken into stock-weapon space and rotated back out with the moved
// weapon. A translation-only transfer leaves the offset pointing the way the
// stock (head-aligned) weapon pointed, so the error grows with how far the
// controller is angled away from the view - the "muzzle exits the wrong point,
// worse when angled" report. Matrices are row-major with world = R * local.
namespace weapon_frame_transfer
{
    inline void Rotate(const float* r, const float* v, float* out) noexcept
    {
        out[0] = r[0] * v[0] + r[1] * v[1] + r[2] * v[2];
        out[1] = r[3] * v[0] + r[4] * v[1] + r[5] * v[2];
        out[2] = r[6] * v[0] + r[7] * v[1] + r[8] * v[2];
    }
    inline void RotateTranspose(const float* r, const float* v, float* out) noexcept
    {
        out[0] = r[0] * v[0] + r[3] * v[1] + r[6] * v[2];
        out[1] = r[1] * v[0] + r[4] * v[1] + r[7] * v[2];
        out[2] = r[2] * v[0] + r[5] * v[1] + r[8] * v[2];
    }
    // out = movedT + movedR * stockR^T * (point - stockT)
    inline void TransferPoint(const float* stockR, const float* stockT, const float* movedR, const float* movedT,
        const float* point, float* out) noexcept
    {
        const float delta[3] = {point[0] - stockT[0], point[1] - stockT[1], point[2] - stockT[2]};
        float local[3], rotated[3];
        RotateTranspose(stockR, delta, local);
        Rotate(movedR, local, rotated);
        for (int i = 0; i < 3; ++i)
            out[i] = movedT[i] + rotated[i];
    }
} // namespace weapon_frame_transfer
