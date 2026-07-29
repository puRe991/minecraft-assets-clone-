// Unit tests for greedy meshing (Module 6).
#include "TestFramework.hpp"
#include "vg/render/Mesher.hpp"

using namespace vg::world;
using namespace vg::render;

static BlockRegistry makeReg() { BlockRegistry r; registerDefaultBlocks(r); return r; }

VG_TEST(single_block_has_six_faces) {
    auto reg = makeReg();
    Chunk c({0, 0});
    c.set(5, 5, 5, blocks::Stone);
    Mesher m(reg);
    Mesh mesh = m.build(c);
    CHECK(mesh.size() == 6);
    for (auto& q : mesh) { CHECK(q.w == 1); CHECK(q.h == 1); CHECK(q.block == blocks::Stone); }
}

VG_TEST(solid_cube_merges_each_side_to_one_quad) {
    auto reg = makeReg();
    Chunk c({0, 0});
    // a 3x3x3 solid block of stone
    for (int x = 2; x < 5; ++x)
        for (int y = 2; y < 5; ++y)
            for (int z = 2; z < 5; ++z)
                c.set(x, y, z, blocks::Stone);
    Mesher m(reg);
    Mesh mesh = m.build(c);
    CHECK(mesh.size() == 6);                  // greedy merges each face
    for (auto& q : mesh) { CHECK(q.w == 3); CHECK(q.h == 3); }   // 3x3 faces
}

VG_TEST(no_interior_faces) {
    auto reg = makeReg();
    Chunk c({0, 0});
    // two stone blocks side by side: the shared face must not be emitted, and
    // the coplanar top/bottom/side faces greedily merge.
    c.set(1, 1, 1, blocks::Stone);
    c.set(2, 1, 1, blocks::Stone);
    Mesher m(reg);
    Mesh mesh = m.build(c);
    // exposed surface area of a 2x1x1 box = 10 unit faces, merged into 6 quads.
    int area = 0;
    for (auto& q : mesh) area += q.w * q.h;
    CHECK(area == 10);          // no interior face leaked in
    CHECK(mesh.size() == 6);    // greedy merging
}

VG_TEST(transparent_neighbour_does_not_occlude) {
    auto reg = makeReg();
    Chunk c({0, 0});
    c.set(3, 3, 3, blocks::Stone);
    c.set(4, 3, 3, blocks::Glass);   // cutout: does NOT fully occlude the stone
    Mesher m(reg);
    Mesh mesh = m.build(c);
    // the stone still shows all 6 faces (glass doesn't hide the +x face);
    // glass also contributes its own faces. Just assert the stone's +x face exists.
    bool stonePlusX = false;
    for (auto& q : mesh)
        if (q.block == blocks::Stone && q.face == 0 /*+X*/ && q.x == 3) stonePlusX = true;
    CHECK(stonePlusX);
}

VG_TEST(full_chunk_layer_merges) {
    auto reg = makeReg();
    Chunk c({0, 0});
    // fill the whole bottom layer with stone
    for (int x = 0; x < CHUNK_W; ++x)
        for (int z = 0; z < CHUNK_W; ++z)
            c.set(x, 0, z, blocks::Stone);
    Mesher m(reg);
    Mesh mesh = m.build(c);
    // top (+Y) and bottom (-Y) faces each merge to one 16x16 quad; the four
    // side strips merge to one quad per side. So: 2 (top/bottom) + 4 sides = 6.
    CHECK(mesh.size() == 6);
    for (auto& q : mesh)
        if (q.face == 2 /*+Y*/ ) { CHECK(q.w == CHUNK_W); CHECK(q.h == CHUNK_W); }
}
