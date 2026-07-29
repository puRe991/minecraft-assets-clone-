// Renders the terrain generator to PNGs (no game/window needed): a top-down
// biome map and a vertical cross-section showing terrain, water, caves and
// ores. Purely a diagnostic/showcase tool for Module 4.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "vg/world/TerrainGenerator.hpp"
#include "vg/world/World.hpp"

extern "C" int vg_png_write(const char* path, const unsigned int* pix, int w, int h);

using namespace vg::world;

static uint32_t rgb(int r, int g, int b) {
    auto c = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
    return 0xff000000u | (c(r) << 16) | (c(g) << 8) | c(b);
}

static uint32_t biomeColor(Biome b) {
    switch (b) {
        case Biome::Ocean:     return rgb(40, 90, 170);
        case Biome::Plains:    return rgb(120, 190, 90);
        case Biome::Forest:    return rgb(45, 120, 55);
        case Biome::Desert:    return rgb(220, 205, 140);
        case Biome::Mountains: return rgb(140, 140, 145);
        case Biome::Snowy:     return rgb(235, 240, 245);
    }
    return rgb(255, 0, 255);
}

static uint32_t blockColor(BlockId id, bool belowSurface) {
    switch (id) {
        case blocks::Air:       return belowSurface ? rgb(15, 15, 20) : rgb(150, 195, 235);
        case blocks::Bedrock:   return rgb(30, 30, 30);
        case blocks::Stone:     return rgb(120, 120, 125);
        case blocks::Dirt:      return rgb(120, 90, 60);
        case blocks::Grass:     return rgb(95, 160, 70);
        case blocks::Sand:      return rgb(215, 200, 150);
        case blocks::Gravel:    return rgb(110, 105, 105);
        case blocks::Water:     return rgb(50, 100, 200);
        case blocks::OakLog:    return rgb(105, 78, 44);
        case blocks::OakLeaves: return rgb(55, 110, 50);
        case blocks::OakPlanks: return rgb(170, 130, 85);
        case blocks::CoalOre:   return rgb(45, 45, 45);
        case blocks::IronOre:   return rgb(200, 170, 130);
        case blocks::GoldOre:   return rgb(230, 200, 70);
        case blocks::DiamondOre:return rgb(90, 220, 220);
        default:                return rgb(255, 0, 255);
    }
}

static void save(const char* name, const std::vector<uint32_t>& px, int w, int h, int scale) {
    std::vector<uint32_t> big((size_t)w * scale * h * scale);
    for (int y = 0; y < h * scale; ++y)
        for (int x = 0; x < w * scale; ++x)
            big[y * w * scale + x] = px[(y / scale) * w + (x / scale)];
    vg_png_write(name, big.data(), w * scale, h * scale);
    std::printf("  %s (%dx%d)\n", name, w * scale, h * scale);
}

int main() {
    TerrainConfig cfg{2024, 40};
    auto gen = std::make_shared<TerrainGenerator>(cfg);
    World world(gen);

    const int R = 6;                     // chunk radius -> (2R+1)*16 blocks
    world.updateStreaming(0, 0, R);
    const int span = (2 * R + 1) * CHUNK_W;
    const int minB = -R * CHUNK_W;

    // --- top-down biome map, brightness modulated by terrain height ---
    std::vector<uint32_t> map((size_t)span * span);
    for (int j = 0; j < span; ++j)
        for (int i = 0; i < span; ++i) {
            int wx = minB + i, wz = minB + j;
            auto col = gen->columnAt(wx, wz);
            uint32_t c = biomeColor(col.biome);
            float shade = 0.6f + 0.4f * (col.height - 20) / 60.0f;   // higher = brighter
            if (shade < 0.4f) shade = 0.4f; if (shade > 1.1f) shade = 1.1f;
            int r = (int)(((c >> 16) & 0xff) * shade);
            int g = (int)(((c >> 8) & 0xff) * shade);
            int b = (int)((c & 0xff) * shade);
            map[j * span + i] = rgb(r, g, b);
        }
    save("render_biome_map.png", map, span, span, 2);

    // --- vertical cross-section at wz = 0 (y up) ---
    std::vector<uint32_t> cut((size_t)span * CHUNK_H);
    int wz = 0;
    for (int i = 0; i < span; ++i) {
        int wx = minB + i;
        int surf = gen->columnAt(wx, wz).height;
        for (int y = 0; y < CHUNK_H; ++y) {
            BlockId id = world.getBlock(wx, y, wz);
            uint32_t c = blockColor(id, y < surf);
            int row = (CHUNK_H - 1 - y);         // flip so sky is on top
            cut[row * span + i] = c;
        }
    }
    save("render_cross_section.png", cut, span, CHUNK_H, 3);

    std::printf("done.\n");
    return 0;
}
