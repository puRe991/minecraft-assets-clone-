// Headless proof that the whole engine renders a world: generate terrain with
// the real TerrainGenerator, drop a player, and raycast-render to PNG.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "Renderer.hpp"
#include "vg/world/TerrainGenerator.hpp"

extern "C" int vg_png_write(const char* path, const unsigned int* pix, int w, int h);

using namespace vg;

static void save(const char* name, const std::vector<uint32_t>& px, int w, int h, int scale) {
    std::vector<uint32_t> big((size_t)w * scale * h * scale);
    for (int y = 0; y < h * scale; ++y)
        for (int x = 0; x < w * scale; ++x)
            big[y * w * scale + x] = px[(y / scale) * w + (x / scale)];
    vg_png_write(name, big.data(), w * scale, h * scale);
    std::printf("  %s (%dx%d)\n", name, w * scale, h * scale);
}

int main() {
    world::BlockRegistry reg; registerDefaultBlocks(reg);
    auto gen = std::make_shared<world::TerrainGenerator>(world::TerrainConfig{2024, 40});
    world::World w(gen);
    w.updateStreaming(0, 0, 8);   // load a big area so the view has distance

    // find a land column (well above sea level) near origin to stand on
    int lx = 4, lz = 4, surf = gen->columnAt(lx, lz).height;
    for (int r = 0; r < 110 && surf <= 46; ++r) {
        lx = (r * 7) % 100 - 50; lz = (r * 13) % 100 - 50;
        surf = gen->columnAt(lx, lz).height;
    }
    double ex = lx + 0.5, ez = lz + 0.5, ey = surf + 3;
    const int W = 480, H = 270;
    std::vector<uint32_t> fb((size_t)W * H);

    struct Shot { const char* name; app::Camera cam; double daylight; };
    Shot shots[] = {
        {"app_day.png",   {{ex, ey, ez}, 0.9, -0.12, 70}, 1.0},
        {"app_sunset.png",{{ex, ey + 2, ez}, 2.4, -0.08, 70}, 0.45},
        {"app_night.png", {{ex, ey, ez}, 4.0, -0.05, 70}, 0.14},
    };
    for (auto& s : shots) {
        app::renderWorld(fb.data(), W, H, w, reg, s.cam, s.daylight, 110.0);
        save(s.name, fb, W, H, 2);
    }
    std::printf("done.\n");
    return 0;
}
