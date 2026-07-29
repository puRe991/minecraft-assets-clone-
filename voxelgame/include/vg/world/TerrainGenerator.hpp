// vg/world/TerrainGenerator.hpp
//
// The real world generator: an IChunkGenerator that produces biome-based
// terrain with oceans and rivers, carved caves, ore veins, trees and simple
// structures — all deterministically from a single seed, and independently per
// chunk (a chunk's contents depend only on its coordinates + the seed, so
// generation order never matters).
//
// It depends on the INoise abstraction from Module 1 (DIP): the noise sources
// are injected/created in the constructor and could be swapped for OpenSimplex
// without touching the generation logic.
#pragma once

#include <memory>

#include "vg/noise/INoise.hpp"
#include "vg/world/Biome.hpp"
#include "vg/world/ChunkGenerator.hpp"

namespace vg::world {

struct TerrainConfig {
    uint32_t seed{2024};
    int seaLevel{40};
};

class TerrainGenerator final : public IChunkGenerator {
public:
    explicit TerrainGenerator(TerrainConfig cfg = {});

    void generate(Chunk& chunk) const override;

    // Exposed for tests / gameplay: describe a column without generating it.
    struct Column { int height; Biome biome; };
    Column columnAt(int wx, int wz) const;

private:
    TerrainConfig cfg_;
    std::shared_ptr<noise::INoise> height_;   // base elevation
    std::shared_ptr<noise::INoise> temp_;     // biome temperature
    std::shared_ptr<noise::INoise> humid_;    // biome humidity
    std::shared_ptr<noise::INoise> mountain_; // extra elevation for peaks
    std::shared_ptr<noise::INoise> river_;    // river channels
    std::shared_ptr<noise::INoise> cave_;     // 3D cave carving

    // helpers
    void placeTree(Chunk& c, int lx, int surfaceY, int lz, uint32_t worldSeedMix) const;
    void carveAndOre(Chunk& c) const;
};

}  // namespace vg::world
