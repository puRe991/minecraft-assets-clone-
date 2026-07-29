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
 *   1..9, 0          select block to place
 *   K / L            save / load the world (world.sav)
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
#include <stdarg.h>
#include <string.h>
#include <time.h>
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

/* ---- game state machine (menus) ---- */
enum { ST_MENU, ST_CREATE, ST_SETTINGS, ST_PLAY };
static int g_state = ST_MENU;
static int g_paused = 0;             /* in-game pause overlay              */
static int g_console_open = 0;
static int g_menu_sel = 0;           /* highlighted button / settings row  */
static float g_menu_yaw = 0;         /* rotating panorama behind the menu  */
static int g_draw_hud = 1;           /* draw crosshair + hotbar in render   */

static char g_input[128];            /* text entry (seed / console)        */
static int  g_input_len = 0;

/* ---- adjustable settings ---- */
static float g_fov = 70.0f;
static float g_render_dist = 96.0f;
static float g_sensitivity = 0.25f;  /* x1000 -> radians per pixel         */
static float g_movespeed = 1.0f;
static float g_daylight = 1.0f;      /* 1 = day, 0 = night                 */
static unsigned g_seed = 2024;

/* ---- console output ring buffer ---- */
#define CON_LINES 10
static char g_console[CON_LINES][96];
static int  g_console_n = 0;

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
    /* up = cross(fwd, right)  (points toward world +Y, not -Y) */
    up->x = fwd->y * right->z - fwd->z * right->y;
    up->y = fwd->z * right->x - fwd->x * right->z;
    up->z = fwd->x * right->y - fwd->y * right->x;
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
    float tanf_ = tanf(g_fov * 0.5f * (float)M_PI / 180.0f);
    float aspect = (float)RENDER_W / RENDER_H;
    float maxray = g_render_dist;
    float dl = g_daylight; if (dl < 0.12f) dl = 0.12f;   /* keep a little moonlight */

    const uint32_t sky_top = rgb((int)(120 * dl), (int)(170 * dl), (int)(235 * dl));
    const uint32_t sky_bot = rgb((int)(200 * dl), (int)(225 * dl), (int)(250 * dl));

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

            if (raycast(g_px, g_py, g_pz, dx, dy, dz, maxray,
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

                /* face lighting (scaled by daylight) */
                float lf = (ny > 0) ? 1.0f : (ny < 0) ? 0.55f : (nx != 0 ? 0.8f : 0.68f);
                lf *= dl;
                /* distance fog toward sky */
                float fog = dist / maxray; if (fog > 1) fog = 1;
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

    if (!g_draw_hud) return;

    /* crosshair */
    int cxp = RENDER_W / 2, cyp = RENDER_H / 2;
    for (int i = -5; i <= 5; i++) {
        g_framebuf[cyp * RENDER_W + (cxp + i)] ^= 0x00ffffff;
        g_framebuf[(cyp + i) * RENDER_W + cxp] ^= 0x00ffffff;
    }

    /* hotbar: block ids 1..10 (GLASS = 10), selected slot highlighted */
    {
        const int slots = 10, sw = 22, gap = 2;
        int tot = slots * (sw + gap) - gap;
        int x0 = (RENDER_W - tot) / 2, y0 = RENDER_H - sw - 6;
        for (int k = 0; k < slots; k++) {
            int blk = k + 1;
            int sxp = x0 + k * (sw + gap);
            int sel = (blk == g_selected);
            for (int yy = -2; yy < sw + 2; yy++)
                for (int xx = -2; xx < sw + 2; xx++) {
                    int px = sxp + xx, py = y0 + yy;
                    if (px < 0 || px >= RENDER_W || py < 0 || py >= RENDER_H) continue;
                    if (xx < 0 || yy < 0 || xx >= sw || yy >= sw)
                        g_framebuf[py * RENDER_W + px] = sel ? 0xffffffffu : 0xff202020u;
                    else
                        g_framebuf[py * RENDER_W + px] =
                            g_tex[blk][1][(yy * TEX / sw) * TEX + (xx * TEX / sw)];
                }
        }
    }
}

/* ----------------------------------------------------------------------- */
/* Text / UI overlay (framebuffer bitmap font)                              */
/* ----------------------------------------------------------------------- */

/* 5x7 font, columns left-to-right, LSB = top row. ASCII 0x20..0x5F. */
static const unsigned char FONT[64][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},
};

