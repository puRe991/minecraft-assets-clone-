// vg/noise/PerlinNoise.cpp  — see PerlinNoise.hpp for the interface contract.
#include "vg/noise/PerlinNoise.hpp"

#include <numeric>
#include <random>

namespace vg::noise {

PerlinNoise::PerlinNoise(uint32_t seed) {
    // Start with the identity permutation 0..255, shuffle it deterministically
    // with a seeded Mersenne Twister, then duplicate it into the second half so
    // index arithmetic never needs a modulo.
    std::array<uint8_t, 256> perm{};
    std::iota(perm.begin(), perm.end(), 0);

    std::mt19937 rng(seed);
    for (int i = 255; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        std::swap(perm[i], perm[dist(rng)]);
    }
    for (int i = 0; i < 512; ++i) p_[i] = perm[i & 255];
}

// Gradient: pick one of 12 edge vectors of a cube from the low 4 hash bits.
double PerlinNoise::grad(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double PerlinNoise::noise3D(double x, double y, double z) const {
    // Unit cube that contains the point.
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;

    // Relative position inside the cube.
    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    // Fade curves for each axis.
    double u = fade(x), v = fade(y), w = fade(z);

    // Hash the 8 cube corners.
    int A = p_[X] + Y, AA = p_[A] + Z, AB = p_[A + 1] + Z;
    int B = p_[X + 1] + Y, BA = p_[B] + Z, BB = p_[B + 1] + Z;

    // Trilinear blend of the 8 corner gradients.
    return lerp(w,
        lerp(v, lerp(u, grad(p_[AA], x, y, z),
                        grad(p_[BA], x - 1, y, z)),
                lerp(u, grad(p_[AB], x, y - 1, z),
                        grad(p_[BB], x - 1, y - 1, z))),
        lerp(v, lerp(u, grad(p_[AA + 1], x, y, z - 1),
                        grad(p_[BA + 1], x - 1, y, z - 1)),
                lerp(u, grad(p_[AB + 1], x, y - 1, z - 1),
                        grad(p_[BB + 1], x - 1, y - 1, z - 1))));
}

double PerlinNoise::noise2D(double x, double y) const {
    // 2D noise is the 3D field sampled on the z = 0 plane.
    return noise3D(x, y, 0.0);
}

}  // namespace vg::noise
