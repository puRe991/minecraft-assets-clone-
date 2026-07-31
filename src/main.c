/*
 * MiniCraft - a from-scratch voxel sandbox game for Windows (32-bit & 64-bit).
 *
 * Original work. Uses only the Win32 API + GDI (no external libraries, no
 * copyrighted assets). All block textures are generated procedurally at
 * runtime. Rendering is a software voxel raycaster (Amanatides & Woo DDA).
 *
 * The raycaster is the whole cost of a frame, so it gets three things:
 * rays are clipped to the world box, empty space is skipped a macro cell at a
 * time via a coarse occupancy grid, and scanlines are split across a pool of
 * worker threads. If a machine still cannot keep up, the internal resolution
 * drops automatically and the result is upscaled to the window.
 *
 * Controls:
 *   Mouse            look around
 *   W / A / S / D    move
 *   Space            jump  (fly up when flying)
 *   Left Shift       sneak / fly down
 *   F                toggle fly mode
 *   Left mouse       break block
 *   Right mouse      place block
 *   1..9, 0          select block to place
 *   K / L            save / load the world (world.sav)
 *   R                regenerate the world
 *   T                toggle adaptive resolution
 *   + / -            raise / lower render resolution (locks it)
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

/* Side of a macro cell in the coarse occupancy grid used to skip empty space
   while raycasting. Must divide all three world dimensions. */
#define MACRO 8
#define MACRO_X (WORLD_X / MACRO)
#define MACRO_Y (WORLD_Y / MACRO)
#define MACRO_Z (WORLD_Z / MACRO)
typedef char macro_divides_world[(WORLD_X % MACRO == 0 && WORLD_Y % MACRO == 0 &&
                                  WORLD_Z % MACRO == 0) ? 1 : -1];

#define MAX_THREADS 16        /* upper bound on render worker threads        */

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

/* Coarse occupancy grid: g_macro[c] is nonzero when macro cell c contains at
   least one ray-stopping block. A zero cell is guaranteed empty, which lets
   the raycaster jump MACRO blocks at a time through open air and sky. */
static uint8_t  g_macro[MACRO_X * MACRO_Y * MACRO_Z];
static int      g_macro_defer = 0;   /* bulk world edit in progress          */

static uint32_t g_framebuf[RENDER_W * RENDER_H];

/* Current internal render size. Never exceeds RENDER_W/RENDER_H; the adaptive
   resolution controller lowers it when frames get expensive. Rows are packed
   at stride g_rw. */
static int g_rw = RENDER_W, g_rh = RENDER_H;
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
static inline int stops_ray(uint8_t b) {
    return b != B_AIR && b != B_WATER;   /* water is passable */
}

/* Recompute one macro cell from the blocks it covers. */
static void macro_rebuild_cell(int mx, int my, int mz) {
    int occupied = 0;
    for (int y = my * MACRO; y < my * MACRO + MACRO && !occupied; y++)
        for (int z = mz * MACRO; z < mz * MACRO + MACRO && !occupied; z++)
            for (int x = mx * MACRO; x < mx * MACRO + MACRO; x++)
                if (stops_ray(g_world[(y * WORLD_Z + z) * WORLD_X + x])) { occupied = 1; break; }
    g_macro[(my * MACRO_Z + mz) * MACRO_X + mx] = (uint8_t)occupied;
}

static void macro_rebuild_all(void) {
    for (int my = 0; my < MACRO_Y; my++)
        for (int mz = 0; mz < MACRO_Z; mz++)
            for (int mx = 0; mx < MACRO_X; mx++)
                macro_rebuild_cell(mx, my, mz);
}

static inline void set_block(int x, int y, int z, uint8_t v) {
    if (!in_world(x, y, z)) return;
    g_world[(y * WORLD_Z + z) * WORLD_X + x] = v;
    if (g_macro_defer) return;                 /* caller rebuilds afterwards */
    int m = ((y / MACRO) * MACRO_Z + (z / MACRO)) * MACRO_X + (x / MACRO);
    if (stops_ray(v)) g_macro[m] = 1;          /* marking is always safe     */
    else if (g_macro[m]) macro_rebuild_cell(x / MACRO, y / MACRO, z / MACRO);
}

