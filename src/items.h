/* MiniCraft item registry.
 *
 * Holds the whole catalogue (data/items.txt -> item_data.h) and turns every
 * entry into something the engine can actually draw:
 *
 *   - a 16x16 texture per face, synthesised procedurally from the item's
 *     pattern and base colours -- no art files needed, and no two items look
 *     alike unless they genuinely share a material;
 *   - identical (pattern, colour, role) triples share one texture layer, so
 *     1712 items cost a few hundred kilobytes rather than five megabytes;
 *   - a texture *animation* applied at sample time (flowing liquids, guttering
 *     flame, swirling potions, enchantment glint), which costs no memory at
 *     all because it is a function of (texel, phase).
 *
 * A real Minecraft-format resource pack still wins over the synthesised
 * texture when one is present: load_assets() looks for
 * assets/minecraft/textures/{block,item}/<slugified name>.png.
 */
#ifndef ITEMS_H
#define ITEMS_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef TEX
#define TEX 16
#endif

/* ----------------------------------------------------------------------- */
/* Enumerations -- these mirror the lists at the top of tools/gen_items.py.  */
/* ----------------------------------------------------------------------- */

enum { CAT_ENGINE, CAT_ITEM, CAT_POTION, CAT_ENCHANT, CAT_TEMPLATE,
       CAT_POTTERY, CAT_COUNT };

enum {
    /* block models */
    MODEL_CUBE, MODEL_COLUMN, MODEL_SLAB, MODEL_STAIRS, MODEL_WALL,
    MODEL_FENCE, MODEL_GATE, MODEL_DOOR, MODEL_TRAPDOOR, MODEL_BUTTON,
    MODEL_PLATE, MODEL_CARPET, MODEL_PANE, MODEL_LADDER, MODEL_TORCH,
    MODEL_LANTERN, MODEL_CHAIN, MODEL_CROSS, MODEL_BUSH, MODEL_POT,
    MODEL_CHEST, MODEL_BED, MODEL_SIGN, MODEL_BANNER, MODEL_SHELF,
    MODEL_ANVIL, MODEL_HEAD, MODEL_CAMPFIRE, MODEL_RAIL, MODEL_CANDLE,
    MODEL_SMALLBOX, MODEL_FRAME,
    /* item models */
    MODEL_FLAT, MODEL_TOOL, MODEL_ROD, MODEL_ARMOR, MODEL_BOTTLE,
    MODEL_BOOK, MODEL_SHEET, MODEL_SHERD, MODEL_DISC, MODEL_BOAT,
    MODEL_MINECART, MODEL_FOOD, MODEL_BOW, MODEL_INGOT, MODEL_GEM,
    MODEL_DUST,
    MODEL_COUNT
};
#define MODEL_FIRST_ITEM MODEL_FLAT

enum {
    PAT_NOISE, PAT_GRAIN, PAT_BARK, PAT_RINGS, PAT_SPECKLE, PAT_BRICKS,
    PAT_TILES, PAT_SMOOTH, PAT_CHISELED, PAT_GLASS, PAT_ORE, PAT_FOLIAGE,
    PAT_PLANT, PAT_FLOWER, PAT_CLOTH, PAT_METAL, PAT_GEMSTONE, PAT_CRYSTAL,
    PAT_LIQUID, PAT_POWDER, PAT_SAND, PAT_CORAL, PAT_SCULK, PAT_GLOW,
    PAT_WEB, PAT_SHERD, PAT_PAPER, PAT_BOOK, PAT_BOTTLE, PAT_SWORD,
    PAT_PICKAXE, PAT_AXE, PAT_SHOVEL, PAT_HOE, PAT_SPEAR, PAT_BOW,
    PAT_ROD, PAT_ARROW, PAT_HELMET, PAT_CHESTPLATE, PAT_LEGGINGS,
    PAT_BOOTS, PAT_INGOT, PAT_NUGGET, PAT_ROUND, PAT_MEAT, PAT_SEED,
    PAT_DISC, PAT_EGG, PAT_BUCKET, PAT_TRIM, PAT_ENCHANT, PAT_SKULL,
    PAT_POT, PAT_CART, PAT_HULL, PAT_HONEY, PAT_SLIME, PAT_MUSHROOM,
    PAT_COUNT
};

enum {
    ANIM_NONE, ANIM_BOB, ANIM_SWAY, ANIM_SPIN, ANIM_SWING, ANIM_STAB,
    ANIM_DRINK, ANIM_EAT, ANIM_SHOOT, ANIM_PLACE, ANIM_OPEN, ANIM_FLICKER,
    ANIM_PULSE, ANIM_COUNT
};

