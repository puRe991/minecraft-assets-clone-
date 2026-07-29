// vg/world/World.cpp — see World.hpp for the contract.
#include "vg/world/World.hpp"

namespace vg::world {

Chunk& World::ensureChunk(ChunkCoord c) {
    auto it = chunks_.find(c);
    if (it != chunks_.end()) return *it->second;

    auto chunk = std::make_unique<Chunk>(c);
    gen_->generate(*chunk);
    chunk->markGenerated();
    chunk->clearDirty();                 // freshly generated == not player-modified
    Chunk& ref = *chunk;
    chunks_.emplace(c, std::move(chunk));
    return ref;
}

BlockId World::getBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= CHUNK_H) return blocks::Air;
    auto it = chunks_.find(worldToChunk(wx, wz));
    if (it == chunks_.end()) return blocks::Air;
    return it->second->get(floorMod(wx, CHUNK_W), wy, floorMod(wz, CHUNK_W));
}

bool World::setBlock(int wx, int wy, int wz, BlockId id) {
    if (wy < 0 || wy >= CHUNK_H) return false;
    auto it = chunks_.find(worldToChunk(wx, wz));
    if (it == chunks_.end()) return false;   // don't silently generate on write
    it->second->set(floorMod(wx, CHUNK_W), wy, floorMod(wz, CHUNK_W), id);
    return true;
}

int World::updateStreaming(int centerWx, int centerWz, int radius) {
    ChunkCoord center = worldToChunk(centerWx, centerWz);

    // 1) load the disc of chunks within `radius` (Chebyshev / square region)
    int generated = 0;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx) {
            ChunkCoord c{center.x + dx, center.z + dz};
            if (!isLoaded(c)) { ensureChunk(c); ++generated; }
        }

    // 2) unload chunks outside the radius
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        const ChunkCoord& c = it->first;
        if (std::abs(c.x - center.x) > radius || std::abs(c.z - center.z) > radius)
            it = chunks_.erase(it);
        else
            ++it;
    }
    return generated;
}

}  // namespace vg::world
