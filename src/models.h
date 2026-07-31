/* Box models.
 *
 * Every item's shape is a short list of axis-aligned boxes in Minecraft's
 * 0..16 model space, exactly like a resource pack's `elements` array. The
 * renderer uses them three ways:
 *
 *   - the world raycaster intersects them per cell, so a slab really is half
 *     a block, a fence really is a post, and a torch really is a stick;
 *   - collision tests them, so you can stand on a slab;
 *   - the 3D icon / held-item / dropped-item renderer rasterises them.
 *
 * Plants use two crossed *planes* rather than the diagonal X of Minecraft's
 * "cross" model: staying axis-aligned keeps every intersection an AABB slab
 * test, which matters when it runs once per pixel.
 *
 * Item-only models (tools, potions, sherds, ...) are all the same shape: the
 * 16x16 sprite extruded one unit deep, again the way Minecraft draws items.
 */
#ifndef MODELS_H
#define MODELS_H

#include "items.h"

typedef struct { uint8_t x0, y0, z0, x1, y1, z1; } Box;

#define BOXES(name, ...) static const Box name[] = { __VA_ARGS__ }

BOXES(bx_cube,     {0,0,0,16,16,16});
BOXES(bx_slab,     {0,0,0,16,8,16});
BOXES(bx_stairs,   {0,0,0,16,8,16}, {0,8,8,16,16,16});
BOXES(bx_wall,     {4,0,4,12,16,12}, {0,0,5,16,13,11}, {5,0,0,11,13,16});
BOXES(bx_fence,    {6,0,6,10,16,10},
                   {0,6,7,16,9,9},  {7,6,0,9,9,16},
                   {0,12,7,16,15,9},{7,12,0,9,15,16});
BOXES(bx_gate,     {0,5,7,2,16,9}, {14,5,7,16,16,9},
                   {2,6,7,14,9,9}, {2,12,7,14,15,9});
BOXES(bx_door,     {0,0,0,3,16,16});
BOXES(bx_trapdoor, {0,0,0,16,3,16});
BOXES(bx_button,   {5,0,6,11,2,10});
BOXES(bx_plate,    {1,0,1,15,1,15});
BOXES(bx_carpet,   {0,0,0,16,1,16});
BOXES(bx_pane,     {7,0,0,9,16,16}, {0,0,7,16,16,9});
BOXES(bx_ladder,   {0,0,13,16,16,16});
BOXES(bx_torch,    {7,0,7,9,10,9});
BOXES(bx_lantern,  {5,1,5,11,8,11}, {7,8,7,9,16,9});
BOXES(bx_chain,    {7,0,7,9,16,9});
BOXES(bx_cross,    {0,0,7,16,16,9}, {7,0,0,9,16,16});
BOXES(bx_bush,     {0,0,6,16,16,10}, {6,0,0,10,16,16});
BOXES(bx_pot,      {4,0,4,12,13,12}, {3,13,3,13,16,13});
BOXES(bx_chest,    {1,0,1,15,14,15});
BOXES(bx_bed,      {0,0,0,16,9,16});
BOXES(bx_sign,     {7,0,7,9,9,9}, {1,9,7,15,16,9});
BOXES(bx_banner,   {7,0,7,9,4,9}, {2,4,7,14,16,9});
BOXES(bx_shelf,    {0,0,10,16,16,16}, {0,6,0,16,9,16});
BOXES(bx_anvil,    {2,0,2,14,4,14}, {5,4,5,11,10,11}, {1,10,3,15,16,13});
BOXES(bx_head,     {4,0,4,12,8,12});
BOXES(bx_campfire, {0,0,0,16,4,16}, {4,4,4,12,9,12});
BOXES(bx_rail,     {0,0,0,16,1,16});
BOXES(bx_candle,   {7,0,7,9,10,9});
BOXES(bx_small,    {4,0,4,12,10,12});
BOXES(bx_frame,    {0,0,15,16,16,16});
BOXES(bx_sprite,   {0,0,7,16,16,9});     /* every item model: extruded sprite */

typedef struct { const Box *box; uint8_t n; } ModelDef;

#define MDEF(a) { a, (uint8_t)(sizeof(a) / sizeof(Box)) }