enum { TA_NONE, TA_FLOW, TA_FIRE, TA_SWIRL, TA_GLINT, TA_PULSE, TA_SPARKLE,
       TA_COUNT };

/* item flags */
#define IF_PLACEABLE 0x01
#define IF_SOLID     0x02
#define IF_FULLCUBE  0x04
#define IF_CUTOUT    0x08
#define IF_EMISSIVE  0x10
#define IF_LIQUID    0x20
#define IF_HIDDEN    0x40
#define IF_TINTED    0x80

typedef struct {
    uint8_t  category;
    uint8_t  model;
    uint8_t  pattern;
    uint8_t  flags;
    uint8_t  anim_idle;
    uint8_t  anim_use;
    uint8_t  tex_anim;
    uint8_t  light;
    uint32_t col_top, col_side, col_bottom, col_accent;
} ItemDef;

#include "item_data.h"

/* ----------------------------------------------------------------------- */
/* Texture layer pool                                                       */
/* ----------------------------------------------------------------------- */

/* Sized from measurement, not guesswork: the catalogue currently resolves to
   well under a thousand distinct layers because whole families of items share
   a material. build_item_textures() never overflows this; the headless test
   asserts the margin. */
#define MAX_LAYERS 2048
#define LAYER_HASH 4096

static uint32_t g_layers[MAX_LAYERS][TEX * TEX];
static int      g_layer_count = 0;
static uint16_t g_item_layer[ITEM_COUNT][3];   /* [item][top|side|bottom]   */

static uint64_t g_layer_key[LAYER_HASH];
static uint16_t g_layer_slot[LAYER_HASH];

/* ----------------------------------------------------------------------- */
/* Small deterministic noise, independent of the world generator's           */
/* ----------------------------------------------------------------------- */

static inline uint32_t ihash(uint32_t a) {
    a ^= a >> 16; a *= 0x7feb352du;
    a ^= a >> 15; a *= 0x846ca68bu;
    a ^= a >> 16;
    return a;
}
static inline uint32_t ihash3(int x, int y, uint32_t salt) {
    return ihash((uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ salt);
}
/* 0..1 */
static inline float inoise(int x, int y, uint32_t salt) {
    return (ihash3(x, y, salt) & 0xffffff) / (float)0x1000000;
}

/* ----------------------------------------------------------------------- */
/* Colour helpers                                                           */
/* ----------------------------------------------------------------------- */

#define A_OPAQUE 0xff000000u

static inline uint32_t col_make(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return A_OPAQUE | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static inline uint32_t col_shade(uint32_t c, float f) {
    return col_make((int)(((c >> 16) & 0xff) * f),
                    (int)(((c >> 8) & 0xff) * f),
                    (int)((c & 0xff) * f));
}
static inline uint32_t col_mix(uint32_t a, uint32_t b, float t) {
    int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
    int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
    return col_make((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t),
                    (int)(ab + (bb - ab) * t));
}
/* base colour plus symmetric per-texel noise */
static inline uint32_t col_noise(uint32_t c, int amp, int x, int y, uint32_t salt) {
    int n = (int)((inoise(x, y, salt) - 0.5f) * 2.0f * amp);
    return col_make(((c >> 16) & 0xff) + n, ((c >> 8) & 0xff) + n,
                    (c & 0xff) + n);
}

/* ----------------------------------------------------------------------- */
/* Drawing primitives on a 16x16 layer                                      */
/* ----------------------------------------------------------------------- */

static inline void tx_clear(uint32_t *t, uint32_t c) {
    for (int i = 0; i < TEX * TEX; i++) t[i] = c;
}
static inline void tx_px(uint32_t *t, int x, int y, uint32_t c) {
    if (x >= 0 && x < TEX && y >= 0 && y < TEX) t[y * TEX + x] = c;
}
static inline uint32_t tx_get(const uint32_t *t, int x, int y) {
    if (x < 0 || x >= TEX || y < 0 || y >= TEX) return 0;
    return t[y * TEX + x];
}
static void tx_rect(uint32_t *t, int x0, int y0, int w, int h, uint32_t c) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) tx_px(t, x, y, c);
}
static void tx_line(uint32_t *t, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        tx_px(t, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void tx_disc(uint32_t *t, float cx, float cy, float r, uint32_t c) {
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) t[y * TEX + x] = c;
        }
}
/* Darken the bottom-right of every drawn texel to fake a bevel, the way
   Minecraft's item sprites are shaded. */
