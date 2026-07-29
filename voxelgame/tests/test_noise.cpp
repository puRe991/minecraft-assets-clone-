// Unit tests for the noise module: PerlinNoise + FractalNoise.
#include "TestFramework.hpp"
#include "vg/noise/FractalNoise.hpp"
#include "vg/noise/PerlinNoise.hpp"

#include <memory>

using namespace vg::noise;

VG_TEST(perlin_deterministic_per_seed) {
    PerlinNoise a(1234), b(1234), c(9999);
    for (double t = 0; t < 5; t += 0.37) {
        CHECK_NEAR(a.noise2D(t, t * 0.5), b.noise2D(t, t * 0.5), 1e-15);   // same seed -> identical
    }
    // different seed should (somewhere) produce a different value
    bool differs = false;
    for (double t = 0; t < 5; t += 0.37)
        if (std::fabs(a.noise2D(t, t) - c.noise2D(t, t)) > 1e-6) differs = true;
    CHECK(differs);
}

VG_TEST(perlin_zero_at_integer_lattice) {
    // Perlin gradient noise is exactly 0 at integer lattice points.
    PerlinNoise n(7);
    CHECK_NEAR(n.noise3D(3, -2, 5), 0.0, 1e-9);
    CHECK_NEAR(n.noise2D(10, 10), 0.0, 1e-9);
}

VG_TEST(perlin_in_range) {
    PerlinNoise n(42);
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 20000; ++i) {
        double x = (i % 211) * 0.123, y = (i % 97) * 0.271, z = (i % 53) * 0.377;
        double v = n.noise3D(x, y, z);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        CHECK(v >= -1.0001 && v <= 1.0001);
    }
    CHECK(lo < -0.2 && hi > 0.2);   // it actually varies, not a flat field
}

VG_TEST(perlin_continuity) {
    // A tiny step in input must produce only a tiny change in output
    // (coherent noise is continuous — no discontinuities).
    PerlinNoise n(3);
    double prev = n.noise2D(2.0, 2.0);
    for (double d = 0; d < 1.0; d += 0.01) {
        double cur = n.noise2D(2.0 + d, 2.0);
        CHECK(std::fabs(cur - prev) < 0.2);
        prev = cur;
    }
}

VG_TEST(perlin_to_unit) {
    CHECK_NEAR(PerlinNoise::toUnit(-1.0), 0.0, 1e-12);
    CHECK_NEAR(PerlinNoise::toUnit(0.0), 0.5, 1e-12);
    CHECK_NEAR(PerlinNoise::toUnit(1.0), 1.0, 1e-12);
}

VG_TEST(fractal_normalised_and_deterministic) {
    auto base = std::make_shared<PerlinNoise>(2024);
    FractalNoise fbm(base, {6, 1.0, 2.0, 0.5});
    FractalNoise fbm2(std::make_shared<PerlinNoise>(2024), {6, 1.0, 2.0, 0.5});
    for (int i = 0; i < 5000; ++i) {
        double x = i * 0.05, y = i * 0.031;
        double v = fbm.noise2D(x, y);
        CHECK(v >= -1.0001 && v <= 1.0001);                 // stays in base range
        CHECK_NEAR(v, fbm2.noise2D(x, y), 1e-12);           // reproducible
    }
}

VG_TEST(fractal_adds_detail) {
    // More octaves should not collapse to the single-octave signal: the fBm
    // field should have higher local variation than one octave alone.
    auto base = std::make_shared<PerlinNoise>(11);
    FractalNoise one(base, {1, 0.5, 2.0, 0.5});
    FractalNoise many(base, {6, 0.5, 2.0, 0.5});
    double varOne = 0, varMany = 0, prevO = 0, prevM = 0;
    for (int i = 1; i < 2000; ++i) {
        double x = i * 0.02;
        double o = one.noise2D(x, 0), m = many.noise2D(x, 0);
        if (i > 1) { varOne += std::fabs(o - prevO); varMany += std::fabs(m - prevM); }
        prevO = o; prevM = m;
    }
    CHECK(varMany > varOne);
}
