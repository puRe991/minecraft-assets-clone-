/*
 * MiniCraft - a from-scratch voxel sandbox game for Windows (32-bit & 64-bit).
 *
 * Original work. Uses only the Win32 API + GDI (no external libraries, no
 * copyrighted assets). All block textures are generated procedurally at
 * runtime. Rendering is a software voxel raycaster (Amanatides & Woo DDA).
 *
 * Controls:
 *   Mouse            look around
 *   W / A / S / D    move
 *   Space            jump  (fly up when flying)
 *   Left Shift       sneak / fly down
 *   F                toggle fly mode
 *   Left mouse       break block
 *   Right mouse      place block
 *   1..9             select block to place
 *   R                regenerate the world
 *   Esc              quit
 *
 * Build: see build.sh (cross-compiled with MinGW-w64 for i686 and x86_64).
 */

#ifndef HEADLESS_TEST
#include <windows.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "png.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ----------------------------------------------------------------------- */
/* Configuration                                                            */
/* ----------------------------------------------------------------------- */

#define WORLD_X 128
#define WORLD_Y 64
#define WORLD_Z 128
#define WATER_LEVEL 22

#define RENDER_W 480          /* internal render resolution (upscaled)      */
#define RENDER_H 270
#define WIN_W 960             /* default window size                        */
#define WIN_H 540

#define FOV_DEG 70.0f
#define MAX_RAY 96.0f
#define REACH 6.0f            /* block interaction distance                 */

#define TEX 16                /* texture size (px)                          */

/* Block ids */
enum {
    B_AIR = 0, B_GRASS, B_DIRT, B_STONE, B_COBBLE,
    B_LOG, B_LEAVES, B_SAND, B_PLANKS, B_WATER, B_GLASS,
    B_COUNT
};

/* ----------------------------------------------------------------------- */
/* Globals                                                                  */
/* ----------------------------------------------------------------------- */

static uint8_t  g_world[WORLD_X * WORLD_Y * WORLD_Z];
static uint32_t g_framebuf[RENDER_W * RENDER_H];
static uint32_t g_tex[B_COUNT][3][TEX * TEX];   /* [block][face 0=top,1=side,2=bottom] */

static float g_px = WORLD_X * 0.5f, g_py = 40.0f, g_pz = WORLD_Z * 0.5f;
static float g_vx = 0, g_vy = 0, g_vz = 0;
static float g_yaw = 0.0f, g_pitch = 0.0f;
static int   g_onground = 0;
static int   g_fly = 0;
static int   g_selected = B_STONE;

static int   g_keys[256];
static int   g_running = 1;
static int   g_focused = 1;

#ifndef HEADLESS_TEST
static HWND  g_hwnd;
#endif
static int   g_client_w = WIN_W, g_client_h = WIN_H;

/* edge-triggered mouse actions handled in the message loop */
static volatile int g_break_req = 0;
static volatile int g_place_req = 0;

/* ----------------------------------------------------------------------- */
/* World access                                                             */
/* ----------------------------------------------------------------------- */

static inline int in_world(int x, int y, int z) {
    return x >= 0 && x < WORLD_X && y >= 0 && y < WORLD_Y && z >= 0 && z < WORLD_Z;
}
static inline uint8_t get_block(int x, int y, int z) {
    if (!in_world(x, y, z)) return B_AIR;
    return g_world[(y * WORLD_Z + z) * WORLD_X + x];
}
static inline void set_block(int x, int y, int z, uint8_t v) {
    if (in_world(x, y, z)) g_world[(y * WORLD_Z + z) * WORLD_X + x] = v;
}
static inline int is_solid(int x, int y, int z) {
    uint8_t b = get_block(x, y, z);
    return b != B_AIR && b != B_WATER;   /* water is passable */
}

/* ----------------------------------------------------------------------- */
/* Procedural noise                                                         */
/* ----------------------------------------------------------------------- */

