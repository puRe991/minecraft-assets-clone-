// vg/world/TerrainGenerator.cpp — see TerrainGenerator.hpp for the contract.
#include "vg/world/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>

#include "vg/noise/FractalNoise.hpp"
#include "vg/noise/PerlinNoise.hpp"

namespace vg::world {

using noise::FractalNoise;
using noise::PerlinNoise;

namespace {
// Deterministic per-voxel hash -> float in [0,1). Used for ores, trees and
// structure placement so results depend only on (coords, seed).
uint32_t hash3(int x, int y, int z, uint32_t s) {
    uint32_t h = s * 747796405u + 2891336453u;
    h ^= (uint32_t)x * 2654435761u; h = (h ^ (h >> 15)) * 2246822519u;
    h ^= (uint32_t)y * 3266489917u; h = (h ^ (h >> 13)) * 3266489917u;
    h ^= (uint32_t)z * 668265263u;  h ^= h >> 16;
    return h;
}
float rnd3(int x, int y, int z, uint32_t s) { return (hash3(x, y, z, s) % 100000u) / 100000.0f; }
float unit(double n) { return (float)(n * 0.5 + 0.5); }

std::shared_ptr<noise::INoise> makeFbm(uint32_t seed, int oct, double freq) {
    return std::make_shared<FractalNoise>(std::make_shared<PerlinNoise>(seed),
                                          noise::FractalParams{oct, freq, 2.0, 0.5});
}
}  // namespace

TerrainGenerator::TerrainGenerator(TerrainConfig cfg) : cfg_(cfg) {
    // Distinct derived seeds keep the noise fields independent.
    height_   = makeFbm(cfg.seed + 1, 4, 0.0090);
    temp_     = makeFbm(cfg.seed + 2, 2, 0.0016);   // large biome regions
    humid_    = makeFbm(cfg.seed + 3, 2, 0.0016);
    mountain_ = makeFbm(cfg.seed + 4, 3, 0.0050);
    river_    = makeFbm(cfg.seed + 5, 2, 0.0040);
    cave_     = makeFbm(cfg.seed + 6, 3, 1.0);       // sampled at explicit freq below
}

TerrainGenerator::Column TerrainGenerator::columnAt(int wx, int wz) const {
    double cont = height_->noise2D(wx, wz);            // [-1,1]
    float  temp = unit(temp_->noise2D(wx, wz));
    float  humid = unit(humid_->noise2D(wx, wz));
    float  mtn  = unit(mountain_->noise2D(wx, wz));

    int sea = cfg_.seaLevel;
    int h = sea + (int)std::lround(cont * 16.0);
    if (mtn > 0.72f) h += (int)((mtn - 0.72f) / 0.28f * 34.0f);   // raise peaks

    // Biome from climate + elevation (using pre-river height).
    Biome biome;
    if (h <= sea - 4)                         biome = Biome::Ocean;
    else if (temp > 0.62f && humid < 0.40f)   biome = Biome::Desert;
    else if (temp < 0.32f)                    biome = Biome::Snowy;
    else if (h > sea + 20)                    biome = Biome::Mountains;
    else if (humid > 0.55f)                   biome = Biome::Forest;
    else                                      biome = Biome::Plains;

    // Rivers: where the river field crosses zero, cut a channel to just below
    // sea level so it fills with water (skipped in the open ocean).
    if (biome != Biome::Ocean) {
        double rv = std::fabs(river_->noise2D(wx, wz));
        if (rv < 0.025) h = std::min(h, sea - 1);
    }

    h = std::clamp(h, 1, CHUNK_H - 2);
    return {h, biome};
}

void TerrainGenerator::placeTree(Chunk& c, int lx, int surfaceY, int lz, uint32_t mix) const {
    int th = 4 + (int)(rnd3(lx, 0, lz, mix) * 3.0f);   // trunk height 4..6
    int top = surfaceY + th;
    for (int y = surfaceY + 1; y <= top && y < CHUNK_H; ++y)
        c.set(lx, y, lz, blocks::OakLog);
    // leaf canopy (kept within [lx,lz] +/-2, guaranteed in-chunk by caller)
    for (int dy = -1; dy <= 2; ++dy) {
        int r = (dy <= 0) ? 2 : 1;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (std::abs(dx) == r && std::abs(dz) == r && rnd3(lx + dx, dy, lz + dz, mix) < 0.4f)
                    continue;
                int y = top + dy;
                if (y >= 0 && y < CHUNK_H && c.get(lx + dx, y, lz + dz) == blocks::Air)
                    c.set(lx + dx, y, lz + dz, blocks::OakLeaves);
            }
    }
}

