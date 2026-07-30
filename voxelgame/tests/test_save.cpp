// Unit tests for persistence: chunk codec, world save/load, autosave (Module 10).
#include "TestFramework.hpp"
#include "vg/save/ChunkCodec.hpp"
#include "vg/save/WorldSaver.hpp"
#include "vg/world/ChunkGenerator.hpp"
#include "vg/world/TerrainGenerator.hpp"

#include <memory>
#include <string>

using namespace vg::world;
using namespace vg::save;

static std::string tmpDir(const char* name) {
    return std::string("/tmp/claude-0/-home-user-minecraft-assets-clone-/"
                       "3dbf0045-846a-5245-96cd-022269b286fa/scratchpad/") + name;
}

VG_TEST(chunk_codec_roundtrip) {
    Chunk c({3, -5});
    c.set(1, 2, 3, blocks::Stone);
    c.set(0, 0, 0, blocks::Bedrock);
    c.set(15, 127, 15, blocks::Glass);
    auto bytes = serializeChunk(c);

    Chunk back({3, -5});
    CHECK(deserializeChunk(bytes, back));
    CHECK(back.get(1, 2, 3) == blocks::Stone);
    CHECK(back.get(0, 0, 0) == blocks::Bedrock);
    CHECK(back.get(15, 127, 15) == blocks::Glass);
    CHECK(back.get(8, 8, 8) == blocks::Air);
    CHECK(!back.dirty());
}

VG_TEST(rle_actually_compresses) {
    // an almost-empty chunk (mostly air) should serialize far smaller than raw
    Chunk c({0, 0});
    c.set(0, 0, 0, blocks::Stone);
    auto bytes = serializeChunk(c);
    size_t raw = (size_t)CHUNK_VOL * sizeof(BlockId);
    CHECK(bytes.size() < raw / 10);
}

VG_TEST(bad_buffer_rejected) {
    Chunk c({0, 0});
    std::vector<uint8_t> junk = {'X', 'X', 'X', 'X', 1, 2, 3};
    CHECK(!deserializeChunk(junk, c));
}

VG_TEST(world_save_and_reload_matches) {
    auto gen = std::make_shared<TerrainGenerator>(TerrainConfig{2024, 40});
    World w(gen);
    w.updateStreaming(0, 0, 1);
    // make a player edit so a chunk is dirty
    w.setBlock(2, 60, 2, blocks::Glass);

    WorldSaver saver(tmpDir("save_test_world"));
    int wrote = saver.saveDirty(w);
    CHECK(wrote >= 1);
    CHECK(saver.saveMeta({2024, 2.5, 60.0, 2.5}));

    // fresh world with the SAME generator; overwrite its chunks from disk
    World w2(gen);
    w2.updateStreaming(0, 0, 1);
    auto coords = w2.loadedCoords();
    int loaded = saver.loadInto(w2, coords);
    CHECK(loaded >= 1);
    CHECK(w2.getBlock(2, 60, 2) == blocks::Glass);   // our edit survived

    WorldMeta m;
    CHECK(saver.loadMeta(m));
    CHECK(m.seed == 2024);
    CHECK(m.py == 60.0);
}

VG_TEST(saving_clears_dirty) {
    auto gen = std::make_shared<FlatChunkGenerator>(32);
    World w(gen);
    w.ensureChunk({0, 0});
    w.setBlock(1, 40, 1, blocks::Stone);             // dirties the chunk
    CHECK(w.mutableChunk({0, 0})->dirty());
    WorldSaver saver(tmpDir("save_test_dirty"));
    CHECK(saver.saveDirty(w) == 1);
    CHECK(!w.mutableChunk({0, 0})->dirty());          // flag cleared
    CHECK(saver.saveDirty(w) == 0);                   // nothing left to save
}

VG_TEST(autosaver_fires_on_interval) {
    AutoSaver as(5.0);
    int saves = 0;
    // 0.5 is exact in binary, 44 ticks = 22.0s -> fires at 5,10,15,20 = 4
    for (int i = 0; i < 44; ++i) if (as.tick(0.5)) ++saves;
    CHECK(saves == 4);
}
