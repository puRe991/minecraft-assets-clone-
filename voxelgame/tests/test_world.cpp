// Unit tests for chunk storage, coordinate math, and world streaming.
#include "TestFramework.hpp"
#include "vg/world/World.hpp"

#include <memory>

using namespace vg::world;

VG_TEST(coord_math_negative) {
    CHECK(floorDiv(-1, 16) == -1);
    CHECK(floorDiv(-16, 16) == -1);
    CHECK(floorDiv(-17, 16) == -2);
    CHECK(floorMod(-1, 16) == 15);
    CHECK(floorMod(-16, 16) == 0);
    CHECK((worldToChunk(-1, -1) == ChunkCoord{-1, -1}));
    CHECK((worldToChunk(0, 0) == ChunkCoord{0, 0}));
    CHECK((worldToChunk(31, 16) == ChunkCoord{1, 1}));
}

VG_TEST(chunk_local_get_set) {
    Chunk c({0, 0});
    CHECK(c.get(1, 2, 3) == blocks::Air);
    c.set(1, 2, 3, blocks::Stone);
    CHECK(c.get(1, 2, 3) == blocks::Stone);
    CHECK(c.dirty());
    // out of bounds is safe
    c.set(-1, 0, 0, blocks::Stone);
    CHECK(c.get(-1, 0, 0) == blocks::Air);
    CHECK(c.get(0, CHUNK_H, 0) == blocks::Air);
}

VG_TEST(world_block_access_across_chunks_and_negatives) {
    auto gen = std::make_shared<FlatChunkGenerator>(0);   // empty-ish
    World w(gen);
    w.ensureChunk(worldToChunk(-1, -1));
    w.ensureChunk(worldToChunk(20, 20));

    CHECK(w.setBlock(-1, 40, -1, blocks::OakLog));
    CHECK(w.getBlock(-1, 40, -1) == blocks::OakLog);
    CHECK(w.setBlock(20, 40, 20, blocks::Glass));
    CHECK(w.getBlock(20, 40, 20) == blocks::Glass);

    // writing to an unloaded column fails and reads return air
    CHECK(w.setBlock(9999, 40, 9999, blocks::Stone) == false);
    CHECK(w.getBlock(9999, 40, 9999) == blocks::Air);

    // y out of range
    CHECK(w.getBlock(-1, -5, -1) == blocks::Air);
    CHECK(w.setBlock(-1, CHUNK_H + 1, -1, blocks::Stone) == false);
}

VG_TEST(world_generator_fills_new_chunks) {
    auto gen = std::make_shared<FlatChunkGenerator>(32);
    World w(gen);
    Chunk& c = w.ensureChunk({0, 0});
    CHECK(c.generated());
    CHECK(!c.dirty());                          // generation isn't a player edit
    CHECK(w.getBlock(0, 32, 0) == blocks::Grass);
    CHECK(w.getBlock(0, 31, 0) == blocks::Dirt);
    CHECK(w.getBlock(0, 10, 0) == blocks::Stone);
    CHECK(w.getBlock(0, 40, 0) == blocks::Air);
}

VG_TEST(streaming_loads_and_unloads) {
    auto gen = std::make_shared<FlatChunkGenerator>(32);
    World w(gen);

    int gen0 = w.updateStreaming(0, 0, 3);
    CHECK(gen0 == 7 * 7);                        // (2*3+1)^2 chunks
    CHECK(w.loadedCount() == 49);
    CHECK(w.isLoaded({0, 0}));
    CHECK(w.isLoaded({3, 3}));
    CHECK(!w.isLoaded({4, 0}));

    // moving keeps the loaded set at the same size, drops far chunks
    int gen1 = w.updateStreaming(16 * 10, 0, 3);   // move +10 chunks in x
    CHECK(w.loadedCount() == 49);
    CHECK(gen1 == 49);                           // an entirely new region
    CHECK(!w.isLoaded({0, 0}));                  // old centre unloaded
    CHECK(w.isLoaded({10, 0}));

    // re-streaming the same centre generates nothing new
    CHECK(w.updateStreaming(16 * 10, 0, 3) == 0);
}
