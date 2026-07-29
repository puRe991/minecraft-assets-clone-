// vg/render/LightEngine.hpp
//
// Per-block lighting for a chunk: a skylight channel (sunlight coming down and
// spreading into shade) and a block-light channel (torches, lava). Both are
// 0..15 and propagate with a breadth-first flood fill that loses one level per
// step (more through translucent blocks), which gives soft, wrap-around
// lighting — the basis for "smooth lighting".
//
// Computed per chunk and self-contained (borders treated as open sky / no
// neighbour light) so it is deterministic and unit-testable in isolation.
#pragma once

#include <cstdint>
#include <vector>

#include "vg/world/BlockRegistry.hpp"
#include "vg/world/Chunk.hpp"

namespace vg::render {

using world::BlockRegistry;
using world::Chunk;

struct LightGrid {
    std::vector<uint8_t> sky;    // 0..15 sunlight
    std::vector<uint8_t> block;  // 0..15 block-source light
    LightGrid() : sky(world::CHUNK_VOL, 0), block(world::CHUNK_VOL, 0) {}

    uint8_t skyAt(int x, int y, int z)   const { return sky[Chunk::index(x, y, z)]; }
    uint8_t blockAt(int x, int y, int z) const { return block[Chunk::index(x, y, z)]; }

    // Combined perceived brightness, with sky scaled by time-of-day (0..1).
    uint8_t combined(int x, int y, int z, float daylight = 1.0f) const {
        int s = (int)(skyAt(x, y, z) * daylight);
        int b = blockAt(x, y, z);
        return (uint8_t)(s > b ? s : b);
    }
};

class LightEngine {
public:
    explicit LightEngine(const BlockRegistry& reg) : reg_(reg) {}

    // Fill `out` with sky + block light for `chunk`.
    void compute(const Chunk& chunk, LightGrid& out) const;

private:
    const BlockRegistry& reg_;
    int opacity(const Chunk& c, int x, int y, int z) const {
        return reg_.get(c.get(x, y, z)).lightOpacity;
    }
};

}  // namespace vg::render
