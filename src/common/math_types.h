#pragma once
// The one 3-vector, quaternion and pose of the common logic headers.
// Each title/feature namespace names these with a `using` alias
// (x, y, z; x, y, z, w = 1). No dependencies, no globals.
namespace mvr_math
{
    struct Vec3
    {
        float x{}, y{}, z{};
    };
    struct Quat
    {
        float x{}, y{}, z{}, w{1};
    };
    struct Pose
    {
        Vec3 position{};
        Quat orientation{};
    };
    inline Vec3 operator+(Vec3 a, Vec3 b) noexcept
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }
    inline Vec3 operator-(Vec3 a, Vec3 b) noexcept
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }
    inline Vec3 operator*(Vec3 a, float s) noexcept
    {
        return {a.x * s, a.y * s, a.z * s};
    }
} // namespace mvr_math