static const ModelDef g_models[MODEL_COUNT] = {
    [MODEL_CUBE]     = MDEF(bx_cube),   [MODEL_COLUMN]   = MDEF(bx_cube),
    [MODEL_SLAB]     = MDEF(bx_slab),   [MODEL_STAIRS]   = MDEF(bx_stairs),
    [MODEL_WALL]     = MDEF(bx_wall),   [MODEL_FENCE]    = MDEF(bx_fence),
    [MODEL_GATE]     = MDEF(bx_gate),   [MODEL_DOOR]     = MDEF(bx_door),
    [MODEL_TRAPDOOR] = MDEF(bx_trapdoor), [MODEL_BUTTON] = MDEF(bx_button),
    [MODEL_PLATE]    = MDEF(bx_plate),  [MODEL_CARPET]   = MDEF(bx_carpet),
    [MODEL_PANE]     = MDEF(bx_pane),   [MODEL_LADDER]   = MDEF(bx_ladder),
    [MODEL_TORCH]    = MDEF(bx_torch),  [MODEL_LANTERN]  = MDEF(bx_lantern),
    [MODEL_CHAIN]    = MDEF(bx_chain),  [MODEL_CROSS]    = MDEF(bx_cross),
    [MODEL_BUSH]     = MDEF(bx_bush),   [MODEL_POT]      = MDEF(bx_pot),
    [MODEL_CHEST]    = MDEF(bx_chest),  [MODEL_BED]      = MDEF(bx_bed),
    [MODEL_SIGN]     = MDEF(bx_sign),   [MODEL_BANNER]   = MDEF(bx_banner),
    [MODEL_SHELF]    = MDEF(bx_shelf),  [MODEL_ANVIL]    = MDEF(bx_anvil),
    [MODEL_HEAD]     = MDEF(bx_head),   [MODEL_CAMPFIRE] = MDEF(bx_campfire),
    [MODEL_RAIL]     = MDEF(bx_rail),   [MODEL_CANDLE]   = MDEF(bx_candle),
    [MODEL_SMALLBOX] = MDEF(bx_small),  [MODEL_FRAME]    = MDEF(bx_frame),
    [MODEL_FLAT]     = MDEF(bx_sprite), [MODEL_TOOL]     = MDEF(bx_sprite),
    [MODEL_ROD]      = MDEF(bx_sprite), [MODEL_ARMOR]    = MDEF(bx_sprite),
    [MODEL_BOTTLE]   = MDEF(bx_sprite), [MODEL_BOOK]     = MDEF(bx_sprite),
    [MODEL_SHEET]    = MDEF(bx_sprite), [MODEL_SHERD]    = MDEF(bx_sprite),
    [MODEL_DISC]     = MDEF(bx_sprite), [MODEL_BOAT]     = MDEF(bx_sprite),
    [MODEL_MINECART] = MDEF(bx_sprite), [MODEL_FOOD]     = MDEF(bx_sprite),
    [MODEL_BOW]      = MDEF(bx_sprite), [MODEL_INGOT]    = MDEF(bx_sprite),
    [MODEL_GEM]      = MDEF(bx_sprite), [MODEL_DUST]     = MDEF(bx_sprite),
};

static inline const ModelDef *item_model(int id) {
    int m = item_def(id)->model;
    return &g_models[(unsigned)m < MODEL_COUNT ? m : MODEL_CUBE];
}

/* ----------------------------------------------------------------------- */
/* Ray / model intersection                                                 */
/* ----------------------------------------------------------------------- */

/* Slab test of one box, expressed in world coordinates around cell (cx,cy,cz).
   Returns 1 and fills *t (entry distance) and the face normal on a hit in
   front of the ray origin. */
static int box_ray(const Box *b, int cx, int cy, int cz,
                   float ox, float oy, float oz,
                   float ix, float iy, float iz, float maxdist,
                   float *out_t, int *nx, int *ny, int *nz) {
    const float s = 1.0f / 16.0f;
    float lo[3] = { cx + b->x0 * s, cy + b->y0 * s, cz + b->z0 * s };
    float hi[3] = { cx + b->x1 * s, cy + b->y1 * s, cz + b->z1 * s };
    float o[3]  = { ox, oy, oz };
    float inv[3] = { ix, iy, iz };

    float tmin = 0.0f, tmax = maxdist;
    int axis = -1, sign = 1;
    for (int a = 0; a < 3; a++) {
        if (inv[a] == 0.0f) {                     /* parallel to this slab */
            if (o[a] < lo[a] || o[a] > hi[a]) return 0;
            continue;
        }
        float t1 = (lo[a] - o[a]) * inv[a];
        float t2 = (hi[a] - o[a]) * inv[a];
        int sg = 1;
        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; sg = -1; }
        if (t1 > tmin) { tmin = t1; axis = a; sign = sg; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return 0;
    }
    if (axis < 0) return 0;                       /* origin inside the box  */
    if (tmin <= 0.0f || tmin > maxdist) return 0;

    *out_t = tmin;
    *nx = *ny = *nz = 0;
    if (axis == 0)      *nx = -sign;
    else if (axis == 1) *ny = -sign;
    else                *nz = -sign;
    return 1;
}