static inline int is_solid(int x, int y, int z) {
    return stops_ray(get_block(x, y, z));
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
    g_macro_defer = 1;                 /* one rebuild at the end, not 1M */
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

    g_macro_defer = 0;
    macro_rebuild_all();

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
/* World save / load (simple RLE-compressed save file)                      */
/* ----------------------------------------------------------------------- */

static int save_world(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("MCW1", 1, 4, f);
    int dims[3] = {WORLD_X, WORLD_Y, WORLD_Z};
    fwrite(dims, sizeof(int), 3, f);
    float ps[5] = {g_px, g_py, g_pz, g_yaw, g_pitch};
    fwrite(ps, sizeof(float), 5, f);
    unsigned i = 0, total = (unsigned)sizeof(g_world);
    while (i < total) {                     /* run-length encode block ids */
        uint8_t v = g_world[i];
        unsigned run = 1;
        while (i + run < total && g_world[i + run] == v && run < 0xffffffu) run++;
        fputc(v, f);
        fputc(run & 0xff, f); fputc((run >> 8) & 0xff, f); fputc((run >> 16) & 0xff, f);
        i += run;
    }
    fclose(f);
    return 1;
}

static int load_world(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4]; int dims[3]; float ps[5];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MCW1", 4) != 0) { fclose(f); return 0; }
    if (fread(dims, sizeof(int), 3, f) != 3 ||
        dims[0] != WORLD_X || dims[1] != WORLD_Y || dims[2] != WORLD_Z) { fclose(f); return 0; }
    if (fread(ps, sizeof(float), 5, f) != 5) { fclose(f); return 0; }
    unsigned i = 0, total = (unsigned)sizeof(g_world);
    while (i < total) {
        int v = fgetc(f);
        int b0 = fgetc(f), b1 = fgetc(f), b2 = fgetc(f);
        if (v < 0 || b2 < 0) break;
        unsigned run = (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16);
        while (run-- && i < total) g_world[i++] = (uint8_t)v;
    }
    fclose(f);
    macro_rebuild_all();            /* g_world was written directly, even on a
                                       short read, so resync the macro grid */
    if (i != total) return 0;
    g_px = ps[0]; g_py = ps[1]; g_pz = ps[2]; g_yaw = ps[3]; g_pitch = ps[4];
    g_vx = g_vy = g_vz = 0; g_onground = 0;
    return 1;
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

/* DDA raycast (Amanatides & Woo). Returns 1 on hit, filling the block coords,
   the face normal, the distance and the block id.
 *
 * Three things keep this cheap, since it runs once per pixel:
 *   1. the ray is clipped to the world box up front, so a ray aimed at the sky
 *      stops at the world boundary instead of stepping all the way to maxdist;
 *   2. traversal is hierarchical -- an outer DDA walks the macro grid and skips
 *      MACRO blocks at a stride whenever a macro cell is known to be empty;
 *   3. the inner DDA is confined to one macro cell, so its block reads are
 *      in-bounds by construction and need no per-step bounds check.
 * The visible result is identical to a plain per-block DDA. */
static int raycast(float ox, float oy, float oz, float dx, float dy, float dz,
                   float maxdist, int *hx, int *hy, int *hz,
                   int *nx, int *ny, int *nz, float *outdist, int *outblock) {
    /* signed inverses (0 stands in for "never crosses"; guarded by the ddN
       sentinel below) and the classic per-axis DDA constants */
    float ix = (dx == 0.0f) ? 0.0f : 1.0f / dx;
    float iy = (dy == 0.0f) ? 0.0f : 1.0f / dy;
    float iz = (dz == 0.0f) ? 0.0f : 1.0f / dz;
    float ddx = (dx == 0.0f) ? 1e30f : fabsf(ix);
    float ddy = (dy == 0.0f) ? 1e30f : fabsf(iy);
    float ddz = (dz == 0.0f) ? 1e30f : fabsf(iz);
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1, sz = dz < 0 ? -1 : 1;

    /* --- clip the ray to the world box --- */
    float t0 = 0.0f, t1 = maxdist, ta, tb;
    int entry_axis = -1;              /* face the ray enters the box by, if any */
    int inside = (ox >= 0.0f && ox < (float)WORLD_X && oy >= 0.0f && oy < (float)WORLD_Y &&
                  oz >= 0.0f && oz < (float)WORLD_Z);
    if (inside) {
        /* Fast path -- the camera is in the world, so only the exit matters.
           This is the case for every one of the ~130k rays cast per frame. */
        if (dx != 0.0f) { ta = ((dx > 0 ? (float)WORLD_X : 0.0f) - ox) * ix; if (ta < t1) t1 = ta; }
        if (dy != 0.0f) { ta = ((dy > 0 ? (float)WORLD_Y : 0.0f) - oy) * iy; if (ta < t1) t1 = ta; }
        if (dz != 0.0f) { ta = ((dz > 0 ? (float)WORLD_Z : 0.0f) - oz) * iz; if (ta < t1) t1 = ta; }
    } else {
        if (dx == 0.0f) { if (ox < 0.0f || ox >= (float)WORLD_X) return 0; }
        else { ta = (0.0f - ox) * ix; tb = ((float)WORLD_X - ox) * ix;
               if (ta > tb) { float s = ta; ta = tb; tb = s; }
               if (ta > t0) { t0 = ta; entry_axis = 0; }
               if (tb < t1) t1 = tb;
               if (t0 > t1) return 0; }
        if (dy == 0.0f) { if (oy < 0.0f || oy >= (float)WORLD_Y) return 0; }
        else { ta = (0.0f - oy) * iy; tb = ((float)WORLD_Y - oy) * iy;
               if (ta > tb) { float s = ta; ta = tb; tb = s; }
               if (ta > t0) { t0 = ta; entry_axis = 1; }
               if (tb < t1) t1 = tb;
               if (t0 > t1) return 0; }
        if (dz == 0.0f) { if (oz < 0.0f || oz >= (float)WORLD_Z) return 0; }
        else { ta = (0.0f - oz) * iz; tb = ((float)WORLD_Z - oz) * iz;
               if (ta > tb) { float s = ta; ta = tb; tb = s; }
               if (ta > t0) { t0 = ta; entry_axis = 2; }
               if (tb < t1) t1 = tb;
               if (t0 > t1) return 0; }
    }
    if (t1 <= 0.0f) return 0;

    int mx, my, mz;               /* current block cell                       */
    float sdx, sdy, sdz;          /* absolute t of the next crossing per axis  */
    float t = t0;                 /* t at which the current cell was entered   */

    /* (Re)start the block DDA from the point at parameter t. Used once at the
       world entry point and again after every jump over empty space.
       The clamp makes a truncating cast equivalent to floorf() here, which
       matters because floorf() is an out-of-line call on the 32-bit target. */
#define SEED_DDA() do {                                                        \
        float px = ox + dx * t, py = oy + dy * t, pz = oz + dz * t;            \
        mx = (int)px; if (mx < 0) mx = 0;                                      \
        else if (mx >= WORLD_X) mx = WORLD_X - 1;                              \
        my = (int)py; if (my < 0) my = 0;                                      \
        else if (my >= WORLD_Y) my = WORLD_Y - 1;                              \
        mz = (int)pz; if (mz < 0) mz = 0;                                      \
        else if (mz >= WORLD_Z) mz = WORLD_Z - 1;                              \
        sdx = (dx < 0 ? (px - mx) : (mx + 1 - px)) * ddx + t;                  \
        sdy = (dy < 0 ? (py - my) : (my + 1 - py)) * ddy + t;                  \
        sdz = (dz < 0 ? (pz - mz) : (mz + 1 - pz)) * ddz + t;                  \
    } while (0)

    SEED_DDA();

    /* A cell is entered through the face named by `side`; -1 marks the cell the
       ray starts in, which stays untested so a camera stuck inside a block can
       still see out (the original behaviour). */
    int side = entry_axis;
    int check_macro = 1;

    /* index of the first block of a macro cell along each axis, in the
       direction of travel -- reaching it means a new macro cell was entered */
    const int edge_x = (sx > 0) ? 0 : MACRO - 1;
    const int edge_y = (sy > 0) ? 0 : MACRO - 1;
    const int edge_z = (sz > 0) ? 0 : MACRO - 1;

    for (;;) {
        /* --- 1. jump over runs of empty macro cells --- */
        if (check_macro &&
            !g_macro[((my / MACRO) * MACRO_Z + (mz / MACRO)) * MACRO_X + (mx / MACRO)]) {
            int mcx = mx / MACRO, mcy = my / MACRO, mcz = mz / MACRO;
            float tmx = (dx == 0.0f) ? 1e30f : ((float)((dx > 0 ? mcx + 1 : mcx) * MACRO) - ox) * ix;
            float tmy = (dy == 0.0f) ? 1e30f : ((float)((dy > 0 ? mcy + 1 : mcy) * MACRO) - oy) * iy;
            float tmz = (dz == 0.0f) ? 1e30f : ((float)((dz > 0 ? mcz + 1 : mcz) * MACRO) - oz) * iz;
            do {
                if (tmx < tmy && tmx < tmz) {
                    t = tmx; if (t >= t1) return 0;
                    mcx += sx; if ((unsigned)mcx >= MACRO_X) return 0;
                    tmx += MACRO * ddx; side = 0;
                } else if (tmy < tmz) {
                    t = tmy; if (t >= t1) return 0;
                    mcy += sy; if ((unsigned)mcy >= MACRO_Y) return 0;
                    tmy += MACRO * ddy; side = 1;
                } else {
                    t = tmz; if (t >= t1) return 0;
                    mcz += sz; if ((unsigned)mcz >= MACRO_Z) return 0;
                    tmz += MACRO * ddz; side = 2;
                }
            } while (!g_macro[(mcy * MACRO_Z + mcz) * MACRO_X + mcx]);
            SEED_DDA();      /* land on the first block of the occupied cell */
        }
        check_macro = 0;

        /* --- 2. test the cell we are in --- */
        if (side >= 0) {
            uint8_t blk = g_world[(my * WORLD_Z + mz) * WORLD_X + mx];
            if (stops_ray(blk)) {
                *hx = mx; *hy = my; *hz = mz;
                *nx = *ny = *nz = 0;
                if (side == 0)      *nx = -sx;
                else if (side == 1) *ny = -sy;
                else                *nz = -sz;
                if (outdist)  *outdist  = t;
                if (outblock) *outblock = blk;
                return 1;
            }
        }

        /* --- 3. advance one block --- */
        if (sdx < sdy && sdx < sdz) {
            t = sdx; if (t >= t1) return 0;
            sdx += ddx; mx += sx; if ((unsigned)mx >= WORLD_X) return 0;
            side = 0; check_macro = ((mx & (MACRO - 1)) == edge_x);
        } else if (sdy < sdz) {
            t = sdy; if (t >= t1) return 0;
            sdy += ddy; my += sy; if ((unsigned)my >= WORLD_Y) return 0;
            side = 1; check_macro = ((my & (MACRO - 1)) == edge_y);
        } else {
            t = sdz; if (t >= t1) return 0;
            sdz += ddz; mz += sz; if ((unsigned)mz >= WORLD_Z) return 0;
            side = 2; check_macro = ((mz & (MACRO - 1)) == edge_z);
        }
    }
#undef SEED_DDA
}