static void tx_bevel(uint32_t *t) {
    uint32_t src[TEX * TEX];
    memcpy(src, t, sizeof src);
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            uint32_t c = src[y * TEX + x];
            if (!(c >> 24)) continue;
            int lit = (tx_get(src, x - 1, y) >> 24) == 0 ||
                      (tx_get(src, x, y - 1) >> 24) == 0;
            int dark = (tx_get(src, x + 1, y) >> 24) == 0 ||
                       (tx_get(src, x, y + 1) >> 24) == 0;
            if (lit)       t[y * TEX + x] = col_shade(c, 1.18f);
            else if (dark) t[y * TEX + x] = col_shade(c, 0.72f);
        }
}

/* ----------------------------------------------------------------------- */
/* Texture synthesis                                                        */
/*                                                                          */
/* role: 0 = top, 1 = side, 2 = bottom. Block patterns fill the layer;       */
/* item patterns draw a sprite on a transparent background.                  */
/* ----------------------------------------------------------------------- */

static void synth_block(uint32_t *t, int pattern, uint32_t base, uint32_t acc,
                        int role, uint32_t salt) {
    switch (pattern) {
    case PAT_GRAIN:                     /* planks: horizontal seams */
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                t[y * TEX + x] = ((y & 3) == 0)
                    ? col_shade(col_noise(base, 8, x, y, salt), 0.72f)
                    : col_noise(base, 12, x, y, salt + (y >> 2));
        break;
    case PAT_BARK:
        if (role != 1) {                /* log ends: growth rings */
            for (int y = 0; y < TEX; y++)
                for (int x = 0; x < TEX; x++) {
                    float dx = x - 7.5f, dy = y - 7.5f;
                    float d = sqrtf(dx * dx + dy * dy);
                    float f = ((int)d & 1) ? 0.86f : 1.0f;
                    t[y * TEX + x] = col_shade(col_noise(acc, 8, x, y, salt), f);
                }
        } else {
            for (int y = 0; y < TEX; y++)
                for (int x = 0; x < TEX; x++)
                    t[y * TEX + x] = ((x & 3) == 0)
                        ? col_shade(base, 0.7f)
                        : col_noise(base, 14, x, y, salt);
        }
        break;
    case PAT_RINGS:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float dx = x - 7.5f, dy = y - 7.5f;
                float d = sqrtf(dx * dx + dy * dy);
                t[y * TEX + x] = col_shade(base, ((int)d & 1) ? 0.85f : 1.0f);
            }
        break;
    case PAT_SPECKLE:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                uint32_t c = col_noise(base, 18, x, y, salt);
                if (((x + y) % 5) == 0 || inoise(x * 3, y * 3, salt) < 0.10f)
                    c = col_shade(base, 0.62f);
                t[y * TEX + x] = c;
            }
        break;
    case PAT_BRICKS:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int row = y >> 2;
                int xoff = (row & 1) ? 2 : 0;
                int mortar = ((y & 3) == 0) || (((x + xoff) & 7) == 0);
                t[y * TEX + x] = mortar ? col_shade(base, 1.35f)
                                        : col_noise(base, 10, x, y, salt + row);
            }
        break;
    case PAT_TILES:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int edge = ((x & 7) == 0) || ((y & 7) == 0);
                t[y * TEX + x] = edge ? col_shade(base, 0.7f)
                                      : col_noise(base, 8, x, y, salt);
            }
        break;
    case PAT_SMOOTH:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                t[y * TEX + x] = col_noise(base, 5, x, y, salt);
        break;
    case PAT_CHISELED:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int b = (x < 2 || y < 2 || x > 13 || y > 13);
                int m = (x >= 5 && x <= 10 && y >= 5 && y <= 10);
                t[y * TEX + x] = b ? col_shade(base, 0.75f)
                                   : m ? col_shade(acc, 1.0f)
                                       : col_noise(base, 7, x, y, salt);
            }
        break;
    case PAT_GLASS:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int edge = (x == 0 || y == 0 || x == TEX - 1 || y == TEX - 1);
                int glint = (x + y == 5 || x + y == 6);
                t[y * TEX + x] = edge ? col_shade(base, 1.12f)
                                      : glint ? col_shade(base, 1.3f)
                                              : col_shade(base, 0.94f);
            }
        break;
    case PAT_ORE:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                t[y * TEX + x] = col_noise(base, 14, x, y, salt);
        for (int b = 0; b < 5; b++) {
            int cx = 2 + (int)(inoise(b, 1, salt) * 12);
            int cy = 2 + (int)(inoise(b, 2, salt) * 12);
            float r = 1.2f + inoise(b, 3, salt) * 1.4f;
            for (int y = 0; y < TEX; y++)
                for (int x = 0; x < TEX; x++) {
                    float dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy <= r * r)
                        t[y * TEX + x] = col_noise(acc, 12, x, y, salt + b);
                }
        }
        break;
    case PAT_FOLIAGE:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float n = inoise(x, y, salt);
                t[y * TEX + x] = col_shade(col_noise(base, 22, x, y, salt),
                                           n < 0.18f ? 0.6f : 1.0f);
            }
        break;
    case PAT_CLOTH:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int weave = ((x + y) & 1);
                t[y * TEX + x] = col_shade(col_noise(base, 8, x, y, salt),
                                           weave ? 1.04f : 0.96f);
            }
        break;
    case PAT_METAL:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float f = 0.88f + 0.24f * (1.0f - fabsf(x - 7.5f) / 8.0f);
                t[y * TEX + x] = col_shade(col_noise(base, 6, x, y, salt), f);
            }
        break;
    case PAT_CRYSTAL:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int band = ((x + y) >> 2) & 1;
                t[y * TEX + x] = col_shade(col_noise(base, 10, x, y, salt),
                                           band ? 1.15f : 0.85f);
            }
        break;
    case PAT_LIQUID:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float w = sinf((x + y * 0.6f) * 0.9f) * 0.06f;
                t[y * TEX + x] = col_shade(col_noise(base, 8, x, y, salt),
                                           1.0f + w);
            }
        break;
    case PAT_POWDER:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                t[y * TEX + x] = col_noise(base, 24, x, y, salt);
        break;
    case PAT_SAND:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                t[y * TEX + x] = col_noise(base, 12, x, y, salt);
        break;
    case PAT_SCULK:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float n = inoise(x, y, salt);
                t[y * TEX + x] = n > 0.88f ? col_shade(acc, 1.4f)
                                           : col_noise(base, 10, x, y, salt);
            }
        break;
    case PAT_GLOW:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                float dx = (x - 7.5f) / 8.0f, dy = (y - 7.5f) / 8.0f;
                float f = 1.25f - 0.45f * sqrtf(dx * dx + dy * dy);
                t[y * TEX + x] = col_shade(col_noise(base, 10, x, y, salt), f);
            }
        break;
    case PAT_WEB:
        tx_clear(t, 0);
        for (int i = 0; i < 8; i++) {
            float a = i * 0.785398f;
            tx_line(t, 8, 8, (int)(8 + cosf(a) * 8), (int)(8 + sinf(a) * 8),
                    A_OPAQUE | 0xe8e8e8);
        }
        for (int r = 3; r <= 7; r += 2)
            for (int i = 0; i < 32; i++) {
                float a = i * 0.19635f;
                tx_px(t, (int)(8 + cosf(a) * r), (int)(8 + sinf(a) * r),
                      A_OPAQUE | 0xd0d0d0);
            }
        break;
    case PAT_HONEY:
    case PAT_SLIME:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++) {
                int inner = (x > 2 && x < 13 && y > 2 && y < 13);
                t[y * TEX + x] = inner ? col_shade(base, 1.12f)
                                       : col_noise(base, 8, x, y, salt);
            }
        break;
    case PAT_NOISE:
    default:
        for (int y = 0; y < TEX; y++)
            for (int x = 0; x < TEX; x++)
                t[y * TEX + x] = col_noise(base, 16, x, y, salt);
        break;
    }
}

