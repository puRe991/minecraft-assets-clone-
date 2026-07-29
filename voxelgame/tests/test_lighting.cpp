// Unit tests for the light engine (Module 6).
#include "TestFramework.hpp"
#include "vg/render/LightEngine.hpp"

using namespace vg::world;
using namespace vg::render;

static BlockRegistry makeReg() { BlockRegistry r; registerDefaultBlocks(r); return r; }

VG_TEST(skylight_full_in_open_air) {
    auto reg = makeReg();
    Chunk c({0, 0});                          // all air
    LightEngine le(reg);
    LightGrid g;
    le.compute(c, g);
    CHECK(g.skyAt(8, CHUNK_H - 1, 8) == 15);
    CHECK(g.skyAt(8, 0, 8) == 15);            // reaches the bottom through air
}

VG_TEST(skylight_blocked_below_opaque) {
    auto reg = makeReg();
    Chunk c({0, 0});
    // a solid stone roof at y=40 over the whole chunk
    for (int x = 0; x < CHUNK_W; ++x)
        for (int z = 0; z < CHUNK_W; ++z)
            c.set(x, 40, z, blocks::Stone);
    LightEngine le(reg);
    LightGrid g;
    le.compute(c, g);
    CHECK(g.skyAt(8, 41, 8) == 15);           // above the roof: full sky
    CHECK(g.skyAt(8, 40, 8) == 0);            // the roof block itself
    CHECK(g.skyAt(8, 39, 8) == 0);            // directly under the roof: dark
}

VG_TEST(skylight_bleeds_sideways_under_overhang) {
    auto reg = makeReg();
    Chunk c({0, 0});
    // small roof covering only x in [4..8], leaving open sky beside it
    for (int x = 4; x <= 8; ++x)
        for (int z = 0; z < CHUNK_W; ++z)
            c.set(x, 40, z, blocks::Stone);
    LightEngine le(reg);
    LightGrid g;
    le.compute(c, g);
    // directly under the centre of the roof is dark-ish, but light bleeds in
    // from the open edge, so it should be > 0 (smooth lighting).
    CHECK(g.skyAt(6, 39, 8) > 0);
    CHECK(g.skyAt(6, 39, 8) < 15);
}

VG_TEST(block_light_from_torch_falls_off) {
    auto reg = makeReg();
    Chunk c({0, 0});
    c.set(0, 20, 0, blocks::Torch);           // emission 14, in a corner
    LightEngine le(reg);
    LightGrid g;
    le.compute(c, g);
    CHECK(g.blockAt(0, 20, 0) == 14);
    CHECK(g.blockAt(0, 20, 1) == 13);         // one step away
    CHECK(g.blockAt(0, 20, 5) == 14 - 5);     // five steps away
    CHECK(g.blockAt(0, 20, 14) == 0);         // 14 steps: beyond range (still in-bounds)
}

VG_TEST(light_never_out_of_range) {
    auto reg = makeReg();
    Chunk c({0, 0});
    c.set(2, 10, 2, blocks::Lava);            // emission 15
    LightEngine le(reg);
    LightGrid g;
    le.compute(c, g);
    for (size_t i = 0; i < g.sky.size(); ++i) {
        CHECK(g.sky[i] <= 15);
        CHECK(g.block[i] <= 15);
    }
    CHECK(g.combined(2, 10, 2, 1.0f) == 15);
    CHECK(g.combined(8, 0, 8, 0.0f) == 0);    // night, no block light -> dark
}
