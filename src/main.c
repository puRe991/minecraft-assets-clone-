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

#define WORLD_Y 64            /* fixed world height                         */
#define WATER_LEVEL 22
#define CH 16                 /* chunk size in X and Z                      */
#define LOADR 8               /* chunk load radius around the player        */
#define GS (2 * LOADR + 1)    /* loaded-chunk grid side (toroidal)          */

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
    /* biome blocks */
    B_SNOW, B_ICE, B_RED_SAND, B_TERRACOTTA, B_DRY_GRASS, B_PODZOL,
    B_MYCELIUM, B_SPRUCE_LEAVES, B_JUNGLE_LEAVES, B_BIRCH_LOG, B_CACTUS,
    B_COUNT
};

/* ----------------------------------------------------------------------- */
/* Globals                                                                  */
/* ----------------------------------------------------------------------- */

/* A chunk is CH x CH x WORLD_Y blocks. Loaded chunks live in a toroidal
   grid that follows the player, so the world streams "infinitely" in X/Z. */
typedef struct { int cx, cz, valid; uint8_t b[CH * CH * WORLD_Y]; } Chunk;
static Chunk g_chunks[GS * GS];

static uint32_t g_framebuf[RENDER_W * RENDER_H];
static uint32_t g_tex[B_COUNT][3][TEX * TEX];   /* [block][face 0=top,1=side,2=bottom] */

static float g_px = 8.5f, g_py = 40.0f, g_pz = 8.5f;
static float g_vx = 0, g_vy = 0, g_vz = 0;
static float g_yaw = 0.0f, g_pitch = 0.0f;
static int   g_onground = 0;
static int   g_fly = 0;

/* ---- inventory / game mode ---- */
typedef struct { uint8_t block; int count; } Slot;   /* count<0 => infinite (creative) */
#define INV_COLS 9
#define INV_ROWS 4                       /* 1 hotbar row + 3 storage rows      */
#define INV_SLOTS (INV_COLS * INV_ROWS)  /* 36, like Minecraft                 */
#define STACK_MAX 99
static Slot g_inv[INV_SLOTS];            /* slots 0..8 = hotbar               */
static int  g_hotbar_sel = 0;            /* selected hotbar slot 0..8         */
static Slot g_hand = {0, 0};             /* stack held by the cursor in the UI */
static int  g_gamemode = 0;              /* 0 = survival, 1 = creative         */
static int  g_inv_open = 0;
static int  g_health = 20;               /* survival health, 20 = 10 hearts    */
static float g_air_max_y = 0;            /* highest y while airborne (falls)   */
static int  g_was_ground = 1;
static int  g_mouse_ix = RENDER_W / 2, g_mouse_iy = RENDER_H / 2;

static inline int selected_block(void) { return g_inv[g_hotbar_sel].block; }

/* forward decls (definitions live in the gameplay section) */
static void inv_give(int block, int n);
static void set_gamemode(int m);
static void init_inventory(int mode);

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
static float g_daylight = 1.0f;      /* 0..1 brightness, derived from g_tod */
static float g_daylen = 240.0f;      /* seconds per full day/night cycle    */
static unsigned g_seed = 2024;

/* time of day: 0 = midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset */
static float g_tod = 0.30f;
static float daylight_from_tod(float t) { return 0.5f - 0.5f * cosf(2.0f * (float)M_PI * t); }

/* ---- console output ring buffer ---- */
#define CON_LINES 10
static char g_console[CON_LINES][96];
static int  g_console_n = 0;

/* ----------------------------------------------------------------------- */
/* World access                                                             */
/* ----------------------------------------------------------------------- */

/* floor-division and positive modulo (correct for negative coords) */
static inline int fdiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
static inline int fmodp(int a, int b) { int r = a % b; if (r < 0) r += b; return r; }

/* the ring cell that currently addresses chunk (cx,cz) */
static inline Chunk *chunk_cell(int cx, int cz) {
    return &g_chunks[fmodp(cz, GS) * GS + fmodp(cx, GS)];
}
/* the loaded chunk containing world column (x,z), or NULL if not resident */
static inline Chunk *chunk_of(int x, int z) {
    int cx = fdiv(x, CH), cz = fdiv(z, CH);
    Chunk *c = chunk_cell(cx, cz);
    return (c->valid && c->cx == cx && c->cz == cz) ? c : NULL;
}

static inline uint8_t get_block(int x, int y, int z) {
    if (y < 0 || y >= WORLD_Y) return B_AIR;
    Chunk *c = chunk_of(x, z);
    if (!c) return B_AIR;
    return c->b[(y * CH + fmodp(z, CH)) * CH + fmodp(x, CH)];
}
static inline void set_block(int x, int y, int z, uint8_t v) {
    if (y < 0 || y >= WORLD_Y) return;
    Chunk *c = chunk_of(x, z);
    if (!c) return;
    c->b[(y * CH + fmodp(z, CH)) * CH + fmodp(x, CH)] = v;
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

/* A tree of a given wood/leaf type. `conical` makes a spruce-style shape;
   otherwise a rounded (oak/birch/jungle) canopy. Kept within the chunk. */
static void place_tree_type(int x, int z, int ground, uint8_t logb, uint8_t leafb,
                            int minH, int randH, int conical) {
    int h = minH + (int)(rnd2(x * 7, z * 3) * randH);
    for (int i = 1; i <= h; i++) set_block(x, ground + i, z, logb);
    int top = ground + h;
    if (conical) {
        for (int dy = 0; dy <= h - 2; dy++) {
            int level = top - dy;
            int r = dy / 2; if (r > 2) r = 2;
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++) {
                    if (dx == 0 && dz == 0) continue;
                    if (abs(dx) == r && abs(dz) == r) continue;   /* clip corners */
                    if (get_block(x + dx, level, z + dz) == B_AIR)
                        set_block(x + dx, level, z + dz, leafb);
                }
        }
        set_block(x, top + 1, z, leafb);       /* tip */
    } else {
        for (int dy = -1; dy <= 2; dy++) {
            int r = (dy <= 0) ? 2 : 1;
            for (int dx = -r; dx <= r; dx++)
                for (int dz = -r; dz <= r; dz++) {
                    if (dx == 0 && dz == 0 && dy <= 0) continue;
                    if (abs(dx) == r && abs(dz) == r && (rnd2(x + dx, z + dz) < 0.4f)) continue;
                    int yy = top + dy;
                    if (get_block(x + dx, yy, z + dz) == B_AIR)
                        set_block(x + dx, yy, z + dz, leafb);
                }
        }
    }
}