/* Which texture face (0 top, 1 side, 2 bottom) a normal selects. */
static inline int face_of_normal(int nx, int ny, int nz) {
    (void)nx; (void)nz;
    return ny > 0 ? 0 : ny < 0 ? 2 : 1;
}

/* Texture coordinates for a hit point, matching the cube case in the
   raycaster: horizontal faces use x/z, vertical faces use the tangent axis
   and height. */
static inline void face_uv(int nx, int nz, float hx, float hy, float hz,
                           float *u, float *v) {
    float fx = hx - (float)(int)hx, fy = hy - (float)(int)hy;
    float fz = hz - (float)(int)hz;
    if (hx < 0) fx += 1.0f;
    if (hy < 0) fy += 1.0f;
    if (hz < 0) fz += 1.0f;
    if (nx != 0)      { *u = fz; *v = 1.0f - fy; }
    else if (nz != 0) { *u = fx; *v = 1.0f - fy; }
    else              { *u = fx; *v = fz; }
}

/* Nearest visible hit of a non-cube model inside cell (cx,cy,cz).
   Texels with alpha < 128 are see-through, so a cross-shaped plant really
   shows the world behind its gaps. Returns 1 on a hit. */
static int model_ray_hit(int id, int cx, int cy, int cz,
                         float ox, float oy, float oz,
                         float ix, float iy, float iz, float maxdist,
                         float *out_t, int *nx, int *ny, int *nz) {
    const ModelDef *m = item_model(id);
    int cutout = (item_def(id)->flags & IF_CUTOUT) != 0;
    float best = maxdist;
    int found = 0, bnx = 0, bny = 0, bnz = 0;

    float dx = ix != 0.0f ? 1.0f / ix : 0.0f;
    float dy = iy != 0.0f ? 1.0f / iy : 0.0f;
    float dz = iz != 0.0f ? 1.0f / iz : 0.0f;

    for (int i = 0; i < m->n; i++) {
        float t; int tx, ty, tz;
        if (!box_ray(&m->box[i], cx, cy, cz, ox, oy, oz, ix, iy, iz, best,
                     &t, &tx, &ty, &tz))
            continue;
        if (cutout) {                        /* see through empty texels */
            float u, v;
            face_uv(tx, tz, ox + dx * t, oy + dy * t, oz + dz * t, &u, &v);
            int su = (int)(u * TEX), sv = (int)(v * TEX);
            if (su < 0) su = 0; else if (su >= TEX) su = TEX - 1;
            if (sv < 0) sv = 0; else if (sv >= TEX) sv = TEX - 1;
            uint32_t texel = item_tex(id, face_of_normal(tx, ty, tz))
                                     [sv * TEX + su];
            if ((texel >> 24) < 128) continue;
        }
        best = t; found = 1; bnx = tx; bny = ty; bnz = tz;
    }
    if (!found) return 0;
    *out_t = best; *nx = bnx; *ny = bny; *nz = bnz;
    return 1;
}

/* ----------------------------------------------------------------------- */
/* Collision                                                                */
/* ----------------------------------------------------------------------- */

/* Does the model in cell (cx,cy,cz) overlap the world-space box given? */
static int model_overlaps(int id, int cx, int cy, int cz,
                          float x0, float y0, float z0,
                          float x1, float y1, float z1) {
    if (!(item_def(id)->flags & IF_SOLID)) return 0;
    const ModelDef *m = item_model(id);
    const float s = 1.0f / 16.0f;
    for (int i = 0; i < m->n; i++) {
        const Box *b = &m->box[i];
        if (x1 > cx + b->x0 * s && x0 < cx + b->x1 * s &&
            y1 > cy + b->y0 * s && y0 < cy + b->y1 * s &&
            z1 > cz + b->z0 * s && z0 < cz + b->z1 * s)
            return 1;
    }
    return 0;
}

/* Height of the model's collision surface, used by the "can I step here"
   checks and by the tests. 0 when the model has no collision at all. */
static float model_top(int id) {
    if (!(item_def(id)->flags & IF_SOLID)) return 0.0f;
    const ModelDef *m = item_model(id);
    int top = 0;
    for (int i = 0; i < m->n; i++)
        if (m->box[i].y1 > top) top = m->box[i].y1;
    return top / 16.0f;
}

#endif /* MODELS_H */