/* ----------------------------------------------------------------------- */
/* Rendering                                                                */
/* ----------------------------------------------------------------------- */

/* Fractional part of a value known to be non-negative. Avoids floorf(), which
   is an out-of-line libm call on the 32-bit target and runs twice per pixel. */
static inline float fracp(float v) { return v - (float)(int)v; }

/* Per-frame camera state, published once before the worker threads start. */
static struct {
    V3    fwd, right, up;
    float ox, oy, oz;
    float su0, dsu, tan_v;      /* screen-space ray parameters */
    int   sky_r[RENDER_H], sky_g[RENDER_H], sky_b[RENDER_H];
} g_view;

#define SKY_BOT_R 200
#define SKY_BOT_G 225
#define SKY_BOT_B 250

/* Renders every ystep'th scanline starting at y0. Splitting the frame between
   threads by interleaving rows (rather than in contiguous bands) keeps the
   load even: sky rows cost far more than ground rows. */
static void render_rows(int y0, int ystep) {
    const int rw = g_rw, rh = g_rh;
    const float ox = g_view.ox, oy = g_view.oy, oz = g_view.oz;

    for (int y = y0; y < rh; y += ystep) {
        float sv = (1.0f - 2.0f * (y + 0.5f) / rh) * g_view.tan_v;
        /* ray direction = base + su * right, with su exact per pixel */
        const float bx = g_view.fwd.x + sv * g_view.up.x;
        const float by = g_view.fwd.y + sv * g_view.up.y;
        const float bz = g_view.fwd.z + sv * g_view.up.z;
        const float ux = g_view.right.x, uy = g_view.right.y, uz = g_view.right.z;

        const int skr = g_view.sky_r[y], skg = g_view.sky_g[y], skb = g_view.sky_b[y];
        uint32_t *row = &g_framebuf[y * rw];

        for (int x = 0; x < rw; x++) {
            float su = g_view.su0 + x * g_view.dsu;
            float dx = bx + su * ux, dy = by + su * uy, dz = bz + su * uz;
            float il = 1.0f / sqrtf(dx * dx + dy * dy + dz * dz);
            float rx = dx * il, ry = dy * il, rz = dz * il;

            int hx, hy, hz, nx, ny, nz, blk;
            float dist;

            if (raycast(ox, oy, oz, rx, ry, rz, MAX_RAY,
                        &hx, &hy, &hz, &nx, &ny, &nz, &dist, &blk)) {
                float hxp = ox + rx * dist;
                float hyp = oy + ry * dist;
                float hzp = oz + rz * dist;
                float u, v;
                int face;
                if (nx != 0)      { u = fracp(hzp); v = 1 - fracp(hyp); face = 1; }
                else if (nz != 0) { u = fracp(hxp); v = 1 - fracp(hyp); face = 1; }
                else              { u = fracp(hxp); v = fracp(hzp);
                                    face = (ny > 0) ? 0 : 2; }
                int tu = (int)(u * TEX); if (tu < 0) tu = 0; else if (tu >= TEX) tu = TEX - 1;
                int tv = (int)(v * TEX); if (tv < 0) tv = 0; else if (tv >= TEX) tv = TEX - 1;
                uint32_t c = g_tex[blk][face][tv * TEX + tu];

                /* face lighting, then distance fog toward the sky colour.
                   Both factors are <= 1 and both endpoints are bytes, so the
                   result cannot leave 0..255 and needs no clamping. */
                float lf  = (ny > 0) ? 1.0f : (ny < 0) ? 0.55f : (nx != 0 ? 0.8f : 0.68f);
                float fog = dist * (1.0f / MAX_RAY); if (fog > 1.0f) fog = 1.0f;
                fog *= 0.85f;

                int r = (int)(((c >> 16) & 0xff) * lf);
                int g = (int)(((c >>  8) & 0xff) * lf);
                int b = (int)(( c        & 0xff) * lf);
                r += (int)((SKY_BOT_R - r) * fog);
                g += (int)((SKY_BOT_G - g) * fog);
                b += (int)((SKY_BOT_B - b) * fog);
                row[x] = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else {
                row[x] = 0xff000000u | ((uint32_t)skr << 16) |
                         ((uint32_t)skg << 8) | (uint32_t)skb;
            }
        }
    }
}

/* Draws the crosshair and hotbar over the finished frame. */
static void render_overlay(void) {
    const int rw = g_rw, rh = g_rh;

    int cxp = rw / 2, cyp = rh / 2;
    for (int i = -5; i <= 5; i++) {
        g_framebuf[cyp * rw + (cxp + i)] ^= 0x00ffffff;
        g_framebuf[(cyp + i) * rw + cxp] ^= 0x00ffffff;
    }

    /* hotbar: block ids 1..10 (GLASS = 10), selected slot highlighted */
    const int slots = 10, gap = 2;
    int sw = 22 * rw / RENDER_W; if (sw < 8) sw = 8;   /* scale with render size */
    int tot = slots * (sw + gap) - gap;
    int x0 = (rw - tot) / 2, y0 = rh - sw - 6;
    for (int k = 0; k < slots; k++) {
        int blk = k + 1;
        int sxp = x0 + k * (sw + gap);
        int sel = (blk == g_selected);
        for (int yy = -2; yy < sw + 2; yy++)
            for (int xx = -2; xx < sw + 2; xx++) {
                int px = sxp + xx, py = y0 + yy;
                if (px < 0 || px >= rw || py < 0 || py >= rh) continue;
                if (xx < 0 || yy < 0 || xx >= sw || yy >= sw)
                    g_framebuf[py * rw + px] = sel ? 0xffffffffu : 0xff202020u;
                else
                    g_framebuf[py * rw + px] =
                        g_tex[blk][1][(yy * TEX / sw) * TEX + (xx * TEX / sw)];
            }
    }
}

/* ----- adaptive resolution ------------------------------------------------
   The renderer is a software raycaster, so cost scales with the pixel count.
   Rather than assume a machine speed, the game measures its own frame time and
   trades internal resolution for frame rate. The image is upscaled to the
   window either way, so a drop costs sharpness, not framing. */
#ifndef HEADLESS_TEST
static const float g_res_scale[] = {1.00f, 0.85f, 0.72f, 0.60f, 0.50f, 0.42f, 0.35f};
#define RES_LEVELS ((int)(sizeof g_res_scale / sizeof g_res_scale[0]))
static int g_res_level = 0;
static int g_autores = 1;

static void set_res_level(int lv) {
    if (lv < 0) lv = 0;
    if (lv >= RES_LEVELS) lv = RES_LEVELS - 1;
    g_res_level = lv;
    int w = (int)(RENDER_W * g_res_scale[lv]) & ~1;
    int h = (int)(RENDER_H * g_res_scale[lv]) & ~1;
    g_rw = w < 64 ? 64 : w;
    g_rh = h < 36 ? 36 : h;
}
#endif /* !HEADLESS_TEST */

/* Fills g_view for the current camera and render size. */
static void view_setup(void) {
    camera_basis(&g_view.fwd, &g_view.right, &g_view.up);
    g_view.ox = g_px; g_view.oy = g_py; g_view.oz = g_pz;
    g_view.tan_v = tanf(FOV_DEG * 0.5f * (float)M_PI / 180.0f);
    float ah = g_view.tan_v * ((float)RENDER_W / RENDER_H);
    g_view.su0 = (2.0f * 0.5f / g_rw - 1.0f) * ah;
    g_view.dsu = (2.0f / g_rw) * ah;

    const int r1 = 120, g1 = 170, b1 = 235;      /* sky top */
    for (int y = 0; y < g_rh; y++) {
        float t = (float)y / g_rh;
        g_view.sky_r[y] = (int)(r1 + (SKY_BOT_R - r1) * t);
        g_view.sky_g[y] = (int)(g1 + (SKY_BOT_G - g1) * t);
        g_view.sky_b[y] = (int)(b1 + (SKY_BOT_B - b1) * t);
    }
}

#ifndef HEADLESS_TEST
/* ----- render worker pool -------------------------------------------------
   Raycasting is embarrassingly parallel: every scanline is independent and
   the only shared state (world, textures, camera) is read-only for the whole
   frame. Worker i renders rows i, i+N, i+2N...; the main thread takes row 0's
   share itself and then waits for the others. Threads are created once and
   parked on an event, so a frame costs two kernel handoffs, not N thread
   creations. */
static HANDLE g_ev_start[MAX_THREADS];
static HANDLE g_ev_done[MAX_THREADS];
static HANDLE g_thread[MAX_THREADS];
static int    g_nthreads = 1;                 /* including the main thread   */
static volatile int g_threads_quit = 0;

static DWORD WINAPI render_worker(LPVOID arg) {
    int id = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(g_ev_start[id], INFINITE);
        if (g_threads_quit) return 0;
        render_rows(id, g_nthreads);
        SetEvent(g_ev_done[id]);
    }
}