static void fb_px(int x, int y, uint32_t c) {
    if (x >= 0 && x < RENDER_W && y >= 0 && y < RENDER_H) g_framebuf[y * RENDER_W + x] = c;
}

static void fb_fill(int x, int y, int w, int h, uint32_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) fb_px(x + i, y + j, c);
}

/* darken a rectangle (simple overlay for panels) */
static void fb_dim(int x, int y, int w, int h, int amount) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int px = x + i, py = y + j;
            if (px < 0 || px >= RENDER_W || py < 0 || py >= RENDER_H) continue;
            uint32_t c = g_framebuf[py * RENDER_W + px];
            int r = ((c >> 16) & 0xff) * (255 - amount) / 255;
            int g = ((c >> 8) & 0xff) * (255 - amount) / 255;
            int b = (c & 0xff) * (255 - amount) / 255;
            g_framebuf[py * RENDER_W + px] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
}

static int fb_char(int x, int y, char ch, uint32_t col, int scale) {
    int c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z') c -= 32;      /* map lowercase to uppercase */
    if (c < 32 || c > 95) c = '?';
    const unsigned char *g = FONT[c - 32];
    for (int col5 = 0; col5 < 5; col5++)
        for (int row = 0; row < 7; row++)
            if (g[col5] & (1 << row))
                fb_fill(x + col5 * scale, y + row * scale, scale, scale, col);
    return 6 * scale;                        /* advance (5 + 1 spacing)     */
}

static int fb_text(int x, int y, const char *s, uint32_t col, int scale) {
    int x0 = x;
    for (; *s; s++) x += fb_char(x, y, *s, col, scale);
    return x - x0;
}

static int fb_text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

static void fb_text_center(int cx, int y, const char *s, uint32_t col, int scale) {
    fb_text(cx - fb_text_w(s, scale) / 2, y, s, col, scale);
}

/* ----------------------------------------------------------------------- */
/* Console output + commands                                                */
/* ----------------------------------------------------------------------- */

static void con_log(const char *fmt, ...) {
    if (g_console_n >= CON_LINES) {
        for (int i = 1; i < CON_LINES; i++) memcpy(g_console[i - 1], g_console[i], sizeof g_console[0]);
        g_console_n = CON_LINES - 1;
    }
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_console[g_console_n], sizeof g_console[0], fmt, ap);
    va_end(ap);
    g_console_n++;
}