static void place_cactus(int x, int z, int ground) {
    int h = 1 + (int)(rnd2(x * 3, z * 5) * 3.0f);
    for (int i = 1; i <= h; i++) set_block(x, ground + i, z, B_CACTUS);
}

/* terrain height at world column (x,z), deterministic from the seed.
   A low-frequency "continentalness" term dominates so oceans, coastlines and
   mountains form coherent regions, with a higher-frequency term adding local
   hills. This keeps biome classification (which reads this height) coherent. */
static int column_height(int x, int z) {
    float ox = (g_seed % 997) * 1.3f, oz = (g_seed % 733) * 1.7f;
    float cont   = fbm((x + ox) * 0.0055f, (z + oz) * 0.0055f);   /* continents */
    float detail = fbm((x + ox) * 0.045f,  (z + oz) * 0.045f);    /* local hills */
    float n = cont * 0.78f + detail * 0.22f;
    int h = (int)(8 + n * 42);
    if (h < 1) h = 1;
    if (h >= WORLD_Y) h = WORLD_Y - 1;
    return h;
}

/* ---- biomes (Minecraft-style set) ---- */
enum {
    BIO_OCEAN, BIO_FROZEN_OCEAN, BIO_BEACH, BIO_PLAINS, BIO_FOREST, BIO_BIRCH,
    BIO_DARK_FOREST, BIO_JUNGLE, BIO_SAVANNA, BIO_DESERT, BIO_BADLANDS,
    BIO_SWAMP, BIO_TAIGA, BIO_SNOWY, BIO_MOUNTAINS, BIO_MUSHROOM, BIO_COUNT
};

/* temperature + humidity fields (large, slow-varying regions) */
static void climate(int x, int z, float *t, float *hu) {
    float ox = (g_seed % 521) * 2.1f, oz = (g_seed % 389) * 1.7f;
    *t  = fbm((x + ox) * 0.0016f, (z + oz) * 0.0016f);
    *hu = fbm((x + ox + 4096) * 0.0016f, (z + oz + 4096) * 0.0016f);
}

/* Pick the biome for a world column from elevation + climate. */
static int biome_at(int x, int z) {
    int h0 = column_height(x, z);
    float t, hu; climate(x, z, &t, &hu);
    if (h0 <= WATER_LEVEL - 4) return (t < 0.28f) ? BIO_FROZEN_OCEAN : BIO_OCEAN;
    if (h0 <= WATER_LEVEL + 1) return BIO_BEACH;
    if (h0 > WATER_LEVEL + 24) return BIO_MOUNTAINS;
    if (fbm((x + 900) * 0.03f, (z - 900) * 0.03f) > 0.86f) return BIO_MUSHROOM;
    if (t < 0.25f) return (hu < 0.5f) ? BIO_SNOWY : BIO_TAIGA;
    if (t < 0.5f) {
        if (hu > 0.75f) return BIO_DARK_FOREST;
        if (hu > 0.52f) return BIO_FOREST;
        if (hu > 0.34f) return BIO_BIRCH;
        return BIO_PLAINS;
    }
    if (t < 0.72f) {
        if (hu > 0.72f) return BIO_JUNGLE;
        if (hu > 0.56f) return BIO_SWAMP;
        if (hu > 0.30f) return BIO_PLAINS;
        return BIO_SAVANNA;
    }
    if (hu > 0.62f) return BIO_JUNGLE;
    if (hu > 0.38f) return BIO_SAVANNA;
    if (hu > 0.20f) return BIO_DESERT;
    return BIO_BADLANDS;
}

/* Height after biome shaping (mountains rise, swamps flatten near water). */
static int surface_height(int x, int z) {
    int h = column_height(x, z);
    int b = biome_at(x, z);
    if (b == BIO_MOUNTAINS) { int over = h - (WATER_LEVEL + 24); if (over < 0) over = 0; h += (int)(over * 1.6f); }
    else if (b == BIO_SWAMP) { if (h > WATER_LEVEL + 2) h = WATER_LEVEL + 1 + (h - (WATER_LEVEL + 2)) / 3; }
    else if (b == BIO_DESERT || b == BIO_SAVANNA) { int m = WATER_LEVEL + 6; h = m + (h - m) * 3 / 4; }
    if (h < 1) h = 1;
    if (h >= WORLD_Y) h = WORLD_Y - 1;
    return h;
}

