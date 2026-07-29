// Unit tests for vg::math::Vec3.
#include "TestFramework.hpp"
#include "vg/math/Vec3.hpp"

#include <unordered_set>

using namespace vg::math;

VG_TEST(vec3_arithmetic) {
    Vec3f a{1, 2, 3}, b{4, 5, 6};
    CHECK((a + b) == Vec3f(5, 7, 9));
    CHECK((b - a) == Vec3f(3, 3, 3));
    CHECK((a * 2.0f) == Vec3f(2, 4, 6));
    CHECK((-a) == Vec3f(-1, -2, -3));
    a += b;
    CHECK(a == Vec3f(5, 7, 9));
}

VG_TEST(vec3_geometry) {
    Vec3d x{1, 0, 0}, y{0, 1, 0};
    CHECK_NEAR(dot(x, y), 0.0, 1e-12);
    CHECK(cross(x, y) == Vec3d(0, 0, 1));       // right-handed
    CHECK_NEAR(length(Vec3d(3, 4, 0)), 5.0, 1e-12);
    Vec3d n = normalize(Vec3d(0, 5, 0));
    CHECK_NEAR(length(n), 1.0, 1e-12);
    CHECK(normalize(Vec3d(0, 0, 0)) == Vec3d(0, 0, 0));   // no NaN on zero vector
}

VG_TEST(vec3_floor_negative) {
    CHECK(floorToInt(Vec3d(-0.1, 2.9, -3.0)) == Vec3i(-1, 2, -3));
    CHECK(floorToInt(Vec3d(15.99, -0.001, 0.0)) == Vec3i(15, -1, 0));
}

VG_TEST(vec3i_hashable) {
    // distinct coordinates should mostly land on distinct hashes
    std::unordered_set<Vec3i> s;
    for (int x = -4; x <= 4; ++x)
        for (int z = -4; z <= 4; ++z)
            s.insert(Vec3i{x, 0, z});
    CHECK(s.size() == 81);
    CHECK(s.count(Vec3i{-4, 0, 3}) == 1);
    CHECK(s.count(Vec3i{5, 0, 0}) == 0);
}
