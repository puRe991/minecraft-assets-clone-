// vg/noise/INoise.hpp
//
// Abstraction for coherent-noise sources. Terrain, caves, biome maps and
// vegetation placement all depend on *this interface*, never on a concrete
// noise class (Dependency Inversion). That lets us swap Perlin for
// OpenSimplex, or wrap a source in a fractal/fBm decorator, without touching
// any generation code.
#pragma once

namespace vg::noise {

class INoise {
public:
    virtual ~INoise() = default;

    // 2D coherent noise. Range is implementation-defined but documented per
    // implementation; PerlinNoise returns roughly [-1, 1].
    virtual double noise2D(double x, double y) const = 0;

    // 3D coherent noise (used for caves and 3D density fields).
    virtual double noise3D(double x, double y, double z) const = 0;
};

}  // namespace vg::noise
