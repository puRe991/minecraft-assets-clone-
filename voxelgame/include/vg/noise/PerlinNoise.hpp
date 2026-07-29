// vg/noise/PerlinNoise.hpp
//
// Ken Perlin's "improved" gradient noise (2002), seeded so a given world seed
// always reproduces the same terrain. Implements INoise.
//
// Output range: approximately [-1, 1] for both 2D and 3D. The classic bound is
// +/- sqrt(N/4) but in practice values stay within [-1, 1]; callers that need a
// strict [0, 1] should use toUnit().
#pragma once

#include <array>
#include <cstdint>

#include "vg/noise/INoise.hpp"

namespace vg::noise {

class PerlinNoise final : public INoise {
public:
    // Construct a permutation table deterministically from `seed`.
    explicit PerlinNoise(uint32_t seed = 0);

    double noise2D(double x, double y) const override;
    double noise3D(double x, double y, double z) const override;

    // Convenience: remap the (approximately [-1,1]) output into [0,1].
    static double toUnit(double n) { return n * 0.5 + 0.5; }

private:
    // 512-entry doubled permutation table (avoids per-lookup masking wrap).
    std::array<uint8_t, 512> p_{};

    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int hash, double x, double y, double z);
};

}  // namespace vg::noise