static unsigned hash2(int x, int y) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd2(int x, int y) { return (hash2(x, y) & 0xffffff) / (float)0xffffff; }

static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

static float valnoise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float a = rnd2(xi, yi),     b = rnd2(xi + 1, yi);
    float c = rnd2(xi, yi + 1), d = rnd2(xi + 1, yi + 1);
    float u = smooth(xf), v = smooth(yf);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

static float fbm(float x, float y) {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < 4; o++) {
        sum += amp * valnoise(x * freq, y * freq);
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return sum / norm;
}

/* ----------------------------------------------------------------------- */
/* World generation                                                         */
/* ----------------------------------------------------------------------- */

static int collide(float ex, float ey, float ez);   /* forward decl */

static void place_tree(int x, int z, int ground) {
    int h = 4 + (int)(rnd2(x * 7, z * 3) * 3.0f);
    for (int i = 1; i <= h; i++) set_block(x, ground + i, z, B_LOG);
    int top = ground + h;
    for (int dy = -1; dy <= 2; dy++) {
        int r = (dy <= 0) ? 2 : 1;
        for (int dx = -r; dx <= r; dx++)
            for (int dz = -r; dz <= r; dz++) {
                if (dx == 0 && dz == 0 && dy <= 0) continue;
                if (abs(dx) == r && abs(dz) == r && (rnd2(x + dx, z + dz) < 0.4f)) continue;
                int yy = top + dy;
                if (get_block(x + dx, yy, z + dz) == B_AIR)
                    set_block(x + dx, yy, z + dz, B_LEAVES);
            }
    }
}

static void gen_world(unsigned seed) {
    for (unsigned i = 0; i < sizeof(g_world); i++) g_world[i] = B_AIR;
    float ox = (seed % 997) * 1.3f, oz = (seed % 733) * 1.7f;

    for (int x = 0; x < WORLD_X; x++) {
        for (int z = 0; z < WORLD_Z; z++) {
            float n = fbm((x + ox) * 0.045f, (z + oz) * 0.045f);
            int h = (int)(14 + n * 30);
            if (h < 1) h = 1;
            if (h >= WORLD_Y) h = WORLD_Y - 1;
            for (int y = 0; y <= h; y++) {
                uint8_t b;
                if (y == h) {
                    if (h <= WATER_LEVEL + 1) b = B_SAND;
                    else b = B_GRASS;
                } else if (y >= h - 3) {
                    b = (h <= WATER_LEVEL + 1) ? B_SAND : B_DIRT;
                } else {
                    b = B_STONE;
                }
                set_block(x, y, z, b);
            }
            /* water fill */
            for (int y = h + 1; y <= WATER_LEVEL; y++) set_block(x, y, z, B_WATER);
        }
    }
    /* trees on grass above water */
    for (int x = 3; x < WORLD_X - 3; x++)
        for (int z = 3; z < WORLD_Z - 3; z++) {
            /* find surface */
            int y = WORLD_Y - 1;
            while (y > 0 && get_block(x, y, z) == B_AIR) y--;
            if (get_block(x, y, z) == B_GRASS && y > WATER_LEVEL + 1 &&
                rnd2(x * 13 + 1, z * 17 + 5) < 0.018f)
                place_tree(x, z, y);
        }

    /* spawn on top of terrain at center */
    int cx = WORLD_X / 2, cz = WORLD_Z / 2, cy = WORLD_Y - 1;
    while (cy > 0 && get_block(cx, cy, cz) == B_AIR) cy--;
    g_px = cx + 0.5f; g_pz = cz + 0.5f; g_py = cy + 2.6f;
    /* ensure we don't spawn inside overhanging leaves/terrain */
    for (int guard = 0; guard < WORLD_Y && collide(g_px, g_py, g_pz); guard++)
        g_py += 1.0f;
    g_vx = g_vy = g_vz = 0; g_onground = 0;
}

