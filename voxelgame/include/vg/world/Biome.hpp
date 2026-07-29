// vg/world/Biome.hpp
//
// Biome classification. A biome is chosen per column from temperature and
// humidity noise (plus elevation), and it drives surface blocks, tree density
// and terrain shaping. Kept as a small enum + helpers so the terrain generator
// and (later) rendering/ambience can share one definition.
#pragma once

#include <cstdint>

namespace vg::world {

enum class Biome : uint8_t { Ocean, Plains, Forest, Desert, Mountains, Snowy };

inline const char* biomeName(Biome b) {
    switch (b) {
        case Biome::Ocean:     return "ocean";
        case Biome::Plains:    return "plains";
        case Biome::Forest:    return "forest";
        case Biome::Desert:    return "desert";
        case Biome::Mountains: return "mountains";
        case Biome::Snowy:     return "snowy";
    }
    return "?";
}

// Tree spawn probability per surface column for a biome (0 = none).
inline float biomeTreeDensity(Biome b) {
    switch (b) {
        case Biome::Forest: return 0.10f;
        case Biome::Plains: return 0.02f;
        case Biome::Snowy:  return 0.03f;
        default:            return 0.0f;   // desert, ocean, bare mountains
    }
}

}  // namespace vg::world
