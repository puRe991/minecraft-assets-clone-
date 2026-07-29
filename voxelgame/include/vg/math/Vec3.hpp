// vg/math/Vec3.hpp
//
// A small, dependency-free 3D vector used across the engine. Header-only and
// templated so the same type works for float positions, double math, and
// integer block coordinates.
//
// Design notes:
//  - Kept intentionally minimal (SRP): it is a value type with arithmetic and
//    the handful of geometric operations the rest of the engine needs.
//  - `constexpr` where possible so vectors can be used in compile-time config.
//  - Free functions (dot/cross/length/normalize) rather than members where the
//    operation is symmetric, which reads more naturally and keeps the type slim.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>

namespace vg::math {

template <typename T>
struct Vec3 {
    T x{}, y{}, z{};

    constexpr Vec3() = default;
    constexpr Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(T s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(T s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }

    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    constexpr bool operator!=(const Vec3& o) const { return !(*this == o); }
};

template <typename T> constexpr T dot(const Vec3<T>& a, const Vec3<T>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
template <typename T> constexpr Vec3<T> cross(const Vec3<T>& a, const Vec3<T>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <typename T> T length(const Vec3<T>& v) { return std::sqrt(dot(v, v)); }

// Returns a unit-length copy; a zero vector is returned unchanged (no NaN).
template <typename T> Vec3<T> normalize(const Vec3<T>& v) {
    T len = length(v);
    return len > T(0) ? v / len : v;
}

// Component-wise floor to integer block coordinates (handles negatives).
inline Vec3<int> floorToInt(const Vec3<double>& v) {
    return {static_cast<int>(std::floor(v.x)),
            static_cast<int>(std::floor(v.y)),
            static_cast<int>(std::floor(v.z))};
}

using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;
using Vec3i = Vec3<int>;

}  // namespace vg::math

// Hash so Vec3i can key unordered_map/set (used later for chunk lookup).
namespace std {
template <>
struct hash<vg::math::Vec3i> {
    size_t operator()(const vg::math::Vec3i& v) const noexcept {
        // splitmix-style mixing of the three coordinates
        auto mix = [](uint64_t h) {
            h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
            h ^= h >> 33; return h;
        };
        uint64_t h = mix((uint64_t)(uint32_t)v.x * 0x9E3779B97F4A7C15ULL + 0x1234567);
        h ^= mix((uint64_t)(uint32_t)v.y) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        h ^= mix((uint64_t)(uint32_t)v.z) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        return (size_t)h;
    }
};
}  // namespace std