/* ----------------------------------------------------------------------- */
/* Procedural textures                                                      */
/* ----------------------------------------------------------------------- */

static uint32_t rgb(int r, int g, int b) {
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

static void fill_tex(uint32_t *t, int br, int bg, int bb, int noise, unsigned salt) {
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            int n = (int)((rnd2(x + salt * 31, y + salt * 71) - 0.5f) * 2 * noise);
            t[y * TEX + x] = rgb(br + n, bg + n, bb + n);
        }
}

static void gen_textures(void) {
    /* grass top */
    fill_tex(g_tex[B_GRASS][0], 86, 140, 55, 22, 1);
    /* grass side: dirt with green cap */
    fill_tex(g_tex[B_GRASS][1], 120, 90, 60, 18, 2);
    for (int x = 0; x < TEX; x++) {
        int cap = 3 + (int)(rnd2(x, 99) * 2);
        for (int y = 0; y < cap; y++) {
            int n = (int)((rnd2(x + 5, y + 5) - 0.5f) * 30);
            g_tex[B_GRASS][1][y * TEX + x] = rgb(86 + n, 140 + n, 55 + n);
        }
    }
    /* grass bottom = dirt */
    fill_tex(g_tex[B_GRASS][2], 120, 90, 60, 18, 2);

    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_DIRT][f], 120, 90, 60, 18, 3);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_STONE][f], 128, 128, 132, 16, 4);

    /* cobblestone: stone base + darker mortar speckle */
    for (int f = 0; f < 3; f++) {
        fill_tex(g_tex[B_COBBLE][f], 120, 120, 124, 20, 9);
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                if (((x + y) % 5) == 0 || rnd2(x * 3, y * 3 + f) < 0.10f)
                    g_tex[B_COBBLE][f][y * TEX + x] = rgb(80, 80, 84);
    }

    /* log: side = bark streaks, top/bottom = rings */
    fill_tex(g_tex[B_LOG][1], 105, 78, 44, 14, 5);
    for (int x = 0; x < TEX; x++)
        if ((x % 4) == 0)
            for (int y = 0; y < TEX; y++)
                g_tex[B_LOG][1][y * TEX + x] = rgb(78, 56, 30);
    for (int f = 0; f < 3; f += 2) {  /* top(0) and bottom(2) */
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float dx = x - 7.5f, dy = y - 7.5f;
                float d = sqrtf(dx * dx + dy * dy);
                int ring = ((int)(d) % 2) ? 20 : 0;
                g_tex[B_LOG][f][y * TEX + x] = rgb(150 - ring, 118 - ring, 70 - ring);
            }
    }

    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_LEAVES][f], 48, 96, 40, 26, 6);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_SAND][f], 216, 204, 156, 14, 7);

    /* planks: tan with horizontal seams */
    for (int f = 0; f < 3; f++) {
        fill_tex(g_tex[B_PLANKS][f], 176, 140, 90, 14, 8);
        for (int y = 0; y < TEX; y++)
            if ((y % 4) == 0)
                for (int x = 0; x < TEX; x++)
                    g_tex[B_PLANKS][f][y * TEX + x] = rgb(130, 100, 60);
    }

    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_WATER][f], 48, 90, 200, 16, 10);

    /* glass: light, with border frame */
    for (int f = 0; f < 3; f++) {
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int edge = (x == 0 || y == 0 || x == TEX - 1 || y == TEX - 1);
                g_tex[B_GLASS][f][y * TEX + x] = edge ? rgb(200, 220, 230) : rgb(170, 200, 215);
            }
    }
}

/* ----------------------------------------------------------------------- */
/* Asset loading (Minecraft-format resource pack)                           */
/*                                                                          */
/* At startup the game looks for real textures under                        */
/*   assets/minecraft/textures/block/<name>.png                             */
/* and uses them in place of the procedural fallback. Grayscale textures    */
/* that Minecraft tints by biome (grass, leaves) get a tint applied here,   */
/* so a stock Minecraft resource pack renders with sensible colours.        */
/* ----------------------------------------------------------------------- */