/* Item sprites: drawn on transparent background, then bevelled. */
static void synth_item(uint32_t *t, int pattern, uint32_t base, uint32_t acc,
                       uint32_t salt) {
    const uint32_t handle = A_OPAQUE | 0x6B4B2A;   /* wooden haft */
    const uint32_t dark   = col_shade(base, 0.65f);
    tx_clear(t, 0);

    switch (pattern) {
    case PAT_SWORD:
        tx_line(t, 4, 12, 11, 5, base);
        tx_line(t, 3, 12, 10, 5, col_shade(base, 1.2f));
        tx_line(t, 3, 10, 6, 13, handle);
        tx_line(t, 2, 12, 4, 14, handle);
        tx_px(t, 12, 4, col_shade(base, 1.3f));
        break;
    case PAT_PICKAXE:
        tx_line(t, 4, 12, 11, 5, handle);
        tx_line(t, 5, 5, 12, 5, base);
        tx_line(t, 4, 6, 6, 4, base);
        tx_line(t, 11, 6, 13, 4, base);
        tx_px(t, 8, 4, col_shade(base, 1.2f));
        break;
    case PAT_AXE:
        tx_line(t, 4, 12, 10, 6, handle);
        tx_rect(t, 8, 3, 4, 5, base);
        tx_line(t, 7, 4, 7, 7, col_shade(base, 1.2f));
        tx_px(t, 12, 5, dark);
        break;
    case PAT_SHOVEL:
        tx_line(t, 4, 12, 10, 6, handle);
        tx_rect(t, 9, 3, 4, 4, base);
        tx_px(t, 8, 7, col_shade(base, 1.2f));
        break;
    case PAT_HOE:
        tx_line(t, 4, 12, 10, 6, handle);
        tx_rect(t, 9, 3, 4, 2, base);
        tx_px(t, 8, 5, base);
        break;
    case PAT_SPEAR:
        tx_line(t, 3, 13, 11, 5, handle);
        tx_line(t, 10, 5, 13, 2, base);
        tx_px(t, 12, 4, base);
        tx_px(t, 11, 3, col_shade(base, 1.25f));
        break;
    case PAT_BOW:
        for (int i = 0; i < 14; i++) {
            float a = -1.1f + i * 0.157f;
            tx_px(t, (int)(4 + cosf(a) * 8), (int)(8 + sinf(a) * 7), base);
        }
        tx_line(t, 4, 2, 4, 14, A_OPAQUE | 0xDCDCDC);
        break;
    case PAT_ROD:
        tx_line(t, 3, 13, 12, 4, base);
        tx_line(t, 4, 13, 13, 4, col_shade(base, 1.15f));
        break;
    case PAT_ARROW:
        tx_line(t, 3, 13, 11, 5, A_OPAQUE | 0xA98B6B);
        tx_line(t, 10, 4, 13, 3, base);
        tx_px(t, 12, 4, base);
        tx_line(t, 2, 12, 4, 14, base);        /* fletching */
        tx_line(t, 3, 11, 5, 13, col_shade(base, 1.2f));
        break;
    case PAT_HELMET:
        tx_rect(t, 4, 4, 8, 5, base);
        tx_rect(t, 3, 9, 10, 2, col_shade(base, 0.85f));
        tx_rect(t, 6, 6, 4, 2, dark);
        break;
    case PAT_CHESTPLATE:
        tx_rect(t, 4, 4, 8, 8, base);
        tx_rect(t, 2, 5, 2, 4, col_shade(base, 0.85f));
        tx_rect(t, 12, 5, 2, 4, col_shade(base, 0.85f));
        tx_rect(t, 7, 5, 2, 5, dark);
        break;
    case PAT_LEGGINGS:
        tx_rect(t, 4, 3, 8, 3, base);
        tx_rect(t, 4, 6, 3, 7, base);
        tx_rect(t, 9, 6, 3, 7, base);
        break;
    case PAT_BOOTS:
        tx_rect(t, 3, 7, 4, 4, base);
        tx_rect(t, 9, 7, 4, 4, base);
        tx_rect(t, 3, 11, 5, 2, col_shade(base, 0.8f));
        tx_rect(t, 9, 11, 5, 2, col_shade(base, 0.8f));
        break;
    case PAT_INGOT:
        for (int y = 5; y < 11; y++) {
            int inset = (y < 8) ? (8 - y) : (y - 7);
            tx_rect(t, 2 + inset, y, 12 - 2 * inset, 1, base);
        }
        tx_line(t, 5, 6, 10, 6, col_shade(base, 1.25f));
        break;
    case PAT_NUGGET:
        tx_disc(t, 8, 8, 3.2f, base);
        tx_px(t, 7, 6, col_shade(base, 1.3f));
        break;
    case PAT_GEMSTONE:
        tx_line(t, 5, 5, 10, 5, col_shade(base, 1.2f));
        tx_rect(t, 4, 6, 8, 3, base);
        tx_line(t, 5, 9, 10, 9, base);
        tx_line(t, 6, 10, 9, 10, col_shade(base, 0.8f));
        tx_px(t, 7, 11, dark);
        tx_px(t, 6, 6, A_OPAQUE | 0xffffff);
        break;
    case PAT_ROUND:
        tx_disc(t, 8, 8.5f, 4.6f, base);
        tx_px(t, 6, 6, col_shade(base, 1.4f));
        tx_px(t, 7, 6, col_shade(base, 1.25f));
        break;
    case PAT_MEAT:
        tx_disc(t, 8, 8, 5.0f, base);
        tx_disc(t, 8, 8, 2.6f, col_shade(base, 1.25f));
        tx_rect(t, 2, 7, 3, 2, A_OPAQUE | 0xE8E0D0);
        break;
    case PAT_SEED:
        tx_disc(t, 6, 6, 1.8f, base);
        tx_disc(t, 10, 8, 1.8f, base);
        tx_disc(t, 7, 11, 1.8f, col_shade(base, 0.85f));
        break;
    case PAT_DISC:
        tx_disc(t, 8, 8, 6.4f, A_OPAQUE | 0x232323);
        tx_disc(t, 8, 8, 3.0f, base);
        tx_disc(t, 8, 8, 1.0f, A_OPAQUE | 0x101010);
        tx_px(t, 5, 4, A_OPAQUE | 0x505050);
        break;
    case PAT_EGG:
        for (int y = 3; y < 13; y++) {
            float r = 4.2f * sinf((y - 2.0f) / 11.0f * 3.14159f);
            tx_rect(t, (int)(8 - r), y, (int)(2 * r + 0.5f), 1, base);
        }
        tx_px(t, 6, 5, col_shade(base, 1.3f));
        break;
    case PAT_BUCKET:
        for (int y = 5; y < 13; y++) {
            int inset = (y - 5) / 4;
            tx_rect(t, 3 + inset, y, 10 - 2 * inset, 1, A_OPAQUE | 0xC0C0C8);
        }
        tx_rect(t, 4, 6, 8, 3, base);            /* contents */
        tx_line(t, 3, 4, 12, 4, A_OPAQUE | 0xE0E0E8);
        break;
    case PAT_BOTTLE:
        tx_rect(t, 7, 2, 2, 3, A_OPAQUE | 0xB0A08A);   /* cork + neck */
        for (int y = 5; y < 14; y++) {
            float r = 2.0f + 2.6f * (y - 4) / 9.0f;
            tx_rect(t, (int)(8 - r), y, (int)(2 * r + 0.5f), 1,
                    y < 7 ? A_OPAQUE | 0xD8E8F0 : base);
        }
        tx_px(t, 6, 9, col_shade(base, 1.4f));
        break;
    case PAT_BOOK:
    case PAT_ENCHANT:
        tx_rect(t, 3, 3, 10, 11, base);
        tx_rect(t, 5, 3, 8, 11, A_OPAQUE | 0xEFE7D2);
        tx_rect(t, 3, 3, 2, 11, col_shade(base, 0.8f));
        tx_line(t, 7, 5, 11, 5, A_OPAQUE | 0xB8AE96);
        tx_line(t, 7, 8, 11, 8, A_OPAQUE | 0xB8AE96);
        tx_line(t, 7, 11, 10, 11, A_OPAQUE | 0xB8AE96);
        if (pattern == PAT_ENCHANT) tx_px(t, 12, 4, A_OPAQUE | 0xFFE8FF);
        break;
    case PAT_PAPER:
        tx_rect(t, 3, 2, 10, 12, base);
        for (int y = 4; y < 13; y += 2) tx_line(t, 5, y, 10, y, col_shade(base, 0.7f));
        break;
    case PAT_TRIM:
        tx_rect(t, 2, 2, 12, 12, base);
        tx_rect(t, 4, 4, 8, 8, col_shade(base, 0.82f));
        tx_line(t, 8, 4, 8, 11, acc);
        tx_line(t, 5, 7, 11, 7, acc);
        tx_px(t, 8, 7, col_shade(acc, 1.3f));
        break;
    case PAT_SHERD:
        for (int y = 3; y < 13; y++) {
            int w = 10 - abs(y - 8);
            tx_rect(t, 8 - w / 2, y, w, 1, col_noise(base, 10, 0, y, salt));
        }
        tx_line(t, 6, 6, 9, 9, acc);
        tx_px(t, 9, 6, acc);
        break;
    case PAT_POT:
        tx_rect(t, 5, 3, 6, 2, col_shade(base, 0.85f));   /* rim */
        for (int y = 5; y < 14; y++) {
            float r = 4.6f - 2.2f * fabsf((y - 9.5f) / 4.5f);
            tx_rect(t, (int)(8 - r), y, (int)(2 * r + 0.5f), 1,
                    col_noise(base, 8, 0, y, salt));
        }
        tx_px(t, 7, 8, acc); tx_px(t, 9, 9, acc);
        break;
    case PAT_SKULL:
        tx_rect(t, 3, 3, 10, 9, base);
        tx_rect(t, 5, 6, 2, 2, A_OPAQUE | 0x101010);
        tx_rect(t, 9, 6, 2, 2, A_OPAQUE | 0x101010);
        tx_rect(t, 6, 10, 4, 1, col_shade(base, 0.7f));
        break;
    case PAT_CART:
        tx_rect(t, 2, 6, 12, 5, base);
        tx_rect(t, 4, 8, 8, 2, col_shade(base, 0.6f));
        tx_disc(t, 5, 12, 1.8f, A_OPAQUE | 0x3A3A3A);
        tx_disc(t, 11, 12, 1.8f, A_OPAQUE | 0x3A3A3A);
        break;
    case PAT_HULL:
        for (int y = 6; y < 12; y++) {
            int inset = (y > 9) ? (y - 9) * 2 : 0;
            tx_rect(t, 1 + inset, y, 14 - 2 * inset, 1,
                    col_noise(base, 10, 0, y, salt));
        }
        tx_rect(t, 3, 6, 10, 1, col_shade(base, 1.2f));
        break;
    case PAT_PLANT:
        for (int x = 5; x <= 10; x++) {
            int h = 4 + (int)(inoise(x, 0, salt) * 6);
            tx_line(t, x, 15, x + ((x & 1) ? 1 : -1), 15 - h,
                    col_shade(base, 0.85f + 0.3f * inoise(x, 1, salt)));
        }
        break;
    case PAT_FLOWER:
        tx_line(t, 8, 15, 8, 8, A_OPAQUE | 0x3E7A2E);
        tx_px(t, 7, 11, A_OPAQUE | 0x4E8A3E);
        tx_px(t, 9, 12, A_OPAQUE | 0x4E8A3E);
        tx_disc(t, 8, 6, 2.8f, base);
        tx_px(t, 8, 6, acc);
        break;
    case PAT_MUSHROOM:
        tx_rect(t, 7, 9, 3, 5, A_OPAQUE | 0xE0D8C8);
        tx_disc(t, 8, 8, 4.4f, base);
        tx_rect(t, 4, 9, 9, 6, 0);
        tx_rect(t, 7, 9, 3, 5, A_OPAQUE | 0xE0D8C8);
        tx_px(t, 6, 6, col_shade(base, 1.3f));
        break;
    case PAT_CORAL:
        tx_line(t, 8, 15, 8, 7, base);
        tx_line(t, 8, 10, 4, 6, base);
        tx_line(t, 8, 9, 12, 5, base);
        tx_px(t, 4, 5, col_shade(base, 1.25f));
        tx_px(t, 12, 4, col_shade(base, 1.25f));
        tx_px(t, 8, 6, col_shade(base, 1.25f));
        break;
    default:                                   /* generic sprite */
        tx_rect(t, 4, 4, 8, 8, base);
        tx_px(t, 5, 5, col_shade(base, 1.3f));
        break;
    }
    tx_bevel(t);
}

