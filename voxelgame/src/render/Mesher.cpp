// vg/render/Mesher.cpp — see Mesher.hpp for the contract.
#include "vg/render/Mesher.hpp"

namespace vg::render {

using world::BlockId;
using world::CHUNK_H;
using world::CHUNK_W;
using world::Chunk;
namespace blocks = world::blocks;

namespace {
// Out-of-chunk reads are air.
BlockId at(const Chunk& c, int x, int y, int z) {
    if (!Chunk::inBounds(x, y, z)) return blocks::Air;
    return c.get(x, y, z);
}
}  // namespace

Mesh Mesher::build(const Chunk& chunk) const {
    Mesh mesh;

    // Does a block render a solid face at all? (air/fluids don't here)
    auto rendersFace = [&](BlockId id) {
        if (id == blocks::Air) return false;
        const auto& b = reg_.get(id);
        return b.solid && !b.fluid;
    };
    // Does the neighbour fully hide the face behind it?
    auto occludes = [&](BlockId id) { return reg_.get(id).opaque(); };

    const int dim[3] = {CHUNK_W, CHUNK_H, CHUNK_W};

    // For each axis and both facing directions, sweep layer by layer, build a
    // 2D mask of exposed faces, and greedily merge equal-block rectangles.
    for (int a = 0; a < 3; ++a) {
        int p = (a + 1) % 3, qd = (a + 2) % 3;
        int Da = dim[a], Dp = dim[p], Dq = dim[qd];

        for (int sign = -1; sign <= 1; sign += 2) {
            int face = a * 2 + (sign > 0 ? 0 : 1);

            for (int la = 0; la < Da; ++la) {
                std::vector<BlockId> mask((size_t)Dp * Dq, 0);
                for (int lq = 0; lq < Dq; ++lq)
                    for (int lp = 0; lp < Dp; ++lp) {
                        int cell[3]; cell[a] = la; cell[p] = lp; cell[qd] = lq;
                        BlockId here = at(chunk, cell[0], cell[1], cell[2]);
                        if (!rendersFace(here)) continue;
                        int nb[3] = {cell[0], cell[1], cell[2]}; nb[a] += sign;
                        BlockId neighbour = at(chunk, nb[0], nb[1], nb[2]);
                        if (occludes(neighbour)) continue;            // hidden
                        mask[(size_t)lq * Dp + lp] = here;
                    }

                // greedy-merge rectangles of equal block id
                for (int j = 0; j < Dq; ++j)
                    for (int i = 0; i < Dp;) {
                        BlockId v = mask[(size_t)j * Dp + i];
                        if (v == 0) { ++i; continue; }
                        int w = 1;
                        while (i + w < Dp && mask[(size_t)j * Dp + i + w] == v) ++w;
                        int h = 1;
                        bool grow = true;
                        while (j + h < Dq && grow) {
                            for (int k = 0; k < w; ++k)
                                if (mask[(size_t)(j + h) * Dp + i + k] != v) { grow = false; break; }
                            if (grow) ++h;
                        }
                        for (int dj = 0; dj < h; ++dj)
                            for (int di = 0; di < w; ++di)
                                mask[(size_t)(j + dj) * Dp + i + di] = 0;

                        int corner[3]; corner[a] = la; corner[p] = i; corner[qd] = j;
                        mesh.push_back({corner[0], corner[1], corner[2], w, h, face, v});
                        i += w;
                    }
            }
        }
    }
    return mesh;
}

}  // namespace vg::render
