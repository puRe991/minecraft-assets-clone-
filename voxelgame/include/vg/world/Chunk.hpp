// vg/world/Chunk.hpp
//
// A Chunk is a fixed-size column of the world: CHUNK_W x CHUNK_W blocks
// horizontally and CHUNK_H tall. It is *pure storage* — it knows how to hold
// and address blocks, nothing about generation, meshing or rendering (SRP).
// Larger systems (World, generators, mesher) operate on chunks.
#pragma once

#include <cstdint>
#include <vector>

#include "vg/world/BlockTypes.hpp"

namespace vg::world {

inline constexpr int CHUNK_W = 16;    // width & depth in blocks
inline constexpr int CHUNK_H = 128;   // height in blocks
inline constexpr int CHUNK_VOL = CHUNK_W * CHUNK_W * CHUNK_H;

// Horizontal chunk coordinate (one per column of the world).
struct ChunkCoord {
    int x{0}, z{0};
    bool operator==(const ChunkCoord& o) const { return x == o.x && z == o.z; }
};

class Chunk {
public:
    explicit Chunk(ChunkCoord pos) : pos_(pos), blocks_(CHUNK_VOL, blocks::Air) {}

    ChunkCoord pos() const { return pos_; }

    // Local coordinates are 0..CHUNK_W-1 (x,z) and 0..CHUNK_H-1 (y).
    static bool inBounds(int lx, int ly, int lz) {
        return lx >= 0 && lx < CHUNK_W && lz >= 0 && lz < CHUNK_W && ly >= 0 && ly < CHUNK_H;
    }
    static int index(int lx, int ly, int lz) { return (ly * CHUNK_W + lz) * CHUNK_W + lx; }

    BlockId get(int lx, int ly, int lz) const {
        if (!inBounds(lx, ly, lz)) return blocks::Air;
        return blocks_[index(lx, ly, lz)];
    }
    void set(int lx, int ly, int lz, BlockId id) {
        if (!inBounds(lx, ly, lz)) return;
        blocks_[index(lx, ly, lz)] = id;
        dirty_ = true;
    }

    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    bool generated() const { return generated_; }
    void markGenerated() { generated_ = true; }

    // Raw access for fast generation / serialization.
    std::vector<BlockId>& data() { return blocks_; }
    const std::vector<BlockId>& data() const { return blocks_; }

private:
    ChunkCoord pos_;
    std::vector<BlockId> blocks_;
    bool dirty_{false};
    bool generated_{false};
};

}  // namespace vg::world

namespace std {
template <> struct hash<vg::world::ChunkCoord> {
    size_t operator()(const vg::world::ChunkCoord& c) const noexcept {
        // pack two ints and mix
        uint64_t h = (uint64_t)(uint32_t)c.x << 32 | (uint32_t)c.z;
        h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33;
        return (size_t)h;
    }
};
}  // namespace std