/* Is this pattern drawn as a sprite (transparent background)? */
static int pattern_is_sprite(int pattern) {
    return pattern >= PAT_SHERD || pattern == PAT_WEB ||
           pattern == PAT_PLANT || pattern == PAT_FLOWER ||
           pattern == PAT_CORAL;
}

static void synth_layer(uint32_t *t, int pattern, uint32_t base, uint32_t acc,
                        int role, uint32_t salt) {
    if (pattern_is_sprite(pattern)) synth_item(t, pattern, base, acc, salt);
    else                            synth_block(t, pattern, base, acc, role, salt);
}

/* ----------------------------------------------------------------------- */
/* Layer allocation with de-duplication                                     */
/* ----------------------------------------------------------------------- */

static uint16_t layer_get(int pattern, uint32_t base, uint32_t acc, int role) {
    uint64_t key = ((uint64_t)pattern << 56) ^ ((uint64_t)role << 52) ^
                   ((uint64_t)base << 24) ^ (uint64_t)acc;
    if (!key) key = 1;
    uint32_t h = (uint32_t)((key ^ (key >> 32)) * 2654435761u) & (LAYER_HASH - 1);
    for (int probe = 0; probe < LAYER_HASH; probe++) {
        uint32_t i = (h + probe) & (LAYER_HASH - 1);
        if (g_layer_key[i] == key) return g_layer_slot[i];
        if (g_layer_key[i] == 0) {
            if (g_layer_count >= MAX_LAYERS) return 0;   /* pool full: share */
            uint16_t slot = (uint16_t)g_layer_count++;
            synth_layer(g_layers[slot], pattern, base, acc, role,
                        (uint32_t)(key * 2654435761u) >> 8);
            g_layer_key[i] = key;
            g_layer_slot[i] = slot;
            return slot;
        }
    }
    return 0;
}