struct texref { const char *name; uint32_t tint; };   /* tint 0 = none */

static const struct texref g_texmap[B_COUNT][3] = {
    /*            top                              side                          bottom            */
    [B_GRASS]  = {{"grass_block_top", 0x91BD59}, {"grass_block_side", 0},     {"dirt", 0}},
    [B_DIRT]   = {{"dirt", 0},                   {"dirt", 0},                 {"dirt", 0}},
    [B_STONE]  = {{"stone", 0},                  {"stone", 0},                {"stone", 0}},
    [B_COBBLE] = {{"cobblestone", 0},            {"cobblestone", 0},          {"cobblestone", 0}},
    [B_LOG]    = {{"oak_log_top", 0},            {"oak_log", 0},              {"oak_log_top", 0}},
    [B_LEAVES] = {{"oak_leaves", 0x59AE30},      {"oak_leaves", 0x59AE30},    {"oak_leaves", 0x59AE30}},
    [B_SAND]   = {{"sand", 0},                   {"sand", 0},                 {"sand", 0}},
    [B_PLANKS] = {{"oak_planks", 0},             {"oak_planks", 0},           {"oak_planks", 0}},
    [B_WATER]  = {{"water_still", 0},            {"water_still", 0},          {"water_still", 0}},
    [B_GLASS]  = {{"glass", 0},                  {"glass", 0},                {"glass", 0}},
};

static int g_assets_loaded = 0;   /* number of textures loaded from disk */

static void apply_tint(uint32_t *t, uint32_t tint) {
    if (!tint) return;
    int tr = (tint >> 16) & 0xff, tg = (tint >> 8) & 0xff, tb = tint & 0xff;
    for (int i = 0; i < TEX * TEX; i++) {
        uint32_t c = t[i];
        int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
        t[i] = 0xff000000u | (((r * tr) / 255) << 16) | (((g * tg) / 255) << 8) | ((b * tb) / 255);
    }
}

static int load_one(const char *base, int blk, int face, uint32_t tint) {
    const char *name = g_texmap[blk][face].name;
    if (!name) return 0;
    char path[600];
    snprintf(path, sizeof path, "%sassets/minecraft/textures/block/%s.png", base, name);
    int w, h;
    uint32_t *img = png_load(path, &w, &h);
    if (!img) return 0;
    int frame_h = (h >= w) ? w : h;       /* animated strips: use first frame */
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            int sx = x * w / TEX;
            int sy = y * frame_h / TEX;
            if (sx >= w) sx = w - 1;
            if (sy >= h) sy = h - 1;
            g_tex[blk][face][y * TEX + x] = img[sy * w + sx] | 0xff000000u;
        }
    free(img);
    apply_tint(g_tex[blk][face], tint);
    return 1;
}

static void load_assets(void) {
    const char *bases[4];
    int nb = 0;
#ifndef HEADLESS_TEST
    static char exedir[600];
    DWORD n = GetModuleFileNameA(NULL, exedir, sizeof exedir);
    if (n > 0 && n < sizeof exedir) {
        for (int i = (int)n - 1; i >= 0; i--)
            if (exedir[i] == '\\' || exedir[i] == '/') { exedir[i + 1] = 0; break; }
        bases[nb++] = exedir;
    }
#endif
    bases[nb++] = "";       /* current working directory */
    bases[nb++] = "../";    /* running from dist/ next to repo root */

    g_assets_loaded = 0;
    for (int b = B_GRASS; b < B_COUNT; b++)
        for (int f = 0; f < 3; f++) {
            if (!g_texmap[b][f].name) continue;
            for (int bi = 0; bi < nb; bi++)
                if (load_one(bases[bi], b, f, g_texmap[b][f].tint)) { g_assets_loaded++; break; }
        }
}