/* Top and sub-surface (filler) block for a biome column of height h. */
static void biome_surface(int b, int h, uint8_t *top, uint8_t *filler) {
    switch (b) {
        case BIO_OCEAN: case BIO_FROZEN_OCEAN: *top = B_SAND; *filler = B_DIRT; break;
        case BIO_BEACH:   *top = B_SAND; *filler = B_SAND; break;
        case BIO_DESERT:  *top = B_SAND; *filler = B_SAND; break;
        case BIO_BADLANDS:*top = B_RED_SAND; *filler = B_TERRACOTTA; break;
        case BIO_SNOWY:   *top = B_SNOW; *filler = B_DIRT; break;
        case BIO_SAVANNA: *top = B_DRY_GRASS; *filler = B_DIRT; break;
        case BIO_MUSHROOM:*top = B_MYCELIUM; *filler = B_DIRT; break;
        case BIO_MOUNTAINS:
            *top = (h > WATER_LEVEL + 34) ? B_SNOW : (h > WATER_LEVEL + 30) ? B_STONE : B_GRASS;
            *filler = (*top == B_GRASS) ? B_DIRT : B_STONE; break;
        default:          *top = B_GRASS; *filler = B_DIRT; break;   /* plains/forest/... */
    }
}

/* Generate chunk (cx,cz) into ring cell c. */
static void gen_chunk(Chunk *c, int cx, int cz) {
    c->cx = cx; c->cz = cz; c->valid = 1;
    memset(c->b, B_AIR, sizeof c->b);
    for (int lx = 0; lx < CH; lx++)
        for (int lz = 0; lz < CH; lz++) {
            int wx = cx * CH + lx, wz = cz * CH + lz;
            int b = biome_at(wx, wz);
            int h = surface_height(wx, wz);
            uint8_t top, filler; biome_surface(b, h, &top, &filler);
            for (int y = 0; y <= h; y++) {
                uint8_t bl;
                if (y == h)          bl = top;
                else if (y >= h - 3) bl = filler;
                else                 bl = B_STONE;
                c->b[(y * CH + lz) * CH + lx] = bl;
            }
            /* water (frozen oceans get an ice sheet on top) */
            for (int y = h + 1; y <= WATER_LEVEL; y++)
                c->b[(y * CH + lz) * CH + lx] = B_WATER;
            if (b == BIO_FROZEN_OCEAN && h < WATER_LEVEL)
                c->b[(WATER_LEVEL * CH + lz) * CH + lx] = B_ICE;
        }
    /* vegetation, kept in the chunk interior so canopies never cross a border */
    for (int lx = 2; lx < CH - 2; lx++)
        for (int lz = 2; lz < CH - 2; lz++) {
            int wx = cx * CH + lx, wz = cz * CH + lz;
            int b = biome_at(wx, wz);
            int h = surface_height(wx, wz);
            if (h <= WATER_LEVEL) continue;
            float r = rnd2(wx * 13 + 1, wz * 17 + 5);
            switch (b) {
                case BIO_FOREST:      if (r < 0.060f) place_tree_type(wx, wz, h, B_LOG, B_LEAVES, 4, 3, 0); break;
                case BIO_BIRCH:       if (r < 0.050f) place_tree_type(wx, wz, h, B_BIRCH_LOG, B_LEAVES, 5, 3, 0); break;
                case BIO_DARK_FOREST: if (r < 0.100f) place_tree_type(wx, wz, h, B_LOG, B_LEAVES, 5, 2, 0); break;
                case BIO_PLAINS:      if (r < 0.008f) place_tree_type(wx, wz, h, B_LOG, B_LEAVES, 4, 3, 0); break;
                case BIO_JUNGLE:      if (r < 0.070f) place_tree_type(wx, wz, h, B_LOG, B_JUNGLE_LEAVES, 8, 6, 0); break;
                case BIO_SAVANNA:     if (r < 0.012f) place_tree_type(wx, wz, h, B_LOG, B_LEAVES, 4, 2, 0); break;
                case BIO_TAIGA:       if (r < 0.050f) place_tree_type(wx, wz, h, B_LOG, B_SPRUCE_LEAVES, 6, 4, 1); break;
                case BIO_SNOWY:       if (r < 0.020f) place_tree_type(wx, wz, h, B_LOG, B_SPRUCE_LEAVES, 6, 3, 1); break;
                case BIO_SWAMP:       if (r < 0.015f) place_tree_type(wx, wz, h, B_LOG, B_LEAVES, 5, 2, 0); break;
                case BIO_MOUNTAINS:
                    if (get_block(wx, h, wz) == B_GRASS && r < 0.010f)
                        place_tree_type(wx, wz, h, B_LOG, B_SPRUCE_LEAVES, 5, 3, 1);
                    break;
                case BIO_DESERT:      if (r < 0.010f) place_cactus(wx, wz, h); break;
                default: break;   /* ocean/beach/badlands/mushroom: no trees */
            }
        }
}

/* Ensure every chunk within LOADR of the player is resident. */
static void stream_chunks(void) {
    int pcx = fdiv((int)floorf(g_px), CH), pcz = fdiv((int)floorf(g_pz), CH);
    for (int dz = -LOADR; dz <= LOADR; dz++)
        for (int dx = -LOADR; dx <= LOADR; dx++) {
            int cx = pcx + dx, cz = pcz + dz;
            Chunk *c = chunk_cell(cx, cz);
            if (!c->valid || c->cx != cx || c->cz != cz) gen_chunk(c, cx, cz);
        }
}