/* Builds every item's textures. Idempotent -- safe to call again. */
static void build_item_textures(void) {
    g_layer_count = 0;
    memset(g_layer_key, 0, sizeof g_layer_key);
    memset(g_layer_slot, 0, sizeof g_layer_slot);

    /* layer 0 is the "missing texture" magenta/black check */
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++)
            g_layers[0][y * TEX + x] =
                (((x >> 3) ^ (y >> 3)) & 1) ? (A_OPAQUE | 0xF800F8)
                                            : (A_OPAQUE | 0x101010);
    g_layer_count = 1;

    for (int i = 0; i < ITEM_COUNT; i++) {
        const ItemDef *d = &g_items[i];
        if (i == IT_AIR) {
            g_item_layer[i][0] = g_item_layer[i][1] = g_item_layer[i][2] = 0;
            continue;
        }
        g_item_layer[i][0] = layer_get(d->pattern, d->col_top, d->col_accent, 0);
        g_item_layer[i][1] = layer_get(d->pattern, d->col_side, d->col_accent, 1);
        g_item_layer[i][2] = layer_get(d->pattern, d->col_bottom, d->col_accent, 2);
    }
}

/* Gives item `id`'s face its own layer, copied from whatever it shares now,
   and returns the new index (0 when the pool is exhausted). Needed before
   overwriting a layer for one item -- layers are shared by construction. */
