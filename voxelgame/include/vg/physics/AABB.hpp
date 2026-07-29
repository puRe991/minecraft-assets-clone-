// vg/physics/AABB.hpp
//
// Axis-aligned bounding box in world space plus the small set of queries the
// collision system needs. Header-only value type (SRP).
#pragma once

#include <algorithm>
#include <cmath>

#include "vg/math/Vec3.hpp"

namespace vg::physics {

using math::Vec3d;

struct AABB {
    Vec3d min, max;

    // Build a player-style box from a feet position (x,z centred, y at feet).
    static AABB fromFeet(const Vec3d& feet, double width, double height) {
        double hw = width * 0.5;
        return {{feet.x - hw, feet.y, feet.z - hw},
                {feet.x + hw, feet.y + height, feet.z + hw}};
    }

    AABB translated(const Vec3d& d) const { return {min + d, max + d}; }

    bool intersects(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x &&
               min.y < o.max.y && max.y > o.min.y &&
               min.z < o.max.z && max.z > o.min.z;
    }
};

}  // namespace vg::physics
