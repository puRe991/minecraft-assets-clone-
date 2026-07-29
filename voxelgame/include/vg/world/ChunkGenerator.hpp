// vg/world/ChunkGenerator.hpp
//
// Abstraction that fills a freshly-created chunk with blocks. The World depends
// on this interface, not on any concrete terrain generator (DIP) — Module 4's
// biome/cave/ore generator will implement it, and tests use a trivial flat
// generator. This keeps world storage/streaming completely independent of how
// terrain is produced.
#pragma once

#include "vg/world/Chunk.hpp"

namespace vg::world {

class IChunkGenerator {
public:
    virtual ~IChunkGenerator() = default;
    // Fill `chunk` (already positioned) with blocks.
    virtual void generate(Chunk& chunk) const = 0;
};

// Minimal generator: solid ground up to `groundHeight`, grass on top, air
// above. Useful as a default and for unit tests.
class FlatChunkGenerator final : public IChunkGenerator {
public:
    explicit FlatChunkGenerator(int groundHeight = 32) : ground_(groundHeight) {}
    void generate(Chunk& chunk) const override {
        for (int x = 0; x < CHUNK_W; ++x)
            for (int z = 0; z < CHUNK_W; ++z)
                for (int y = 0; y <= ground_ && y < CHUNK_H; ++y) {
                    BlockId b = (y == ground_) ? blocks::Grass
                              : (y >= ground_ - 3) ? blocks::Dirt : blocks::Stone;
                    chunk.set(x, y, z, b);
                }
    }
private:
    int ground_;
};

}  // namespace vg::world
