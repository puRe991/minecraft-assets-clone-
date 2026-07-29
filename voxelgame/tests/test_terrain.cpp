// Unit tests for the terrain generator (Module 4).
#include "TestFramework.hpp"
#include "vg/world/TerrainGenerator.hpp"
#include "vg/world/World.hpp"

#include <map>
#include <memory>

using namespace vg::world;
using Column = TerrainGenerator::Column;

// Count blocks of interest over a region of generated world.
struct Stats {
    long solid = 0, air = 0, water = 0, ores = 0, logs = 0, leaves = 0, caveAir = 0;
    std::map<Biome, int> biomes;
};

static Stats scan(World& w, const TerrainGenerator& gen, int chunksR) {
    Stats s;
    for (int cx = -chunksR; cx <= chunksR; ++cx)
        for (int cz = -chunksR; cz <= chunksR; ++cz) {
            const Chunk* c = w.find({cx, cz});
            if (!c) continue;
            for (int lx = 0; lx < CHUNK_W; ++lx)
                for (int lz = 0; lz < CHUNK_W; ++lz) {
                    int wx = cx * CHUNK_W + lx, wz = cz * CHUNK_W + lz;
                    Column col = gen.columnAt(wx, wz);
                    s.biomes[col.biome]++;
                    for (int y = 0; y < CHUNK_H; ++y) {
                        BlockId b = c->get(lx, y, lz);
                        switch (b) {
                            case blocks::Air: s.air++; if (y < col.height) s.caveAir++; break;
                            case blocks::Water: s.water++; break;
                            case blocks::OakLog: s.logs++; break;
                            case blocks::OakLeaves: s.leaves++; break;
                            case blocks::CoalOre: case blocks::IronOre:
                            case blocks::GoldOre: case blocks::DiamondOre: s.ores++; s.solid++; break;
                            default: if (b != blocks::Air) s.solid++; break;
                        }
                    }
                }
        }
    return s;
}

VG_TEST(terrain_deterministic) {
    auto g = std::make_shared<TerrainGenerator>(TerrainConfig{777, 40});
    World a(g), b(g);
    a.ensureChunk({3, -2});
    b.ensureChunk({3, -2});
    for (int lx = 0; lx < CHUNK_W; ++lx)
        for (int lz = 0; lz < CHUNK_W; ++lz)
            for (int y = 0; y < CHUNK_H; y += 3)
                CHECK(a.find({3, -2})->get(lx, y, lz) == b.find({3, -2})->get(lx, y, lz));
}

VG_TEST(terrain_has_ground_and_sky) {
    auto g = std::make_shared<TerrainGenerator>(TerrainConfig{5, 40});
    World w(g);
    w.updateStreaming(0, 0, 2);
    // bedrock floor, air near the top of the world
    CHECK(w.getBlock(0, 0, 0) == blocks::Bedrock);
    CHECK(w.getBlock(0, CHUNK_H - 1, 0) == blocks::Air);
    // the surface is solid-or-water somewhere reasonable
    Column col = g->columnAt(0, 0);
    CHECK(col.height > 0 && col.height < CHUNK_H);
}

VG_TEST(terrain_surface_matches_biome) {
    auto g = std::make_shared<TerrainGenerator>(TerrainConfig{123, 40});
    World w(g);
    w.updateStreaming(0, 0, 6);
    Stats s = scan(w, *g, 6);
    // more than one biome should appear across a large area
    CHECK(s.biomes.size() >= 3);
    CHECK(s.solid > 0 && s.water > 0);   // land and water both exist
}

VG_TEST(terrain_has_caves) {
    auto g = std::make_shared<TerrainGenerator>(TerrainConfig{9, 40});
    World w(g);
    w.updateStreaming(0, 0, 4);
    Stats s = scan(w, *g, 4);
    CHECK(s.caveAir > 0);                 // some air exists below the surface
}

VG_TEST(terrain_has_ores_in_stone) {
    auto g = std::make_shared<TerrainGenerator>(TerrainConfig{17, 40});
    World w(g);
    w.updateStreaming(0, 0, 4);
    Stats s = scan(w, *g, 4);
    CHECK(s.ores > 0);
    // deep diamonds should only occur low; verify none appear above y=14
    for (int cx = -4; cx <= 4; ++cx)
        for (int cz = -4; cz <= 4; ++cz) {
            const Chunk* c = w.find({cx, cz});
            if (!c) continue;
            for (int lx = 0; lx < CHUNK_W; ++lx)
                for (int lz = 0; lz < CHUNK_W; ++lz)
                    for (int y = 14; y < CHUNK_H; ++y)
                        CHECK(c->get(lx, y, lz) != blocks::DiamondOre);
        }
}

VG_TEST(terrain_trees_in_forest_not_desert) {
    // Search for a forest and a desert column, then confirm tree behaviour.
    auto g = std::make_shared<TerrainGenerator>(TerrainConfig{2024, 40});
    World w(g);
    w.updateStreaming(0, 0, 8);
    Stats s = scan(w, *g, 8);
    CHECK(s.logs > 0);       // trees were generated somewhere
    CHECK(s.leaves > 0);

    // no logs should sit on desert sand surfaces
    for (int cx = -8; cx <= 8; ++cx)
        for (int cz = -8; cz <= 8; ++cz) {
            const Chunk* c = w.find({cx, cz});
            if (!c) continue;
            for (int lx = 0; lx < CHUNK_W; ++lx)
                for (int lz = 0; lz < CHUNK_W; ++lz) {
                    Column col = g->columnAt(cx * CHUNK_W + lx, cz * CHUNK_W + lz);
                    if (col.biome == Biome::Desert)
                        CHECK(c->get(lx, col.height + 1, lz) != blocks::OakLog);
                }
        }
}