static void render_threads_init(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    if (n < 1) n = 1;
    if (n > MAX_THREADS) n = MAX_THREADS;

    for (int i = 1; i < n; i++) {
        g_ev_start[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_ev_done[i]  = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (g_ev_start[i] && g_ev_done[i])
            g_thread[i] = CreateThread(NULL, 0, render_worker, (LPVOID)(intptr_t)i, 0, NULL);
        if (!g_thread[i]) {                   /* fall back to fewer threads  */
            if (g_ev_start[i]) { CloseHandle(g_ev_start[i]); g_ev_start[i] = NULL; }
            if (g_ev_done[i])  { CloseHandle(g_ev_done[i]);  g_ev_done[i]  = NULL; }
            break;
        }
        g_nthreads = i + 1;                   /* only count threads we got   */
    }
}

static void render_threads_shutdown(void) {
    g_threads_quit = 1;
    for (int i = 1; i < g_nthreads; i++) SetEvent(g_ev_start[i]);
    for (int i = 1; i < g_nthreads; i++) {
        if (g_thread[i]) { WaitForSingleObject(g_thread[i], 1000); CloseHandle(g_thread[i]); }
        if (g_ev_start[i]) CloseHandle(g_ev_start[i]);
        if (g_ev_done[i])  CloseHandle(g_ev_done[i]);
    }
    g_nthreads = 1;
}
#endif /* !HEADLESS_TEST */

static void render_frame(void) {
    view_setup();
#ifndef HEADLESS_TEST
    if (g_nthreads > 1) {
        for (int i = 1; i < g_nthreads; i++) SetEvent(g_ev_start[i]);
        render_rows(0, g_nthreads);
        WaitForMultipleObjects(g_nthreads - 1, &g_ev_done[1], TRUE, INFINITE);
        render_overlay();
        return;
    }
#endif
    render_rows(0, 1);
    render_overlay();
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
        if (w == 'K') save_world("world.sav");
        if (w == 'L') load_world("world.sav");
        if (w >= '1' && w <= '9') {
            int idx = (int)w - '0';          /* 1..9 -> block ids 1..9 */
            if (idx < B_COUNT) g_selected = idx;
        }
        if (w == '0') g_selected = B_GLASS;
        if (w == 'T') g_autores = !g_autores;
        if (w == VK_OEM_MINUS || w == VK_SUBTRACT) { g_autores = 0; set_res_level(g_res_level + 1); }
        if (w == VK_OEM_PLUS  || w == VK_ADD)      { g_autores = 0; set_res_level(g_res_level - 1); }
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
    render_threads_init();
    gen_world((unsigned)GetTickCount());
    load_world("world.sav");   /* resume a saved world if one exists */

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    int mouse_init = 0, cursor_hidden = 0;
    float ft_avg = 1.0f / 60.0f;      /* smoothed frame time, seconds */
    float since_res = 0, since_title = 1e9f;
    HDC hdc = GetDC(g_hwnd);
    SetStretchBltMode(hdc, COLORONCOLOR);   /* cheapest upscale filter */

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
            /* ShowCursor keeps a counter, so this must be edge-triggered --
               calling it every frame drove the counter to -infinity and left
               the cursor stuck hidden after alt-tabbing away. */
            if (!cursor_hidden) { ShowCursor(FALSE); cursor_hidden = 1; }
        } else {
            mouse_init = 0;
            if (cursor_hidden) { ShowCursor(TRUE); cursor_hidden = 0; }
            Sleep(20);          /* don't burn a core in the background */
        }

        if (g_break_req) { do_break(); g_break_req = 0; }
        if (g_place_req) { do_place(); g_place_req = 0; }

        update_player(dt);

        /* --- adaptive resolution ---------------------------------------- */
        ft_avg += (dt - ft_avg) * 0.1f;
        since_res += dt;
        /* Only judge speed while focused -- a backgrounded frame is padded
           with Sleep() and would drag the resolution down for no reason. */
        if (g_autores && g_focused && since_res > 0.4f) {
            if (ft_avg > 1.0f / 45.0f && g_res_level < RES_LEVELS - 1) {
                set_res_level(g_res_level + 1); since_res = 0;
            } else if (ft_avg < 1.0f / 110.0f && g_res_level > 0) {
                set_res_level(g_res_level - 1); since_res = 0;
            }
        }

        render_frame();

        bmi.bmiHeader.biWidth  =  g_rw;
        bmi.bmiHeader.biHeight = -g_rh;          /* top-down */
        StretchDIBits(hdc, 0, 0, g_client_w, g_client_h,
                      0, 0, g_rw, g_rh,
                      g_framebuf, &bmi, DIB_RGB_COLORS, SRCCOPY);

        since_title += dt;
        if (since_title > 0.5f) {
            char title[192];
            snprintf(title, sizeof title,
                     "MiniCraft - voxel sandbox  |  %.0f FPS  |  %dx%d%s  |  "
                     "%d thread%s  |  %d textures",
                     ft_avg > 0 ? 1.0f / ft_avg : 0.0f, g_rw, g_rh,
                     g_autores ? "" : " (locked)",
                     g_nthreads, g_nthreads == 1 ? "" : "s", g_assets_loaded);
            SetWindowTextA(g_hwnd, title);
            since_title = 0;
        }
    }

    render_threads_shutdown();
    ReleaseDC(g_hwnd, hdc);
    return 0;
}

#endif /* !HEADLESS_TEST */