static uint16_t layer_private(int id, int face) {
    if ((unsigned)id >= ITEM_COUNT || (unsigned)face > 2) return 0;
    if (g_layer_count >= MAX_LAYERS) return 0;
    uint16_t old = g_item_layer[id][face];
    uint16_t slot = (uint16_t)g_layer_count++;
    memcpy(g_layers[slot], g_layers[old], sizeof g_layers[slot]);
    g_item_layer[id][face] = slot;
    return slot;
}

/* ----------------------------------------------------------------------- */
/* Accessors                                                                */
/* ----------------------------------------------------------------------- */

static inline const ItemDef *item_def(int id) {
    return &g_items[(unsigned)id < ITEM_COUNT ? id : 0];
}
static inline const char *item_name(int id) {
    return g_item_names[(unsigned)id < ITEM_COUNT ? id : 0];
}
static inline const uint32_t *item_tex(int id, int face) {
    if ((unsigned)id >= ITEM_COUNT) id = 0;
    if ((unsigned)face > 2) face = 1;
    return g_layers[g_item_layer[id][face]];
}
static inline int item_flags(int id) { return item_def(id)->flags; }
static inline int item_is_full_cube(int id) {
    return (item_def(id)->flags & IF_FULLCUBE) != 0;
}
static inline int item_is_placeable(int id) {
    return (item_def(id)->flags & IF_PLACEABLE) != 0;
}
static inline int item_is_liquid(int id) {
    return (item_def(id)->flags & IF_LIQUID) != 0;
}
static inline int item_is_solid(int id) {
    return (item_def(id)->flags & IF_SOLID) != 0;
}
static inline int item_light(int id) { return item_def(id)->light; }

