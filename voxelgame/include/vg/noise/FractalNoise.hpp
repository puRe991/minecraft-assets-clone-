// vg/noise/FractalNoise.hpp
//
// Fractional Brownian motion (fBm): sums several octaves of a base noise at
// increasing frequency and decreasing amplitude to produce natural-looking
// detail. Implemented as a *decorator* over INoise (Open/Closed principle):
// it adds behaviour to any noise source without modifying it, and is itself an
// INoise so it can be layered or swapped like any other source.
#pragma once

#include <memory>

#include "vg/noise/INoise.hpp"

namespace vg::noise {

struct FractalParams {
    int    octaves     = 4;      // number of layers
    double frequency   = 1.0;    // starting frequency
    double lacunarity  = 2.0;    // frequency multiplier per octave
    double persistence = 0.5;    // amplitude multiplier per octave
};

class FractalNoise final : public INoise {
public:
    FractalNoise(std::shared_ptr<INoise> source, FractalParams params)
        : source_(std::move(source)), p_(params) {}

    double noise2D(double x, double y) const override {
        double sum = 0, amp = 1, freq = p_.frequency, norm = 0;
        for (int o = 0; o < p_.octaves; ++o) {
            sum  += amp * source_->noise2D(x * freq, y * freq);
            norm += amp;
            amp  *= p_.persistence;
            freq *= p_.lacunarity;
        }
        return norm > 0 ? sum / norm : 0.0;   // normalised back to source range
    }

    double noise3D(double x, double y, double z) const override {
        double sum = 0, amp = 1, freq = p_.frequency, norm = 0;
        for (int o = 0; o < p_.octaves; ++o) {
            sum  += amp * source_->noise3D(x * freq, y * freq, z * freq);
            norm += amp;
            amp  *= p_.persistence;
            freq *= p_.lacunarity;
        }
        return norm > 0 ? sum / norm : 0.0;
    }

private:
    std::shared_ptr<INoise> source_;
    FractalParams p_;
};

}  // namespace vg::noise
