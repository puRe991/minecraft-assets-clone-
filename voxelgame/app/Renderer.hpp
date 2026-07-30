// app/Renderer.hpp
//
// Platform-independent software renderer: a voxel DDA raycaster that draws the
// engine's World into a 32-bit ARGB framebuffer. Kept free of any window code
// so the same function backs both the Win32 app and the headless screenshot
// tool. (The tested greedy Mesher/LightEngine are the foundation for a future
// GPU path; this raycaster is the dependency-free way to actually put pixels on
// screen.)
#pragma once

#include <cmath>
#include <cstdint>

#include "vg/math/Vec3.hpp"
#include "vg/world/BlockRegistry.hpp"
#include "vg/world/World.hpp"

namespace vg::app {

using math::Vec3d;

struct Camera {
    Vec3d  pos{0, 70, 0};
    double yaw{0}, pitch{0};
    double fovDeg{70};
};

// Base colour per block (ARGB 0xAARRGGBB); face lighting is applied by caller.
inline uint32_t blockColor(world::BlockId id) {
    using namespace world::blocks;
    switch (id) {
        case Stone:       return 0xff808085;
        case Dirt:        return 0xff78543c;
        case Grass:       return 0xff5fa046;
        case Sand:        return 0xffd8cd9c;
        case Gravel:      return 0xff6e6a69;
        case Bedrock:     return 0xff2b2b2b;
        case OakLog:      return 0xff69502c;
        case OakPlanks:   return 0xffb08c5a;
        case OakLeaves:   return 0xff356e32;
        case Glass:       return 0xffb0d0e0;
        case Water:       return 0xff3264c8;
        case Lava:        return 0xffe06010;
        case CoalOre:     return 0xff5a5a5a;
        case IronOre:     return 0xffc0a080;
        case GoldOre:     return 0xffe0c840;
        case DiamondOre:  return 0xff5ad0d0;
        case Torch:       return 0xffffd040;
        case Ladder:      return 0xff8a6a3a;
        case Cobblestone: return 0xff787074;
        default:          return 0xffff00ff;
    }
}

inline uint32_t shade(uint32_t c, double f) {
    auto cl = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
    int r = cl((int)(((c >> 16) & 0xff) * f));
    int g = cl((int)(((c >> 8) & 0xff) * f));
    int b = cl((int)((c & 0xff) * f));
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

// Cast one ray; returns true and fills hit info on the first non-air block.
inline bool raycast(const world::World& w, const world::BlockRegistry& reg,
                    Vec3d o, Vec3d d, double maxDist,
                    int& hx, int& hy, int& hz, int& nx, int& ny, int& nz,
                    double& dist, world::BlockId& blk) {
    int mapx = (int)std::floor(o.x), mapy = (int)std::floor(o.y), mapz = (int)std::floor(o.z);
    double ddx = d.x == 0 ? 1e30 : std::fabs(1.0 / d.x);
    double ddy = d.y == 0 ? 1e30 : std::fabs(1.0 / d.y);
    double ddz = d.z == 0 ? 1e30 : std::fabs(1.0 / d.z);
    int sx = d.x < 0 ? -1 : 1, sy = d.y < 0 ? -1 : 1, sz = d.z < 0 ? -1 : 1;
    double sdx = (d.x < 0 ? (o.x - mapx) : (mapx + 1 - o.x)) * ddx;
    double sdy = (d.y < 0 ? (o.y - mapy) : (mapy + 1 - o.y)) * ddy;
    double sdz = (d.z < 0 ? (o.z - mapz) : (mapz + 1 - o.z)) * ddz;
    int side = 0; double t = 0;
    for (int i = 0; i < 4096; ++i) {
        if (sdx < sdy && sdx < sdz) { t = sdx; sdx += ddx; mapx += sx; side = 0; }
        else if (sdy < sdz)         { t = sdy; sdy += ddy; mapy += sy; side = 1; }
        else                        { t = sdz; sdz += ddz; mapz += sz; side = 2; }
        if (t > maxDist) return false;
        world::BlockId b = w.getBlock(mapx, mapy, mapz);
        if (b != world::blocks::Air) {
            hx = mapx; hy = mapy; hz = mapz; nx = ny = nz = 0;
            if (side == 0) nx = -sx; else if (side == 1) ny = -sy; else nz = -sz;
            dist = t; blk = b; (void)reg; return true;
        }
    }
    return false;
}

// Render the world from `cam` into an ARGB framebuffer of size W x H.
inline void renderWorld(uint32_t* fb, int W, int H, const world::World& world,
                        const world::BlockRegistry& reg, const Camera& cam,
                        double daylight, double maxDist) {
    double cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
    double cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
    Vec3d fwd{cp * sy, sp, cp * cy};
    Vec3d right{cy, 0, -sy};
    Vec3d up = math::cross(fwd, right);
    double tanF = std::tan(cam.fovDeg * 0.5 * 3.14159265358979 / 180.0);
    double aspect = (double)W / H;
    double dl = daylight < 0.12 ? 0.12 : daylight;

    uint32_t skyTop = shade(0xff78aaeb, dl), skyBot = shade(0xffc8e1fa, dl);

    for (int y = 0; y < H; ++y) {
        double sv = (1.0 - 2.0 * (y + 0.5) / H) * tanF;
        for (int x = 0; x < W; ++x) {
            double su = (2.0 * (x + 0.5) / W - 1.0) * tanF * aspect;
            Vec3d d = math::normalize(Vec3d{fwd.x + su * right.x + sv * up.x,
                                            fwd.y + su * right.y + sv * up.y,
                                            fwd.z + su * right.z + sv * up.z});
            int hx, hy, hz, nx, ny, nz; double dist; world::BlockId blk;
            uint32_t col;
            if (raycast(world, reg, cam.pos, d, maxDist, hx, hy, hz, nx, ny, nz, dist, blk)) {
                double lf = (ny > 0) ? 1.0 : (ny < 0) ? 0.55 : (nx != 0 ? 0.8 : 0.68);
                col = shade(blockColor(blk), lf * dl);
                double fog = dist / maxDist; if (fog > 1) fog = 1;
                int r = (col >> 16) & 0xff, g = (col >> 8) & 0xff, b = col & 0xff;
                int sr = (skyBot >> 16) & 0xff, sg = (skyBot >> 8) & 0xff, sb = skyBot & 0xff;
                col = 0xff000000u | ((int)(r + (sr - r) * fog * 0.8) << 16)
                                  | ((int)(g + (sg - g) * fog * 0.8) << 8)
                                  |  (int)(b + (sb - b) * fog * 0.8);
            } else {
                double t = (double)y / H;
                int r1 = (skyTop >> 16) & 0xff, g1 = (skyTop >> 8) & 0xff, b1 = skyTop & 0xff;
                int r2 = (skyBot >> 16) & 0xff, g2 = (skyBot >> 8) & 0xff, b2 = skyBot & 0xff;
                col = 0xff000000u | ((int)(r1 + (r2 - r1) * t) << 16)
                                  | ((int)(g1 + (g2 - g1) * t) << 8)
                                  |  (int)(b1 + (b2 - b1) * t);
            }
            fb[y * W + x] = col;
        }
    }
}

}  // namespace vg::app