static void gen_world(unsigned seed) {
    g_seed = seed;
    for (int i = 0; i < GS * GS; i++) g_chunks[i].valid = 0;
    g_px = 8.5f; g_pz = 8.5f;
    stream_chunks();                       /* generate around spawn */
    int cy = WORLD_Y - 1;
    while (cy > 0 && get_block(8, cy, 8) == B_AIR) cy--;
    g_py = cy + 2.6f;
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

    /* ---- biome blocks (uniform across faces) ---- */
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_SNOW][f], 236, 240, 246, 8, 11);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_ICE][f], 150, 190, 230, 10, 12);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_RED_SAND][f], 200, 110, 60, 16, 13);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_TERRACOTTA][f], 156, 92, 58, 14, 14);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_DRY_GRASS][f], 150, 160, 72, 20, 15);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_PODZOL][f], 92, 66, 40, 16, 16);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_SPRUCE_LEAVES][f], 40, 72, 46, 22, 17);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_JUNGLE_LEAVES][f], 46, 122, 40, 24, 18);
    for (int f = 0; f < 3; f++) fill_tex(g_tex[B_CACTUS][f], 60, 120, 52, 14, 19);

    /* mycelium: grey soil with purple flecks */
    for (int f = 0; f < 3; f++) {
        fill_tex(g_tex[B_MYCELIUM][f], 122, 110, 124, 14, 20);
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                if (rnd2(x * 5 + f, y * 7) < 0.10f) g_tex[B_MYCELIUM][f][y * TEX + x] = rgb(120, 80, 130);
    }
    /* birch log: pale bark with dark dashes; rings on the ends */
    fill_tex(g_tex[B_BIRCH_LOG][1], 220, 222, 210, 10, 21);
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++)
            if (((y % 5) == 0) && rnd2(x, y) < 0.5f) g_tex[B_BIRCH_LOG][1][y * TEX + x] = rgb(40, 40, 40);
    for (int f = 0; f < 3; f += 2)
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float dx = x - 7.5f, dy = y - 7.5f; int ring = ((int)sqrtf(dx * dx + dy * dy) % 2) ? 18 : 0;
                g_tex[B_BIRCH_LOG][f][y * TEX + x] = rgb(210 - ring, 212 - ring, 198 - ring);
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

/* RLE a byte buffer to file */
static void rle_write(FILE *f, const uint8_t *buf, unsigned total) {
    unsigned i = 0;
    while (i < total) {
        uint8_t v = buf[i];
        unsigned run = 1;
        while (i + run < total && buf[i + run] == v && run < 0xffffffu) run++;
        fputc(v, f);
        fputc(run & 0xff, f); fputc((run >> 8) & 0xff, f); fputc((run >> 16) & 0xff, f);
        i += run;
    }
}
static int rle_read(FILE *f, uint8_t *buf, unsigned total) {
    unsigned i = 0;
    while (i < total) {
        int v = fgetc(f), b0 = fgetc(f), b1 = fgetc(f), b2 = fgetc(f);
        if (v < 0 || b2 < 0) break;
        unsigned run = (unsigned)b0 | ((unsigned)b1 << 8) | ((unsigned)b2 << 16);
        while (run-- && i < total) buf[i++] = (uint8_t)v;
    }
    return i == total;
}

/* Save the seed, player state, and all currently-loaded chunks. Chunks
   outside the saved set simply regenerate deterministically from the seed. */
static int save_world(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("MCW2", 1, 4, f);
    fwrite(&g_seed, sizeof g_seed, 1, f);
    float ps[5] = {g_px, g_py, g_pz, g_yaw, g_pitch};
    fwrite(ps, sizeof(float), 5, f);
    int n = 0;
    for (int i = 0; i < GS * GS; i++) if (g_chunks[i].valid) n++;
    fwrite(&n, sizeof n, 1, f);
    for (int i = 0; i < GS * GS; i++) {
        Chunk *c = &g_chunks[i];
        if (!c->valid) continue;
        fwrite(&c->cx, sizeof c->cx, 1, f);
        fwrite(&c->cz, sizeof c->cz, 1, f);
        rle_write(f, c->b, (unsigned)sizeof c->b);
    }
    fclose(f);
    return 1;
}

