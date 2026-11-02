#pragma once
#include <algorithm>
#include <cmath>
#include <libretro.h>

namespace LibretroVr
{
constexpr float pi            = 3.14159265358979323846f;
constexpr float unitsPerMeter = 400.f;

struct Angles
{
    float yaw, pitch, roll;
};

inline bool valid(const retro_vr_eye_state &eye)
{
    float norm = 0;
    for (float f : eye.position)
        if (!std::isfinite(f) || std::fabs(f) > 10000)
            return false;

    for (float f : eye.orientation)
    {
        if (!std::isfinite(f))
            return false;
        norm += f * f;
    }

    if (norm < .9f || norm > 1.1f)
        return false;

    for (float f : eye.fov_tan)
        if (!std::isfinite(f) || f < .05f || f > 10.f)
            return false;

    return true;
}

inline Angles angles(const float *q)
{
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;

    // Y-up right-handed tracking: forward is -Z; positive Build yaw turns right.
    return { std::atan2(-2 * (x * z + w * y), 1 - 2 * (x * x + y * y)), std::asin(std::max(-1.f, std::min(1.f, 2 * (w * x - y * z)))),
             std::atan2(2 * (x * y + w * z), 1 - 2 * (x * x + z * z)) };
}

struct Vec3
{
    float x, y, z;
};

// Forward (-Z) direction of an orientation quaternion (x, y, z, w) in tracking space.
inline Vec3 forward(const float *q)
{
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;

    return { -2 * (x * z + w * y), -2 * (y * z - w * x), -(1 - 2 * (x * x + y * y)) };
}

// Rotates v by the inverse of the orientation quaternion q.
inline Vec3 unrotate(const float *q, Vec3 v)
{
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    const float x = -q[0] / n, y = -q[1] / n, z = -q[2] / n, w = q[3] / n;
    const Vec3  c = { y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x };
    const Vec3  d = { y * c.z - z * c.y, z * c.x - x * c.z, x * c.y - y * c.x };

    return { v.x + 2 * (w * c.x + d.x), v.y + 2 * (w * c.y + d.y), v.z + 2 * (w * c.z + d.z) };
}

struct Crop
{
    float x, y, w, h, tangent;
};

inline Crop crop(const retro_vr_eye_state &eye, float aspect)
{
    auto  f = eye.fov_tan;
    float t = std::max(std::max(f[0], f[1]), std::max(f[2], f[3]) * aspect);
    float v = t / aspect;

    return { (t - f[0]) / (2 * t), (v - f[3]) / (2 * v), (f[0] + f[1]) / (2 * t), (f[2] + f[3]) / (2 * v), t };
}
}  // namespace LibretroVr