/* ----------------------------------------------------------------------- */
/* Camera / rays                                                            */
/* ----------------------------------------------------------------------- */

typedef struct { float x, y, z; } V3;

static void camera_basis(V3 *fwd, V3 *right, V3 *up) {
    float cy = cosf(g_yaw), sy = sinf(g_yaw);
    float cp = cosf(g_pitch), sp = sinf(g_pitch);
    fwd->x = cp * sy; fwd->y = sp; fwd->z = cp * cy;
    /* right = normalize(cross(fwd, worldUp)) */
    right->x = cy; right->y = 0; right->z = -sy;
    /* up = cross(right, fwd) */
    up->x = right->y * fwd->z - right->z * fwd->y;
    up->y = right->z * fwd->x - right->x * fwd->z;
    up->z = right->x * fwd->y - right->y * fwd->x;
}

/* DDA raycast. Returns 1 on hit. Fills block coords, face normal, and the
   coord of the empty cell just before the hit (for placement). */
static int raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                   float maxdist, int *hx, int *hy, int *hz,
                   int *nx, int *ny, int *nz, float *outdist, int *outblock) {
    int mapx = (int)floorf(ox), mapy = (int)floorf(oy), mapz = (int)floorf(oz);
    float ddx = (dx == 0) ? 1e30f : fabsf(1.0f / dx);
    float ddy = (dy == 0) ? 1e30f : fabsf(1.0f / dy);
    float ddz = (dz == 0) ? 1e30f : fabsf(1.0f / dz);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, sz = dz < 0 ? -1 : 1;
    float sdx = (dx < 0 ? (ox - mapx) : (mapx + 1 - ox)) * ddx;
    float sdy = (dy < 0 ? (oy - mapy) : (mapy + 1 - oy)) * ddy;
    float sdz = (dz < 0 ? (oz - mapz) : (mapz + 1 - oz)) * ddz;
    int side = 0;
    float dist = 0;

    for (int iter = 0; iter < 1024; iter++) {
        if (sdx < sdy && sdx < sdz) { dist = sdx; sdx += ddx; mapx += sx; side = 0; }
        else if (sdy < sdz)         { dist = sdy; sdy += ddy; mapy += sy; side = 1; }
        else                        { dist = sdz; sdz += ddz; mapz += sz; side = 2; }
        if (dist > maxdist) return 0;
        uint8_t b = get_block(mapx, mapy, mapz);
        if (b != B_AIR && b != B_WATER) {
            *hx = mapx; *hy = mapy; *hz = mapz;
            *nx = *ny = *nz = 0;
            if (side == 0)      *nx = -sx;
            else if (side == 1) *ny = -sy;
            else                *nz = -sz;
            if (outdist) *outdist = dist;
            if (outblock) *outblock = b;
            return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Rendering                                                                */
/* ----------------------------------------------------------------------- */

static uint32_t shade(uint32_t c, float f) {
    int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    return rgb((int)(r * f), (int)(g * f), (int)(b * f));
}

static void render_frame(void) {
    V3 fwd, right, up;
    camera_basis(&fwd, &right, &up);
    float tanf_ = tanf(FOV_DEG * 0.5f * (float)M_PI / 180.0f);
    float aspect = (float)RENDER_W / RENDER_H;

    const uint32_t sky_top = rgb(120, 170, 235);
    const uint32_t sky_bot = rgb(200, 225, 250);

    for (int y = 0; y < RENDER_H; y++) {
        float sv = (1.0f - 2.0f * (y + 0.5f) / RENDER_H) * tanf_;
        for (int x = 0; x < RENDER_W; x++) {
            float su = (2.0f * (x + 0.5f) / RENDER_W - 1.0f) * tanf_ * aspect;
            float dx = fwd.x + su * right.x + sv * up.x;
            float dy = fwd.y + su * right.y + sv * up.y;
            float dz = fwd.z + su * right.z + sv * up.z;
            float il = 1.0f / sqrtf(dx * dx + dy * dy + dz * dz);
            dx *= il; dy *= il; dz *= il;

            int hx, hy, hz, nx, ny, nz, blk;
            float dist;
            uint32_t col;

            if (raycast(g_px, g_py, g_pz, dx, dy, dz, MAX_RAY,
                        &hx, &hy, &hz, &nx, &ny, &nz, &dist, &blk)) {
                /* hit point for texture coords */
                float hxp = g_px + dx * dist;
                float hyp = g_py + dy * dist;
                float hzp = g_pz + dz * dist;
                float u, v;
                int face;
                if (nx != 0)      { u = hzp - floorf(hzp); v = 1 - (hyp - floorf(hyp)); face = 1; }
                else if (nz != 0) { u = hxp - floorf(hxp); v = 1 - (hyp - floorf(hyp)); face = 1; }
                else              { u = hxp - floorf(hxp); v = hzp - floorf(hzp);
                                    face = (ny > 0) ? 0 : 2; }
                int tu = (int)(u * TEX); if (tu < 0) tu = 0; if (tu >= TEX) tu = TEX - 1;
                int tv = (int)(v * TEX); if (tv < 0) tv = 0; if (tv >= TEX) tv = TEX - 1;
                col = g_tex[blk][face][tv * TEX + tu];

                /* face lighting */
                float lf = (ny > 0) ? 1.0f : (ny < 0) ? 0.55f : (nx != 0 ? 0.8f : 0.68f);
                /* distance fog toward sky */
                float fog = dist / MAX_RAY; if (fog > 1) fog = 1;
                col = shade(col, lf);
                int r = (col >> 16) & 0xff, g = (col >> 8) & 0xff, b = col & 0xff;
                int sr = (sky_bot >> 16) & 0xff, sg = (sky_bot >> 8) & 0xff, sb = sky_bot & 0xff;
                col = rgb((int)(r + (sr - r) * fog * 0.85f),
                          (int)(g + (sg - g) * fog * 0.85f),
                          (int)(b + (sb - b) * fog * 0.85f));
            } else {
                /* sky gradient */
                float t = (float)y / RENDER_H;
                int r1 = (sky_top >> 16) & 0xff, g1 = (sky_top >> 8) & 0xff, b1 = sky_top & 0xff;
                int r2 = (sky_bot >> 16) & 0xff, g2 = (sky_bot >> 8) & 0xff, b2 = sky_bot & 0xff;
                col = rgb((int)(r1 + (r2 - r1) * t),
                          (int)(g1 + (g2 - g1) * t),
                          (int)(b1 + (b2 - b1) * t));
            }
            g_framebuf[y * RENDER_W + x] = col;
        }
    }

    /* crosshair */
    int cxp = RENDER_W / 2, cyp = RENDER_H / 2;
    for (int i = -5; i <= 5; i++) {
        g_framebuf[cyp * RENDER_W + (cxp + i)] ^= 0x00ffffff;
        g_framebuf[(cyp + i) * RENDER_W + cxp] ^= 0x00ffffff;
    }

    /* selected-block swatch (top-left) */
    for (int y = 4; y < 24; y++)
        for (int x = 4; x < 24; x++)
            g_framebuf[y * RENDER_W + x] = g_tex[g_selected][1][(y % TEX) * TEX + (x % TEX)];
}

/* ----------------------------------------------------------------------- */
/* Player physics & interaction                                             */
/* ----------------------------------------------------------------------- */

static const float PLR_RAD = 0.3f;
static const float PLR_HEAD = 0.2f;
static const float PLR_FEET = 1.6f;

static int collide(float ex, float ey, float ez) {
    int x0 = (int)floorf(ex - PLR_RAD), x1 = (int)floorf(ex + PLR_RAD);
    int y0 = (int)floorf(ey - PLR_FEET), y1 = (int)floorf(ey + PLR_HEAD);
    int z0 = (int)floorf(ez - PLR_RAD), z1 = (int)floorf(ez + PLR_RAD);
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++)
                if (is_solid(x, y, z)) return 1;
    return 0;
}

static void update_player(float dt) {
    float cy = cosf(g_yaw), sy = sinf(g_yaw);
    /* forward/right on the horizontal plane */
    float fx = sy, fz = cy;
    float rx = cy, rz = -sy;

    float wish_x = 0, wish_z = 0;
    if (g_keys['W']) { wish_x += fx; wish_z += fz; }
    if (g_keys['S']) { wish_x -= fx; wish_z -= fz; }
    if (g_keys['D']) { wish_x += rx; wish_z += rz; }
    if (g_keys['A']) { wish_x -= rx; wish_z -= rz; }
    float wl = sqrtf(wish_x * wish_x + wish_z * wish_z);
    if (wl > 0) { wish_x /= wl; wish_z /= wl; }

    float speed = g_fly ? 9.0f : 4.5f;

    if (g_fly) {
        g_vx = wish_x * speed; g_vz = wish_z * speed;
        g_vy = 0;
        if (g_keys[0x20])   g_vy = speed;   /* 0x20 = VK_SPACE / ' ' */
        if (g_keys[0x10])   g_vy = -speed;  /* 0x10 = VK_SHIFT       */
    } else {
        g_vx = wish_x * speed; g_vz = wish_z * speed;
        g_vy -= 22.0f * dt;                 /* gravity */
        if (g_keys[0x20] && g_onground) { g_vy = 8.2f; g_onground = 0; }
    }

    /* integrate with per-axis collision */
    float nx = g_px + g_vx * dt;
    if (!collide(nx, g_py, g_pz)) g_px = nx; else g_vx = 0;
    float nz = g_pz + g_vz * dt;
    if (!collide(g_px, g_py, nz)) g_pz = nz; else g_vz = 0;
    float ny = g_py + g_vy * dt;
    if (!collide(g_px, ny, g_pz)) { g_py = ny; g_onground = 0; }
    else { if (g_vy < 0) g_onground = 1; g_vy = 0; }

    if (g_py < -40) { g_py = 60; g_vy = 0; }  /* fell out: reset height */
}

static void do_break(void) {
    V3 f, r, u; camera_basis(&f, &r, &u);
    int hx, hy, hz, nx, ny, nz, blk; float d;
    if (raycast(g_px, g_py, g_pz, f.x, f.y, f.z, REACH,
                &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk))
        set_block(hx, hy, hz, B_AIR);
}

static void do_place(void) {
    V3 f, r, u; camera_basis(&f, &r, &u);
    int hx, hy, hz, nx, ny, nz, blk; float d;
    if (raycast(g_px, g_py, g_pz, f.x, f.y, f.z, REACH,
                &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk)) {
        int bx = hx + nx, by = hy + ny, bz = hz + nz;
        /* don't place inside the player */
        float ex = bx + 0.5f, ey = by + 0.5f, ez = bz + 0.5f;
        int px0 = (int)floorf(g_px - PLR_RAD), px1 = (int)floorf(g_px + PLR_RAD);
        int py0 = (int)floorf(g_py - PLR_FEET), py1 = (int)floorf(g_py + PLR_HEAD);
        int pz0 = (int)floorf(g_pz - PLR_RAD), pz1 = (int)floorf(g_pz + PLR_RAD);
        (void)ex; (void)ey; (void)ez;
        int inside = (bx >= px0 && bx <= px1 && by >= py0 && by <= py1 && bz >= pz0 && bz <= pz1);
        if (!inside && get_block(bx, by, bz) == B_AIR)
            set_block(bx, by, bz, (uint8_t)g_selected);
    }
}

/* ----------------------------------------------------------------------- */
/* Win32 plumbing                                                           */
/* ----------------------------------------------------------------------- */
#ifndef HEADLESS_TEST

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: g_running = 0; PostQuitMessage(0); return 0;
    case WM_SIZE:    g_client_w = LOWORD(l); g_client_h = HIWORD(l); return 0;
    case WM_SETFOCUS:  g_focused = 1; return 0;
    case WM_KILLFOCUS: g_focused = 0; return 0;
    case WM_KEYDOWN:
        if (w < 256) g_keys[w] = 1;
        if (w == VK_ESCAPE) { g_running = 0; PostQuitMessage(0); }
        if (w == 'F') g_fly = !g_fly;
        if (w == 'R') gen_world((unsigned)GetTickCount());
        if (w >= '1' && w <= '9') {
            int idx = (int)w - '0';          /* 1..9 -> block ids 1..9 */
            if (idx < B_COUNT) g_selected = idx;
        }
        if (w == '0') g_selected = B_GLASS;
        return 0;
    case WM_KEYUP:
        if (w < 256) g_keys[w] = 0;
        return 0;
    case WM_LBUTTONDOWN: g_break_req = 1; return 0;
    case WM_RBUTTONDOWN: g_place_req = 1; return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void center_mouse(int *cx, int *cy) {
    RECT rc; GetClientRect(g_hwnd, &rc);
    POINT p; p.x = (rc.right - rc.left) / 2; p.y = (rc.bottom - rc.top) / 2;
    *cx = p.x; *cy = p.y;
    ClientToScreen(g_hwnd, &p);
    SetCursorPos(p.x, p.y);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int nShow) {
    (void)hPrev; (void)cmd;

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "MiniCraftWnd";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    RECT r = {0, 0, WIN_W, WIN_H};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindow("MiniCraftWnd", "MiniCraft - voxel sandbox (32/64-bit)",
                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          r.right - r.left, r.bottom - r.top,
                          NULL, NULL, hInst, NULL);
    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    gen_textures();
    load_assets();
    gen_world((unsigned)GetTickCount());

    {
        char title[128];
        snprintf(title, sizeof title,
                 "MiniCraft - voxel sandbox (32/64-bit)  |  %d textures from assets",
                 g_assets_loaded);
        SetWindowTextA(g_hwnd, title);
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = RENDER_W;
    bmi.bmiHeader.biHeight = -RENDER_H;      /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    int mouse_init = 0;
    HDC hdc = GetDC(g_hwnd);

    while (g_running) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_running) break;

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        /* mouse look */
        if (g_focused) {
            int cx, cy; RECT rc; GetClientRect(g_hwnd, &rc);
            cx = (rc.right - rc.left) / 2; cy = (rc.bottom - rc.top) / 2;
            POINT p; GetCursorPos(&p); ScreenToClient(g_hwnd, &p);
            if (mouse_init) {
                float mdx = (float)(p.x - cx), mdy = (float)(p.y - cy);
                g_yaw   -= mdx * 0.0025f;
                g_pitch -= mdy * 0.0025f;
                float lim = 1.55f;
                if (g_pitch > lim) g_pitch = lim;
                if (g_pitch < -lim) g_pitch = -lim;
            }
            int scx, scy; center_mouse(&scx, &scy);
            mouse_init = 1;
            ShowCursor(FALSE);
        } else {
            mouse_init = 0;
            ShowCursor(TRUE);
        }

        if (g_break_req) { do_break(); g_break_req = 0; }
        if (g_place_req) { do_place(); g_place_req = 0; }

        update_player(dt);
        render_frame();

        StretchDIBits(hdc, 0, 0, g_client_w, g_client_h,
                      0, 0, RENDER_W, RENDER_H,
                      g_framebuf, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    ReleaseDC(g_hwnd, hdc);
    return 0;
}

#endif /* !HEADLESS_TEST */