static int load_world(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4]; float ps[5]; unsigned seed; int n;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MCW2", 4) != 0) { fclose(f); return 0; }
    if (fread(&seed, sizeof seed, 1, f) != 1) { fclose(f); return 0; }
    if (fread(ps, sizeof(float), 5, f) != 5) { fclose(f); return 0; }
    if (fread(&n, sizeof n, 1, f) != 1) { fclose(f); return 0; }
    g_seed = seed;
    for (int i = 0; i < GS * GS; i++) g_chunks[i].valid = 0;
    for (int k = 0; k < n; k++) {
        int cx, cz;
        if (fread(&cx, sizeof cx, 1, f) != 1 || fread(&cz, sizeof cz, 1, f) != 1) { fclose(f); return 0; }
        Chunk *c = chunk_cell(cx, cz);
        c->cx = cx; c->cz = cz; c->valid = 1;
        if (!rle_read(f, c->b, (unsigned)sizeof c->b)) { fclose(f); return 0; }
    }
    fclose(f);
    g_px = ps[0]; g_py = ps[1]; g_pz = ps[2]; g_yaw = ps[3]; g_pitch = ps[4];
    g_vx = g_vy = g_vz = 0; g_onground = 0;
    stream_chunks();                 /* fill in any gaps around the player */
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
    float loaded = (float)((LOADR - 1) * CH);            /* never ray into unloaded chunks */
    if (maxray > loaded) maxray = loaded;
    float dl = g_daylight; if (dl < 0.12f) dl = 0.12f;   /* keep a little moonlight */

    /* sky colour shifts warm at dawn/dusk */
    float warm = 0.0f;
    { float d = g_tod - 0.25f; if (d < -0.5f) d += 1; if (d > 0.5f) d -= 1;  /* dist to sunrise */
      float d2 = g_tod - 0.75f; if (d2 < -0.5f) d2 += 1; if (d2 > 0.5f) d2 -= 1; /* to sunset */
      float m = fabsf(d) < fabsf(d2) ? fabsf(d) : fabsf(d2);
      warm = 1.0f - m / 0.10f; if (warm < 0) warm = 0; }
    int sr_t = (int)((120 + warm * 90) * dl), sg_t = (int)((170 + warm * 20) * dl), sb_t = (int)((235 - warm * 120) * dl);
    int sr_b = (int)((200 + warm * 40) * dl), sg_b = (int)((225 - warm * 40) * dl), sb_b = (int)((250 - warm * 120) * dl);
    const uint32_t sky_top = rgb(sr_t, sg_t, sb_t);
    const uint32_t sky_bot = rgb(sr_b, sg_b, sb_b);

    /* sun / moon direction (moves through the sky with the time of day) */
    float phase = (g_tod - 0.25f) * 2.0f * (float)M_PI;
    float su_x = cosf(phase), su_y = sinf(phase), su_z = 0.35f;
    float sl = 1.0f / sqrtf(su_x * su_x + su_y * su_y + su_z * su_z);
    su_x *= sl; su_y *= sl; su_z *= sl;
    int sun_up = (su_y > -0.05f);       /* sun above horizon => daytime disc */

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
                /* sun or moon disc + soft halo */
                float dot = dx * su_x + dy * su_y + dz * su_z;
                if (sun_up) {
                    if (dot > 0.9995f)      col = rgb(255, 245, 200);           /* sun */
                    else if (dot > 0.995f)  col = rgb((int)(255 * 0.6f + ((col >> 16) & 0xff) * 0.4f),
                                                      (int)(230 * 0.6f + ((col >> 8) & 0xff) * 0.4f),
                                                      (int)(160 * 0.6f + (col & 0xff) * 0.4f));
                } else {
                    float md = -dot;   /* moon is opposite the sun */
                    if (md > 0.9995f)      col = rgb(230, 230, 245);            /* moon */
                    else if (md > 0.997f)  col = rgb(180, 180, 200);
                }
            }
            g_framebuf[y * RENDER_W + x] = col;
        }
    }

    if (!g_draw_hud) return;

    /* crosshair (HUD hotbar/hearts are drawn separately in draw_hud) */
    int cxp = RENDER_W / 2, cyp = RENDER_H / 2;
    for (int i = -5; i <= 5; i++) {
        g_framebuf[cyp * RENDER_W + (cxp + i)] ^= 0x00ffffff;
        g_framebuf[(cyp + i) * RENDER_W + cxp] ^= 0x00ffffff;
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

/* draw a block's icon (side texture) scaled into an sw x sw box at (x,y) */
static void fb_block_icon(int x, int y, int block, int sw) {
    for (int yy = 0; yy < sw; yy++)
        for (int xx = 0; xx < sw; xx++)
            fb_px(x + xx, y + yy, g_tex[block][1][(yy * TEX / sw) * TEX + (xx * TEX / sw)]);
}

/* a small 7x7 heart */
static void fb_heart(int x, int y, uint32_t col) {
    static const unsigned char H[7] = {0x36, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
    for (int row = 0; row < 7; row++)
        for (int c = 0; c < 7; c++)
            if (H[row] & (1 << (6 - c))) fb_px(x + c, y + row, col);
}

/* the in-game HUD: hotbar (inventory slots 0..8) + survival hearts */
static void draw_hud(void) {
    const int slots = INV_COLS, sw = 22, gap = 2;
    int tot = slots * (sw + gap) - gap;
    int x0 = (RENDER_W - tot) / 2, y0 = RENDER_H - sw - 6;

    if (g_gamemode == 0) {               /* survival: 10 hearts above the hotbar */
        for (int i = 0; i < 10; i++) {
            int hx = x0 + i * 9, hy = y0 - 12;
            int hp = g_health - i * 2;
            fb_heart(hx, hy, 0xff400000u);                 /* empty background */
            if (hp >= 2) fb_heart(hx, hy, 0xffff3020u);    /* full  */
            else if (hp == 1) fb_heart(hx, hy, 0xffff8060u);/* half  */
        }
    }

    for (int k = 0; k < slots; k++) {
        int sxp = x0 + k * (sw + gap);
        int sel = (k == g_hotbar_sel);
        for (int yy = -2; yy < sw + 2; yy++)
            for (int xx = -2; xx < sw + 2; xx++) {
                int px = sxp + xx, py = y0 + yy;
                if (xx < 0 || yy < 0 || xx >= sw || yy >= sw)
                    fb_px(px, py, sel ? 0xffffffffu : 0xff2a2a2au);
            }
        Slot *s = &g_inv[k];
        if (s->count != 0 && s->block) {
            fb_block_icon(sxp, y0, s->block, sw);
            if (s->count > 0) {
                char n[8]; snprintf(n, sizeof n, "%d", s->count);
                fb_text(sxp + sw - fb_text_w(n, 1) - 1, y0 + sw - 8, n, 0xffffffffu, 1);
            }
        }
    }
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
        else if (!strcmp(a, "day"))   { g_tod = 0.5f;  con_log("time set day"); }
        else if (!strcmp(a, "night")) { g_tod = 0.0f;  con_log("time set night"); }
        else { g_tod = (float)atof(a); g_tod -= floorf(g_tod); con_log("time %.2f", g_tod); }
        g_daylight = daylight_from_tod(g_tod);
    } else if (!strcmp(cmd, "tp")) {
        char *ax = strtok(NULL, " "), *ay = strtok(NULL, " "), *az = strtok(NULL, " ");
        if (ax && ay && az) { g_px = (float)atof(ax); g_py = (float)atof(ay); g_pz = (float)atof(az);
            g_vx = g_vy = g_vz = 0; con_log("teleported"); }
        else con_log("usage: tp x y z");
    } else if (!strcmp(cmd, "give")) {
        char *a = strtok(NULL, " "), *cnt = strtok(NULL, " ");
        int b = a ? block_from_name(a) : -1;
        int n = cnt ? atoi(cnt) : 1; if (n < 1) n = 1;
        if (b > 0) { inv_give(b, n); con_log("gave %d x block %d", n, b); }
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
        if (a && (!strcmp(a, "creative") || !strcmp(a, "1"))) { set_gamemode(1); con_log("creative"); }
        else if (a && (!strcmp(a, "survival") || !strcmp(a, "0"))) { set_gamemode(0); con_log("survival"); }
        else con_log("usage: gamemode creative|survival");
    } else if (!strcmp(cmd, "fly")) {
        if (g_gamemode == 1) { g_fly = !g_fly; con_log("fly %s", g_fly ? "on" : "off"); }
        else con_log("fly is creative-only");
    } else if (!strcmp(cmd, "heal")) {
        g_health = 20; con_log("healed");
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
    fb_text_center(RENDER_W / 2, 46, "CREATE NEW WORLD", 0xffffffffu, 2);
    fb_text_center(RENDER_W / 2, 84, "SEED (BLANK = RANDOM):", 0xffb0b0b0u, 1);
    int fx = (RENDER_W - 220) / 2, fy = 100;
    fb_fill(fx, fy, 220, 20, 0xff202020u);
    fb_fill(fx, fy, 220, 1, 0xff808080u);
    char shown[130];
    snprintf(shown, sizeof shown, "%s_", g_input);
    fb_text(fx + 6, fy + 6, shown, 0xffffffffu, 1);
    char mode[48];
    snprintf(mode, sizeof mode, "< MODE: %s >", g_gamemode ? "CREATIVE" : "SURVIVAL");
    fb_text_center(RENDER_W / 2, 130, mode, 0xffffd040u, 1);
    draw_button(0, "CREATE WORLD");
    draw_button(1, "BACK");
    fb_text_center(RENDER_W / 2, RENDER_H - 12, "SEED + ENTER   -   LEFT/RIGHT: MODE", 0xff909090u, 1);
}

static const char *g_settings_names[] = {"FOV", "RENDER DISTANCE", "MOUSE SENSITIVITY", "MOVE SPEED", "DAY LENGTH (S)"};
static float *settings_ptr(int i) {
    switch (i) { case 0: return &g_fov; case 1: return &g_render_dist;
                 case 2: return &g_sensitivity; case 3: return &g_movespeed; default: return &g_daylen; }
}
static void settings_adjust(int i, int dir) {
    float *p = settings_ptr(i);
    float step[] = {5, 8, 0.05f, 0.1f, 30};
    float lo[]   = {30, 24, 0.05f, 0.3f, 30};
    float hi[]   = {110, (LOADR - 1) * CH, 1.0f, 4.0f, 1200};
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
        if (i == 0 || i == 1 || i == 4) snprintf(val, sizeof val, "%.0f", v);
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
/* Inventory screen (Minecraft-style slot grid, click to move stacks)       */
/* ----------------------------------------------------------------------- */

#define SS 20                            /* inventory slot size */
static int inv_slot_x(int col) {
    int gw = INV_COLS * (SS + 2) - 2, gx = (RENDER_W - gw) / 2;
    return gx + col * (SS + 2);
}
static int inv_slot_y(int row) {         /* rows 0..2 = storage, row 3 = hotbar */
    int top = 78;
    return (row < 3) ? top + row * (SS + 2) : top + 3 * (SS + 2) + 8;
}
static int slot_index(int row, int col) { return (row < 3) ? 9 + row * INV_COLS + col : col; }

static int inv_slot_at(int ix, int iy) {
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < INV_COLS; col++) {
            int x = inv_slot_x(col), y = inv_slot_y(row);
            if (ix >= x && ix < x + SS && iy >= y && iy < y + SS) return slot_index(row, col);
        }
    return -1;
}

/* pick up / place / swap / split a stack, Minecraft-style */
static void inv_click(int idx, int right) {
    if (idx < 0) return;
    Slot *s = &g_inv[idx];
    if (!right) {
        if (g_hand.count == 0) {
            if (s->count < 0) { g_hand.block = s->block; g_hand.count = STACK_MAX; }   /* grab from infinite */
            else if (s->count > 0) { g_hand = *s; s->block = 0; s->count = 0; }
        } else {
            if (s->count == 0) { *s = g_hand; g_hand.block = 0; g_hand.count = 0; }
            else if (s->count < 0) { g_hand.block = 0; g_hand.count = 0; }              /* drop into infinite */
            else if (s->block == g_hand.block) {
                int space = STACK_MAX - s->count, mv = g_hand.count < space ? g_hand.count : space;
                s->count += mv; g_hand.count -= mv; if (g_hand.count == 0) g_hand.block = 0;
            } else { Slot t = *s; *s = g_hand; g_hand = t; }
        }
    } else {
        if (g_hand.count == 0) {
            if (s->count > 0) { int half = (s->count + 1) / 2; g_hand.block = s->block; g_hand.count = half;
                s->count -= half; if (s->count == 0) s->block = 0; }
            else if (s->count < 0) { g_hand.block = s->block; g_hand.count = 1; }
        } else {
            if (s->count == 0) { s->block = g_hand.block; s->count = 1; if (--g_hand.count == 0) g_hand.block = 0; }
            else if (s->count > 0 && s->block == g_hand.block && s->count < STACK_MAX) {
                s->count++; if (--g_hand.count == 0) g_hand.block = 0; }
        }
    }
}

static void render_inventory(void) {
    fb_dim(0, 0, RENDER_W, RENDER_H, 150);
    int gw = INV_COLS * (SS + 2) - 2, gx = (RENDER_W - gw) / 2;
    fb_fill(gx - 8, 62, gw + 16, inv_slot_y(3) + SS - 62 + 8, 0xff303030u);
    fb_text_center(RENDER_W / 2, 66, g_gamemode ? "CREATIVE INVENTORY" : "INVENTORY", 0xffffffffu, 2);
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < INV_COLS; col++) {
            int idx = slot_index(row, col), x = inv_slot_x(col), y = inv_slot_y(row);
            int hot = (row == 3 && col == g_hotbar_sel);
            fb_fill(x - 1, y - 1, SS + 2, SS + 2, hot ? 0xffffffffu : 0xff181818u);
            fb_fill(x, y, SS, SS, 0xff585858u);
            Slot *s = &g_inv[idx];
            if (s->count != 0 && s->block) {
                fb_block_icon(x, y, s->block, SS);
                if (s->count > 0) {
                    char n[8]; snprintf(n, sizeof n, "%d", s->count);
                    fb_text(x + SS - fb_text_w(n, 1) - 1, y + SS - 8, n, 0xffffffffu, 1);
                }
            }
        }
    if (g_hand.count != 0 && g_hand.block) {
        fb_block_icon(g_mouse_ix - SS / 2, g_mouse_iy - SS / 2, g_hand.block, SS);
        if (g_hand.count > 0) {
            char n[8]; snprintf(n, sizeof n, "%d", g_hand.count);
            fb_text(g_mouse_ix + 2, g_mouse_iy + 2, n, 0xffffffffu, 1);
        }
    }
    fb_text_center(RENDER_W / 2, RENDER_H - 12, "E TO CLOSE   -   CLICK TO MOVE STACKS", 0xff909090u, 1);
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

/* add n of a block into the inventory (merge stacks, then fill empty slots) */
static void inv_give(int block, int n) {
    if (block <= 0 || n <= 0) return;
    for (int i = 0; i < INV_SLOTS && n > 0; i++)
        if (g_inv[i].block == block && g_inv[i].count >= 0 && g_inv[i].count < STACK_MAX) {
            int add = STACK_MAX - g_inv[i].count; if (add > n) add = n;
            g_inv[i].count += add; n -= add;
        }
    for (int i = 0; i < INV_SLOTS && n > 0; i++)
        if (g_inv[i].count == 0) {
            g_inv[i].block = (uint8_t)block;
            int add = n > STACK_MAX ? STACK_MAX : n;
            g_inv[i].count = add; n -= add;
        }
}

/* consume one of the selected hotbar block (no-op in creative) */
static void consume_selected(void) {
    if (g_gamemode == 1) return;
    Slot *s = &g_inv[g_hotbar_sel];
    if (s->count > 0 && --s->count == 0) s->block = 0;
}

static void init_inventory(int mode) {
    for (int i = 0; i < INV_SLOTS; i++) { g_inv[i].block = 0; g_inv[i].count = 0; }
    g_hand.block = 0; g_hand.count = 0;
    g_health = 20;
    if (mode == 1) {                         /* creative: infinite blocks */
        int blk[] = {B_GRASS, B_DIRT, B_STONE, B_COBBLE, B_LOG,
                     B_LEAVES, B_SAND, B_PLANKS, B_GLASS, B_WATER};
        for (int i = 0; i < 10; i++) { g_inv[i].block = (uint8_t)blk[i]; g_inv[i].count = -1; }
    }
}

static void set_gamemode(int m) {
    g_gamemode = m;
    if (m == 0) g_fly = 0;
    init_inventory(m);
}

static void respawn(void) {
    g_px = 8.5f; g_pz = 8.5f; stream_chunks();
    int cy = WORLD_Y - 1;
    while (cy > 0 && get_block(8, cy, 8) == B_AIR) cy--;
    g_py = cy + 2.6f;
    g_vx = g_vy = g_vz = 0; g_health = 20; g_air_max_y = g_py; g_was_ground = 1;
}

static void update_player(float dt) {
    stream_chunks();                 /* keep terrain loaded around the player */
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

    int flying = g_fly && g_gamemode == 1;   /* fly is a creative-only ability */
    float speed = (flying ? 9.0f : 4.5f) * g_movespeed;

    if (flying) {
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

    /* fall damage (survival only) */
    if (!g_onground) {
        if (g_py > g_air_max_y) g_air_max_y = g_py;
    } else {
        if (!g_was_ground && g_gamemode == 0 && !flying) {
            float fall = g_air_max_y - g_py;
            if (fall > 3.0f) {
                g_health -= (int)(fall - 3.0f);
                if (g_health <= 0) { respawn(); }
            }
        }
        g_air_max_y = g_py;
    }
    g_was_ground = g_onground;

    if (g_py < -40) { respawn(); }  /* fell out of the world */
}

static void do_break(void) {
    V3 f, r, u; camera_basis(&f, &r, &u);
    int hx, hy, hz, nx, ny, nz, blk; float d;
    if (raycast(g_px, g_py, g_pz, f.x, f.y, f.z, REACH,
                &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk)) {
        int broken = get_block(hx, hy, hz);
        set_block(hx, hy, hz, B_AIR);
        if (g_gamemode == 0 && broken != B_AIR && broken != B_WATER)
            inv_give(broken, 1);          /* survival: collect the block */
    }
}

static void do_place(void) {
    int blk_sel = selected_block();
    if (blk_sel <= 0) return;                     /* nothing selected/available */
    if (g_gamemode == 0 && g_inv[g_hotbar_sel].count <= 0) return;
    V3 f, r, u; camera_basis(&f, &r, &u);
    int hx, hy, hz, nx, ny, nz, blk; float d;
    if (raycast(g_px, g_py, g_pz, f.x, f.y, f.z, REACH,
                &hx, &hy, &hz, &nx, &ny, &nz, &d, &blk)) {
        int bx = hx + nx, by = hy + ny, bz = hz + nz;
        /* don't place inside the player */
        int px0 = (int)floorf(g_px - PLR_RAD), px1 = (int)floorf(g_px + PLR_RAD);
        int py0 = (int)floorf(g_py - PLR_FEET), py1 = (int)floorf(g_py + PLR_HEAD);
        int pz0 = (int)floorf(g_pz - PLR_RAD), pz1 = (int)floorf(g_pz + PLR_RAD);
        int inside = (bx >= px0 && bx <= px1 && by >= py0 && by <= py1 && bz >= pz0 && bz <= pz1);
        if (!inside && get_block(bx, by, bz) == B_AIR) {
            set_block(bx, by, bz, (uint8_t)blk_sel);
            consume_selected();
        }
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

static volatile int g_lclick = 0, g_rclick = 0;   /* UI clicks (edge)          */
static volatile int g_toggle_inv = 0;             /* E pressed                 */

static void start_world(unsigned seed) {
    g_seed = seed;
    gen_world(seed);
    init_inventory(g_gamemode);
    g_state = ST_PLAY;
    g_paused = 0;
    g_console_open = 0;
    g_inv_open = 0;
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

        if (w == 'E' && g_state == ST_PLAY && !g_paused && !g_console_open) g_toggle_inv = 1;

        /* gameplay hotkeys only while actively playing (not in a UI overlay) */
        if (g_state == ST_PLAY && !g_paused && !g_console_open && !g_inv_open) {
            if (w == 'F') g_fly = !g_fly;
            if (w == 'G') set_gamemode(g_gamemode ^ 1);
            if (w == 'R') { g_seed = (unsigned)GetTickCount(); gen_world(g_seed); init_inventory(g_gamemode); }
            if (w == 'K') save_world("world.sav");
            if (w == 'L') load_world("world.sav");
            if (w >= '1' && w <= '9') g_hotbar_sel = (int)w - '1';   /* select hotbar slot */
            if (w == 'T' || w == VK_OEM_2) {   /* T or '/' opens console */
                g_console_open = 1; g_input_len = 0; g_input[0] = 0;
            }
        }
        return 0;

    case WM_KEYUP:
        if (w < 256) g_keys[w] = 0;
        return 0;

    case WM_MOUSEMOVE:
        if (g_inv_open) {
            g_mouse_ix = g_client_w ? (short)LOWORD(l) * RENDER_W / g_client_w : 0;
            g_mouse_iy = g_client_h ? (short)HIWORD(l) * RENDER_H / g_client_h : 0;
        }
        return 0;

    case WM_MOUSEWHEEL: {                    /* scroll changes hotbar slot */
        if (g_state == ST_PLAY && !g_paused && !g_console_open && !g_inv_open) {
            int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1;
            g_hotbar_sel = (g_hotbar_sel + d + INV_COLS) % INV_COLS;
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (g_inv_open) { g_click_x = LOWORD(l); g_click_y = HIWORD(l); g_lclick = 1; }
        else if (g_state == ST_PLAY && !g_paused && !g_console_open) g_break_req = 1;
        else { g_click_x = LOWORD(l); g_click_y = HIWORD(l); }
        return 0;
    case WM_RBUTTONDOWN:
        if (g_inv_open) { g_click_x = LOWORD(l); g_click_y = HIWORD(l); g_rclick = 1; }
        else if (g_state == ST_PLAY && !g_paused && !g_console_open) g_place_req = 1;
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

        int playing = (g_state == ST_PLAY && !g_paused && !g_console_open && !g_inv_open);

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
            if (g_navlr) { g_gamemode ^= 1; g_navlr = 0; }   /* toggle survival/creative */
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
            if (g_toggle_inv) { g_toggle_inv = 0; g_inv_open = !g_inv_open; }
            if (g_inv_open) {
                if (g_esc) { g_esc = 0; g_inv_open = 0; }
                if (g_lclick || g_rclick) {
                    int ix, iy; click_to_internal(&ix, &iy);
                    int slot = inv_slot_at(ix, iy);
                    inv_click(slot, g_rclick ? 1 : 0);
                    g_lclick = g_rclick = 0; g_click_x = -1;
                }
            } else if (g_console_open) {
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
                g_tod += dt / g_daylen;             /* advance day/night cycle */
                if (g_tod >= 1.0f) g_tod -= 1.0f;
                g_daylight = daylight_from_tod(g_tod);
            }

            render_frame();
            if (!g_inv_open) draw_hud();
            if (g_inv_open) render_inventory();
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