/* ----------------------------------------------------------------------- */
/* Texture animation                                                        */
/*                                                                          */
/* Applied when a texel is sampled, so an animated texture costs nothing to  */
/* store. `phase` is a free-running 0..1 ramp (one loop per cycle).          */
/* ----------------------------------------------------------------------- */

static uint32_t item_sample(int id, int face, int tu, int tv, float phase) {
    const ItemDef *d = item_def(id);
    const uint32_t *tex = item_tex(id, face);

    switch (d->tex_anim) {
    case TA_FLOW: {                     /* scroll the texture along v */
        int off = (int)(phase * TEX) % TEX;
        tv = (tv + off) & (TEX - 1);
        break;
    }
    case TA_SWIRL: {                    /* rotate about the centre */
        float a = phase * 6.2831853f;
        float ca = cosf(a), sa = sinf(a);
        float dx = tu - 7.5f, dy = tv - 7.5f;
        int rx = (int)(7.5f + dx * ca - dy * sa);
        int ry = (int)(7.5f + dx * sa + dy * ca);
        if (rx >= 0 && rx < TEX && ry >= 0 && ry < TEX) { tu = rx; tv = ry; }
        break;
    }
    default: break;
    }

    uint32_t c = tex[tv * TEX + tu];
    if (!(c >> 24)) return c;           /* keep cut-out texels transparent */

    switch (d->tex_anim) {
    case TA_FIRE: {
        float f = 0.82f + 0.30f * sinf(phase * 6.2831853f + tv * 0.55f +
                                       tu * 0.21f);
        return (c & 0xff000000u) | (col_shade(c, f) & 0x00ffffffu);
    }
    case TA_PULSE: {
        float f = 0.88f + 0.22f * sinf(phase * 6.2831853f);
        return (c & 0xff000000u) | (col_shade(c, f) & 0x00ffffffu);
    }
    case TA_SPARKLE: {
        /* a couple of texels catch the light each frame */
        int frame = (int)(phase * 8.0f) & 7;
        if (inoise(tu + frame * 17, tv - frame * 5, 0x5A17u) > 0.93f)
            return (c & 0xff000000u) | (col_shade(c, 1.9f) & 0x00ffffffu);
        return c;
    }
    case TA_GLINT: {
        /* a diagonal highlight sweeping across the sprite */
        float band = fmodf((tu + tv) * 0.0625f - phase, 1.0f);
        if (band < 0) band += 1.0f;
        if (band < 0.16f) {
            float f = 1.0f + 0.55f * (1.0f - band / 0.16f);
            return (c & 0xff000000u) | (col_shade(c, f) & 0x00ffffffu);
        }
        return c;
    }
    default: return c;
    }
}

#endif /* ITEMS_H */