void TerrainGenerator::generate(Chunk& chunk) const {
    const int sea = cfg_.seaLevel;
    const int baseX = chunk.pos().x * CHUNK_W;
    const int baseZ = chunk.pos().z * CHUNK_W;

    // 1) terrain columns: solid fill, surface layers, caves, ores, water.
    for (int lx = 0; lx < CHUNK_W; ++lx)
        for (int lz = 0; lz < CHUNK_W; ++lz) {
            int wx = baseX + lx, wz = baseZ + lz;
            Column col = columnAt(wx, wz);
            int h = col.height;
            Biome b = col.biome;
            bool bareRock = (b == Biome::Mountains && h > sea + 26);

            for (int y = 0; y <= h; ++y) {
                BlockId block;
                if (y == 0) block = blocks::Bedrock;
                else if (y == h) {
                    if (b == Biome::Ocean || b == Biome::Desert) block = blocks::Sand;
                    else if (bareRock)                           block = blocks::Stone;
                    else                                         block = blocks::Grass;
                } else if (y >= h - 3) {
                    if (b == Biome::Ocean || b == Biome::Desert) block = blocks::Sand;
                    else if (bareRock)                           block = blocks::Stone;
                    else                                         block = blocks::Dirt;
                } else block = blocks::Stone;

                // caves: carve winding air pockets underground (never the very
                // top layer or bedrock), where the 3D field crosses zero.
                if (y > 0 && y < h - 1) {
                    double n = cave_->noise3D(wx * 0.06, y * 0.09, wz * 0.06);
                    if (std::fabs(n) < 0.055) { chunk.set(lx, y, lz, blocks::Air); continue; }
                }

                // ores: replace stone based on depth bands + per-voxel hash.
                if (block == blocks::Stone) {
                    float r = rnd3(wx, y, wz, cfg_.seed + 100);
                    if      (y < 14 && r < 0.006f) block = blocks::DiamondOre;
                    else if (y < 28 && r < 0.008f) block = blocks::GoldOre;
                    else if (y < 50 && r < 0.020f) block = blocks::IronOre;
                    else if (r < 0.030f)           block = blocks::CoalOre;
                }
                chunk.set(lx, y, lz, block);
            }

            // water: fill from the surface up to sea level (oceans/rivers/lakes)
            for (int y = h + 1; y <= sea && y < CHUNK_H; ++y)
                if (chunk.get(lx, y, lz) == blocks::Air)
                    chunk.set(lx, y, lz, blocks::Water);
        }

    // 2) trees (kept in the interior so canopies stay inside the chunk).
    for (int lx = 2; lx < CHUNK_W - 2; ++lx)
        for (int lz = 2; lz < CHUNK_W - 2; ++lz) {
            int wx = baseX + lx, wz = baseZ + lz;
            Column col = columnAt(wx, wz);
            if (col.height <= sea) continue;                     // no trees underwater
            if (chunk.get(lx, col.height, lz) != blocks::Grass) continue;
            float density = biomeTreeDensity(col.biome);
            if (density <= 0) continue;
            if (rnd3(wx, 1, wz, cfg_.seed + 200) < density)
                placeTree(chunk, lx, col.height, lz, cfg_.seed + 300);
        }

    // 3) a simple structure: a rare underground dungeon (cobble room) — a small
    //    demonstration of structure placement (villages are a future multi-chunk
    //    feature built on top of this same hook).
    if (hash3(chunk.pos().x, 0, chunk.pos().z, cfg_.seed + 900) % 60u == 0) {
        int rx = 5, rz = 5, ry = 10;            // fits inside one chunk
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 4; ++y)
                for (int z = 0; z < 5; ++z) {
                    bool shell = (x == 0 || x == 4 || y == 0 || y == 3 || z == 0 || z == 4);
                    chunk.set(rx + x, ry + y, rz + z, shell ? blocks::OakPlanks : blocks::Air);
                }
    }
}

// (kept for symmetry with the header; caves+ore are inlined per column above)
void TerrainGenerator::carveAndOre(Chunk&) const {}

}  // namespace vg::world
