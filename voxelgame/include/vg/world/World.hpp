// vg/world/World.hpp
//
// The World owns the set of currently-loaded chunks and presents a flat
// world-space block API on top of them. It streams chunks in and out around a
// moving centre so the world is effectively infinite in X/Z. Terrain content
// comes from an injected IChunkGenerator (DIP) — the World knows nothing about
// biomes or caves.
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "vg/world/Chunk.hpp"
#include "vg/world/ChunkGenerator.hpp"

namespace vg::world {

// Floor-division / positive-modulo helpers (correct for negative coordinates).
inline int floorDiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) --q; return q; }
inline int floorMod(int a, int b) { int r = a % b; if (r < 0) r += b; return r; }

inline ChunkCoord worldToChunk(int wx, int wz) { return {floorDiv(wx, CHUNK_W), floorDiv(wz, CHUNK_W)}; }

class World {
public:
    explicit World(std::shared_ptr<IChunkGenerator> gen) : gen_(std::move(gen)) {}

    // Block access in world coordinates. Reads from an unloaded column return
    // air; writes to an unloaded column are ignored (load it first).
    BlockId getBlock(int wx, int wy, int wz) const;
    bool    setBlock(int wx, int wy, int wz, BlockId id);

    // Ensure the chunk at (cx,cz) exists (generating it on first access).
    Chunk& ensureChunk(ChunkCoord c);
    bool   isLoaded(ChunkCoord c) const { return chunks_.count(c) != 0; }
    size_t loadedCount() const { return chunks_.size(); }

    // Stream chunks around a world-space centre: load everything within
    // `radius` chunks (Chebyshev distance) and unload everything outside it.
    // Returns the number of chunks newly generated this call.
    int updateStreaming(int centerWx, int centerWz, int radius);

    const Chunk* find(ChunkCoord c) const {
        auto it = chunks_.find(c);
        return it == chunks_.end() ? nullptr : it->second.get();
    }
    Chunk* mutableChunk(ChunkCoord c) {
        auto it = chunks_.find(c);
        return it == chunks_.end() ? nullptr : it->second.get();
    }

    // Coordinates of all loaded chunks (for saving/iteration).
    std::vector<ChunkCoord> loadedCoords() const {
        std::vector<ChunkCoord> v; v.reserve(chunks_.size());
        for (const auto& kv : chunks_) v.push_back(kv.first);
        return v;
    }

    // Insert (or replace) a chunk that was loaded from disk.
    Chunk& putChunk(std::unique_ptr<Chunk> c) {
        ChunkCoord pos = c->pos();
        Chunk& ref = *c;
        chunks_[pos] = std::move(c);
        return ref;
    }

private:
    std::shared_ptr<IChunkGenerator> gen_;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks_;
};

}  // namespace vg::world