static int block_from_name(const char *n) {
    struct { const char *name; int id; } t[] = {
        {"air", B_AIR}, {"grass", B_GRASS}, {"dirt", B_DIRT}, {"stone", B_STONE},
        {"cobblestone", B_COBBLE}, {"cobble", B_COBBLE}, {"log", B_LOG}, {"oak_log", B_LOG},
        {"wood", B_LOG}, {"leaves", B_LEAVES}, {"sand", B_SAND}, {"planks", B_PLANKS},
        {"oak_planks", B_PLANKS}, {"water", B_WATER}, {"glass", B_GLASS},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (strcmp(n, t[i].name) == 0) return t[i].id;
    int v = atoi(n);
    if (v > 0 && v < B_COUNT) return v;
    return -1;
}

/* Execute a console command line (without a leading '/'). */
static void exec_command(const char *line) {
    char buf[128];
    snprintf(buf, sizeof buf, "%s", line);
    con_log("> %s", line);
    char *cmd = strtok(buf, " ");
    if (!cmd) return;

    if (!strcmp(cmd, "help")) {
        con_log("help seed time tp give setblock");
        con_log("fill gamemode fly speed fov regen clear");
    } else if (!strcmp(cmd, "seed")) {
        con_log("seed: %u", g_seed);
    } else if (!strcmp(cmd, "time")) {
        char *a = strtok(NULL, " ");
        if (!a) con_log("usage: time day|night|<0..1>");
        else if (!strcmp(a, "day")) { g_daylight = 1.0f; con_log("time set day"); }
        else if (!strcmp(a, "night")) { g_daylight = 0.15f; con_log("time set night"); }
        else { g_daylight = (float)atof(a); con_log("daylight %.2f", g_daylight); }
    } else if (!strcmp(cmd, "tp")) {
        char *ax = strtok(NULL, " "), *ay = strtok(NULL, " "), *az = strtok(NULL, " ");
        if (ax && ay && az) { g_px = (float)atof(ax); g_py = (float)atof(ay); g_pz = (float)atof(az);
            g_vx = g_vy = g_vz = 0; con_log("teleported"); }
        else con_log("usage: tp x y z");
    } else if (!strcmp(cmd, "give")) {
        char *a = strtok(NULL, " ");
        int b = a ? block_from_name(a) : -1;
        if (b > 0) { g_selected = b; con_log("selected block %d", b); }
        else con_log("unknown block");
    } else if (!strcmp(cmd, "setblock")) {
        char *ax = strtok(NULL, " "), *ay = strtok(NULL, " "), *az = strtok(NULL, " "), *ab = strtok(NULL, " ");
        int b = ab ? block_from_name(ab) : -1;
        if (ax && ay && az && b >= 0) { set_block(atoi(ax), atoi(ay), atoi(az), (uint8_t)b); con_log("ok"); }
        else con_log("usage: setblock x y z block");
    } else if (!strcmp(cmd, "fill")) {
        char *v[7]; int ok = 1;
        for (int i = 0; i < 7; i++) { v[i] = strtok(NULL, " "); if (!v[i]) ok = 0; }
        int b = ok ? block_from_name(v[6]) : -1;
        if (ok && b >= 0) {
            int x1 = atoi(v[0]), y1 = atoi(v[1]), z1 = atoi(v[2]);
            int x2 = atoi(v[3]), y2 = atoi(v[4]), z2 = atoi(v[5]);
            if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
            if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
            if (z1 > z2) { int t = z1; z1 = z2; z2 = t; }
            long n = 0;
            for (int x = x1; x <= x2; x++) for (int y = y1; y <= y2; y++) for (int z = z1; z <= z2; z++)
                { set_block(x, y, z, (uint8_t)b); n++; }
            con_log("filled %ld blocks", n);
        } else con_log("usage: fill x1 y1 z1 x2 y2 z2 block");
    } else if (!strcmp(cmd, "gamemode")) {
        char *a = strtok(NULL, " ");
        if (a && !strcmp(a, "creative")) { g_fly = 1; con_log("creative"); }
        else if (a && !strcmp(a, "survival")) { g_fly = 0; con_log("survival"); }
        else con_log("usage: gamemode creative|survival");
    } else if (!strcmp(cmd, "fly")) {
        g_fly = !g_fly; con_log("fly %s", g_fly ? "on" : "off");
    } else if (!strcmp(cmd, "speed")) {
        char *a = strtok(NULL, " ");
        if (a) { g_movespeed = (float)atof(a); con_log("speed %.2f", g_movespeed); }
        else con_log("usage: speed <n>");
    } else if (!strcmp(cmd, "fov")) {
        char *a = strtok(NULL, " ");
        if (a) { g_fov = (float)atof(a); if (g_fov < 30) g_fov = 30; if (g_fov > 110) g_fov = 110;
            con_log("fov %.0f", g_fov); }
    } else if (!strcmp(cmd, "regen")) {
        char *a = strtok(NULL, " ");
        if (a) g_seed = (unsigned)strtoul(a, NULL, 10);
        gen_world(g_seed); con_log("regenerated seed %u", g_seed);
    } else if (!strcmp(cmd, "clear")) {
        g_console_n = 0;
    } else {
        con_log("unknown command: %s", cmd);
    }
}

/* Hash an arbitrary seed string to an unsigned value (numeric seeds pass
   through as their integer value, like Minecraft). */
static unsigned seed_from_string(const char *s) {
    if (!*s) return (unsigned)time(NULL);
    char *end; unsigned long v = strtoul(s, &end, 10);
    if (*end == 0) return (unsigned)v;      /* pure number */
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

/* ----------------------------------------------------------------------- */
/* Menu / settings / console screens (compose into g_framebuf)              */
/* ----------------------------------------------------------------------- */

#define BTN_W 180
#define BTN_H 24
#define BTN_STEP 32
#define BTN_Y0 150
#define BTN_X ((RENDER_W - BTN_W) / 2)

static void draw_button(int i, const char *label) {
    int x = BTN_X, y = BTN_Y0 + i * BTN_STEP;
    int sel = (i == g_menu_sel);
    fb_fill(x, y, BTN_W, BTN_H, sel ? 0xff787878u : 0xff404040u);
    fb_fill(x, y, BTN_W, 1, 0xff909090u);
    fb_fill(x, y + BTN_H - 1, BTN_W, 1, 0xff202020u);
    fb_text_center(RENDER_W / 2, y + (BTN_H - 7) / 2, label, 0xffffffffu, 1);
}

/* which button index is at internal-resolution point (mx,my), or -1 */
static int button_at(int mx, int my, int count) {
    for (int i = 0; i < count; i++) {
        int y = BTN_Y0 + i * BTN_STEP;
        if (mx >= BTN_X && mx < BTN_X + BTN_W && my >= y && my < y + BTN_H) return i;
    }
    return -1;
}

static const char *g_menu_items[] = {"SINGLEPLAYER", "SETTINGS", "QUIT"};
static const char *g_pause_items[] = {"RESUME", "SETTINGS", "SAVE & QUIT TO MENU"};

static void render_menu(void) {
    /* rotating panorama */
    g_draw_hud = 0;
    g_yaw = g_menu_yaw; g_pitch = -0.12f;
    render_frame();
    g_draw_hud = 1;
    fb_dim(0, 0, RENDER_W, RENDER_H, 90);
    fb_text_center(RENDER_W / 2, 50, "MINICRAFT", 0xffffffffu, 4);
    fb_text_center(RENDER_W / 2, 92, "A VOXEL SANDBOX", 0xffb0d060u, 1);
    for (int i = 0; i < 3; i++) draw_button(i, g_menu_items[i]);
    fb_text_center(RENDER_W / 2, RENDER_H - 12, "ARROWS + ENTER, OR CLICK", 0xff909090u, 1);
}

/* render the rotating world (no HUD) as a background for menu screens */
static void render_frame_menu_bg(void) {
    g_draw_hud = 0;
    g_menu_yaw += 0.004f;
    g_yaw = g_menu_yaw; g_pitch = -0.12f;
    render_frame();
    g_draw_hud = 1;
}

static void render_create(void) {
    fb_dim(0, 0, RENDER_W, RENDER_H, 110);
    fb_text_center(RENDER_W / 2, 50, "CREATE NEW WORLD", 0xffffffffu, 2);
    fb_text_center(RENDER_W / 2, 100, "SEED (BLANK = RANDOM):", 0xffb0b0b0u, 1);
    int fx = (RENDER_W - 220) / 2, fy = 118;
    fb_fill(fx, fy, 220, 20, 0xff202020u);
    fb_fill(fx, fy, 220, 1, 0xff808080u);
    char shown[130];
    snprintf(shown, sizeof shown, "%s_", g_input);
    fb_text(fx + 6, fy + 6, shown, 0xffffffffu, 1);
    draw_button(0, "CREATE WORLD");
    draw_button(1, "BACK");
    fb_text_center(RENDER_W / 2, RENDER_H - 12, "TYPE A SEED, ENTER TO CREATE", 0xff909090u, 1);
}

static const char *g_settings_names[] = {"FOV", "RENDER DISTANCE", "MOUSE SENSITIVITY", "MOVE SPEED", "DAYLIGHT"};
static float *settings_ptr(int i) {
    switch (i) { case 0: return &g_fov; case 1: return &g_render_dist;
                 case 2: return &g_sensitivity; case 3: return &g_movespeed; default: return &g_daylight; }
}
static void settings_adjust(int i, int dir) {
    float *p = settings_ptr(i);
    float step[] = {5, 8, 0.05f, 0.1f, 0.1f};
    float lo[]   = {30, 24, 0.05f, 0.3f, 0.1f};
    float hi[]   = {110, 240, 1.0f, 4.0f, 1.0f};
    *p += dir * step[i];
    if (*p < lo[i]) *p = lo[i];
    if (*p > hi[i]) *p = hi[i];
}

static void render_settings(void) {
    fb_dim(0, 0, RENDER_W, RENDER_H, 120);
    fb_text_center(RENDER_W / 2, 40, "SETTINGS", 0xffffffffu, 3);
    for (int i = 0; i < 5; i++) {
        int y = 90 + i * 24;
        int sel = (i == g_menu_sel);
        uint32_t col = sel ? 0xffffff40u : 0xffe0e0e0u;
        char val[48];
        float v = *settings_ptr(i);
        if (i == 0 || i == 1) snprintf(val, sizeof val, "%.0f", v);
        else snprintf(val, sizeof val, "%.2f", v);
        fb_text(80, y, g_settings_names[i], col, 1);
        char line[64]; snprintf(line, sizeof line, "< %s >", val);
        fb_text(RENDER_W - 80 - fb_text_w(line, 1), y, line, col, 1);
    }
    /* BACK button (settings row index 5) */
    int bx = BTN_X, by = 90 + 5 * 24 + 10;
    int selb = (g_menu_sel == 5);
    fb_fill(bx, by, BTN_W, BTN_H, selb ? 0xff787878u : 0xff404040u);
    fb_fill(bx, by, BTN_W, 1, 0xff909090u);
    fb_text_center(RENDER_W / 2, by + (BTN_H - 7) / 2, "BACK", 0xffffffffu, 1);
    fb_text_center(RENDER_W / 2, RENDER_H - 12, "UP/DOWN SELECT, LEFT/RIGHT ADJUST", 0xff909090u, 1);
}
static int settings_back_at(int mx, int my) {
    int by = 90 + 5 * 24 + 10;
    return (mx >= BTN_X && mx < BTN_X + BTN_W && my >= by && my < by + BTN_H);
}

static void render_console_overlay(void) {
    int h = 12 + CON_LINES * 9 + 14;
    fb_dim(0, RENDER_H - h, RENDER_W, h, 150);
    for (int i = 0; i < g_console_n; i++)
        fb_text(6, RENDER_H - h + 6 + i * 9, g_console[i], 0xffd0d0d0u, 1);
    char line[140];
    snprintf(line, sizeof line, "/%s_", g_input);
    fb_fill(0, RENDER_H - 12, RENDER_W, 12, 0xff101010u);
    fb_text(4, RENDER_H - 10, line, 0xffffffffu, 1);
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

    float speed = (g_fly ? 9.0f : 4.5f) * g_movespeed;

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

/* edge-triggered menu navigation events, consumed by the main loop */
static volatile int g_nav = 0;       /* -1 up, +1 down                     */
static volatile int g_navlr = 0;     /* -1 left, +1 right                  */
static volatile int g_enter = 0;
static volatile int g_esc = 0;
static volatile int g_click_x = -1, g_click_y = -1;

static void start_world(unsigned seed) {
    g_seed = seed;
    gen_world(seed);
    g_state = ST_PLAY;
    g_paused = 0;
    g_console_open = 0;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_DESTROY: g_running = 0; PostQuitMessage(0); return 0;
    case WM_SIZE:    g_client_w = LOWORD(l); g_client_h = HIWORD(l); return 0;
    case WM_SETFOCUS:  g_focused = 1; return 0;
    case WM_KILLFOCUS: g_focused = 0; return 0;

    case WM_CHAR:
        if (g_state == ST_CREATE || g_console_open) {
            char c = (char)w;
            if (c == '\b') { if (g_input_len > 0) g_input[--g_input_len] = 0; }
            else if (c >= 32 && c < 127 && g_input_len < (int)sizeof(g_input) - 1) {
                g_input[g_input_len++] = c; g_input[g_input_len] = 0;
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (w < 256) g_keys[w] = 1;
        if (w == VK_UP)    g_nav = -1;
        if (w == VK_DOWN)  g_nav = 1;
        if (w == VK_LEFT)  g_navlr = -1;
        if (w == VK_RIGHT) g_navlr = 1;
        if (w == VK_RETURN) g_enter = 1;
        if (w == VK_ESCAPE) g_esc = 1;

        /* gameplay hotkeys only while actively playing */
        if (g_state == ST_PLAY && !g_paused && !g_console_open) {
            if (w == 'F') g_fly = !g_fly;
            if (w == 'R') { g_seed = (unsigned)GetTickCount(); gen_world(g_seed); }
            if (w == 'K') save_world("world.sav");
            if (w == 'L') load_world("world.sav");
            if (w >= '1' && w <= '9') { int i = (int)w - '0'; if (i < B_COUNT) g_selected = i; }
            if (w == '0') g_selected = B_GLASS;
            if (w == 'T' || w == VK_OEM_2) {   /* T or '/' opens console */
                g_console_open = 1; g_input_len = 0; g_input[0] = 0;
                if (w == VK_OEM_2) { /* prefix nothing; user types command */ }
            }
        }
        return 0;

    case WM_KEYUP:
        if (w < 256) g_keys[w] = 0;
        return 0;

    case WM_LBUTTONDOWN:
        if (g_state == ST_PLAY && !g_paused && !g_console_open) g_break_req = 1;
        else { g_click_x = LOWORD(l); g_click_y = HIWORD(l); }
        return 0;
    case WM_RBUTTONDOWN:
        if (g_state == ST_PLAY && !g_paused && !g_console_open) g_place_req = 1;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void center_mouse(void) {
    RECT rc; GetClientRect(g_hwnd, &rc);
    POINT p; p.x = (rc.right - rc.left) / 2; p.y = (rc.bottom - rc.top) / 2;
    ClientToScreen(g_hwnd, &p);
    SetCursorPos(p.x, p.y);
}

/* map a client-pixel click to internal 480x270 coordinates */
static void click_to_internal(int *ix, int *iy) {
    *ix = g_client_w ? g_click_x * RENDER_W / g_client_w : 0;
    *iy = g_client_h ? g_click_y * RENDER_H / g_client_h : 0;
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
    gen_world(g_seed);          /* a world to spin behind the main menu */

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

        int playing = (g_state == ST_PLAY && !g_paused && !g_console_open);

        /* ---- mouse look (only while actively playing) ---- */
        if (g_focused && playing) {
            RECT rc; GetClientRect(g_hwnd, &rc);
            int cx = (rc.right - rc.left) / 2, cy = (rc.bottom - rc.top) / 2;
            POINT p; GetCursorPos(&p); ScreenToClient(g_hwnd, &p);
            if (mouse_init) {
                float s = g_sensitivity * 0.01f;
                g_yaw   -= (float)(p.x - cx) * s;
                g_pitch -= (float)(p.y - cy) * s;
                float lim = 1.55f;
                if (g_pitch > lim) g_pitch = lim;
                if (g_pitch < -lim) g_pitch = -lim;
            }
            center_mouse();
            mouse_init = 1;
            ShowCursor(FALSE);
        } else {
            mouse_init = 0;
            ShowCursor(TRUE);
        }

        /* ---- per-state update + render ---- */
        if (g_state == ST_MENU) {
            g_menu_yaw += dt * 0.15f;
            if (g_nav) { g_menu_sel = (g_menu_sel + (g_nav > 0 ? 1 : 2)) % 3; g_nav = 0; }
            int click = -1;
            if (g_click_x >= 0) { int ix, iy; click_to_internal(&ix, &iy); click = button_at(ix, iy, 3); g_click_x = -1; }
            if (click >= 0) { g_menu_sel = click; g_enter = 1; }
            if (g_enter) {
                g_enter = 0;
                if (g_menu_sel == 0) { g_state = ST_CREATE; g_menu_sel = 0; g_input_len = 0; g_input[0] = 0; }
                else if (g_menu_sel == 1) { g_state = ST_SETTINGS; g_menu_sel = 0; }
                else { g_running = 0; }
            }
            if (g_esc) { g_esc = 0; g_running = 0; }
            render_menu();
        }
        else if (g_state == ST_CREATE) {
            if (g_nav) { g_menu_sel ^= 1; g_nav = 0; }
            int click = -1;
            if (g_click_x >= 0) { int ix, iy; click_to_internal(&ix, &iy); click = button_at(ix, iy, 2); g_click_x = -1; }
            if (click == 0 || (g_enter)) { g_enter = 0; start_world(seed_from_string(g_input)); }
            else if (click == 1 || g_esc) { g_esc = 0; g_state = ST_MENU; g_menu_sel = 0; }
            render_frame_menu_bg();
            render_create();
        }
        else if (g_state == ST_SETTINGS) {
            if (g_nav) { g_menu_sel = (g_menu_sel + (g_nav > 0 ? 1 : 5)) % 6; g_nav = 0; }
            if (g_navlr && g_menu_sel < 5) { settings_adjust(g_menu_sel, g_navlr); g_navlr = 0; }
            g_navlr = 0;
            int back = 0;
            if (g_click_x >= 0) { int ix, iy; click_to_internal(&ix, &iy); if (settings_back_at(ix, iy)) back = 1; g_click_x = -1; }
            if ((g_enter && g_menu_sel == 5) || back || g_esc) {
                g_enter = 0; g_esc = 0;
                g_state = (g_paused ? ST_PLAY : ST_MENU); g_menu_sel = 0;
            } else g_enter = 0;
            render_frame_menu_bg();
            render_settings();
        }
        else { /* ST_PLAY */
            if (g_console_open) {
                if (g_enter) { g_enter = 0; if (g_input_len) exec_command(g_input); g_input_len = 0; g_input[0] = 0; }
                if (g_esc)   { g_esc = 0; g_console_open = 0; }
            } else if (g_paused) {
                if (g_nav) { g_menu_sel = (g_menu_sel + (g_nav > 0 ? 1 : 2)) % 3; g_nav = 0; }
                int click = -1;
                if (g_click_x >= 0) { int ix, iy; click_to_internal(&ix, &iy); click = button_at(ix, iy, 3); g_click_x = -1; }
                if (click >= 0) { g_menu_sel = click; g_enter = 1; }
                if (g_enter) {
                    g_enter = 0;
                    if (g_menu_sel == 0) g_paused = 0;
                    else if (g_menu_sel == 1) { g_state = ST_SETTINGS; g_menu_sel = 0; }
                    else { save_world("world.sav"); g_state = ST_MENU; g_paused = 0; g_menu_sel = 0; }
                }
                if (g_esc) { g_esc = 0; g_paused = 0; }
            } else {
                if (g_esc) { g_esc = 0; g_paused = 1; g_menu_sel = 0; }
                if (g_break_req) { do_break(); g_break_req = 0; }
                if (g_place_req) { do_place(); g_place_req = 0; }
                update_player(dt);
            }

            render_frame();
            if (g_console_open) render_console_overlay();
            if (g_paused) {
                fb_dim(0, 0, RENDER_W, RENDER_H, 120);
                fb_text_center(RENDER_W / 2, 70, "PAUSED", 0xffffffffu, 3);
                for (int i = 0; i < 3; i++) draw_button(i, g_pause_items[i]);
            }
        }

        /* clear stale one-shot events */
        g_nav = 0; g_navlr = 0;

        StretchDIBits(hdc, 0, 0, g_client_w, g_client_h,
                      0, 0, RENDER_W, RENDER_H,
                      g_framebuf, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    ReleaseDC(g_hwnd, hdc);
    return 0;
}

#endif /* !HEADLESS_TEST */
