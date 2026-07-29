// vg/render/LightEngine.cpp — see LightEngine.hpp for the contract.
#include "vg/render/LightEngine.hpp"

#include <queue>

namespace vg::render {

using world::CHUNK_H;
using world::CHUNK_W;

namespace {
struct Node { int x, y, z; };
}  // namespace

void LightEngine::compute(const Chunk& chunk, LightGrid& out) const {
    std::fill(out.sky.begin(), out.sky.end(), 0);
    std::fill(out.block.begin(), out.block.end(), 0);

    std::queue<Node> q;

    // --- skylight: cast straight down each column, dimming through translucency
    for (int x = 0; x < CHUNK_W; ++x)
        for (int z = 0; z < CHUNK_W; ++z) {
            int cur = 15;
            for (int y = CHUNK_H - 1; y >= 0; --y) {
                int opac = opacity(chunk, x, y, z);
                if (opac >= 15) cur = 0;                       // opaque: full shadow below
                else if (opac > 0) cur = std::max(0, cur - opac);
                out.sky[Chunk::index(x, y, z)] = (uint8_t)cur;
                if (cur > 1) q.push({x, y, z});                // seed for horizontal spread
            }
        }
    // spread skylight into shade (BFS, -1 per step, more through translucency)
    auto flood = [&](std::vector<uint8_t>& grid, std::queue<Node>& queue) {
        const int dx[6] = {1, -1, 0, 0, 0, 0};
        const int dy[6] = {0, 0, 1, -1, 0, 0};
        const int dz[6] = {0, 0, 0, 0, 1, -1};
        while (!queue.empty()) {
            Node n = queue.front(); queue.pop();
            int level = grid[Chunk::index(n.x, n.y, n.z)];
            if (level <= 1) continue;
            for (int i = 0; i < 6; ++i) {
                int nx = n.x + dx[i], ny = n.y + dy[i], nz = n.z + dz[i];
                if (!Chunk::inBounds(nx, ny, nz)) continue;
                int opac = opacity(chunk, nx, ny, nz);
                if (opac >= 15) continue;                      // can't enter opaque
                int cost = std::max(1, opac);
                int next = level - cost;
                if (next < 0) continue;
                int idx = Chunk::index(nx, ny, nz);
                if (grid[idx] < next) { grid[idx] = (uint8_t)next; queue.push({nx, ny, nz}); }
            }
        }
    };
    flood(out.sky, q);

    // --- block light: seed every emitter, then flood the same way
    std::queue<Node> qb;
    for (int x = 0; x < CHUNK_W; ++x)
        for (int y = 0; y < CHUNK_H; ++y)
            for (int z = 0; z < CHUNK_W; ++z) {
                int e = reg_.get(chunk.get(x, y, z)).lightEmission;
                if (e > 0) { out.block[Chunk::index(x, y, z)] = (uint8_t)e; qb.push({x, y, z}); }
            }
    flood(out.block, qb);
}

}  // namespace vg::render
