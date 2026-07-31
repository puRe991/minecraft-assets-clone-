/* Software rasteriser for box models.
 *
 * The world is drawn by raycasting, but items are not blocks in a grid --
 * they hang in the player's hand, spin on the ground where they dropped and
 * sit in inventory slots. All three want the same thing: take a model's
 * boxes, put them through an animation pose and a transform, and fill
 * textured triangles with a depth test.
 *
 *   draw_item_ortho()  -- inventory icons and the first-person held item,
 *                         drawn isometrically into a small local depth buffer
 *   draw_item_world()  -- dropped items, projected with the game camera and
 *                         depth-tested against the raycast frame
 */
#ifndef RENDER3D_H
#define RENDER3D_H

#include <math.h>
#include <stdint.h>

#include "anim.h"
#include "items.h"
#include "models.h"

/* ----------------------------------------------------------------------- */
/* Small matrix helpers (row-major 3x3)                                     */
/* ----------------------------------------------------------------------- */

typedef struct { float m[9]; } Mat3;

static Mat3 mat3_id(void) {
    Mat3 r = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
    return r;
}
static Mat3 mat3_mul(Mat3 a, Mat3 b) {
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += a.m[i * 3 + k] * b.m[k * 3 + j];
            r.m[i * 3 + j] = s;
        }
    return r;
}
static Mat3 mat3_rotx(float deg) {
    float a = deg * 0.01745329f, c = cosf(a), s = sinf(a);
    Mat3 r = {{1, 0, 0, 0, c, -s, 0, s, c}};
    return r;
}
static Mat3 mat3_roty(float deg) {
    float a = deg * 0.01745329f, c = cosf(a), s = sinf(a);
    Mat3 r = {{c, 0, s, 0, 1, 0, -s, 0, c}};
    return r;
}
static Mat3 mat3_rotz(float deg) {
    float a = deg * 0.01745329f, c = cosf(a), s = sinf(a);
    Mat3 r = {{c, -s, 0, s, c, 0, 0, 0, 1}};
    return r;
}
static void mat3_apply(Mat3 m, float x, float y, float z,
                       float *ox, float *oy, float *oz) {
    *ox = m.m[0] * x + m.m[1] * y + m.m[2] * z;
    *oy = m.m[3] * x + m.m[4] * y + m.m[5] * z;
    *oz = m.m[6] * x + m.m[7] * y + m.m[8] * z;
}

/* Rotation for a pose: yaw, then pitch, then roll. */
static Mat3 pose_matrix(const Pose *p) {
    return mat3_mul(mat3_rotz(p->rz), mat3_mul(mat3_rotx(p->rx),
                                               mat3_roty(p->ry)));
}

/* ----------------------------------------------------------------------- */
/* Render target                                                            */
/* ----------------------------------------------------------------------- */

typedef struct {
    uint32_t *color;
    int       w, h;          /* colour surface size                        */
    float    *depth;         /* depth buffer (never NULL)                  */
    int       dstride;       /* depth row stride                           */
    int       dx0, dy0;      /* depth buffer origin in colour coordinates  */
    int       cx0, cy0, cx1, cy1;   /* clip rectangle, exclusive max       */
} Target;

typedef struct { float x, y, z, u, v; } RVert;

/* Barycentric triangle fill. Depth is interpolated linearly, which is exact
   enough for objects a few pixels to a few dozen pixels across. */
static void raster_tri(Target *t, RVert a, RVert b, RVert c,
                       int item, int face, float phase, float shade) {
    float minxf = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    float maxxf = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    float minyf = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    float maxyf = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);

    int x0 = (int)floorf(minxf), x1 = (int)ceilf(maxxf);
    int y0 = (int)floorf(minyf), y1 = (int)ceilf(maxyf);
    if (x0 < t->cx0) x0 = t->cx0;
    if (y0 < t->cy0) y0 = t->cy0;
    if (x1 > t->cx1) x1 = t->cx1;
    if (y1 > t->cy1) y1 = t->cy1;
    if (x0 >= x1 || y0 >= y1) return;

    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (fabsf(area) < 1e-6f) return;
    float inv = 1.0f / area;

    for (int y = y0; y < y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f;
            float w0 = ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) * inv;
            float w1 = ((px - a.x) * (c.y - a.y) - (py - a.y) * (c.x - a.x)) * inv;
            if (w0 < 0 || w1 < 0 || w0 + w1 > 1.0f) continue;
            float w2 = 1.0f - w0 - w1;
            /* w2 weights a, w1 weights b, w0 weights c */
            float z = a.z * w2 + b.z * w1 + c.z * w0;
            float *dp = &t->depth[(y - t->dy0) * t->dstride + (x - t->dx0)];
            if (z >= *dp) continue;

            float u = a.u * w2 + b.u * w1 + c.u * w0;
            float v = a.v * w2 + b.v * w1 + c.v * w0;
            int tu = (int)(u * TEX), tv = (int)(v * TEX);
            if (tu < 0) tu = 0; else if (tu >= TEX) tu = TEX - 1;
            if (tv < 0) tv = 0; else if (tv >= TEX) tv = TEX - 1;

            uint32_t col = item_sample(item, face, tu, tv, phase);
            if ((col >> 24) < 128) continue;          /* cut-out texel */
            *dp = z;
            t->color[y * t->w + x] = col_shade(col, shade);
        }
    }
}

/* Face shading, matching the world renderer's per-face light. */
static const float g_face_shade[6] = {1.00f, 0.55f, 0.80f, 0.80f, 0.68f, 0.68f};
static const int   g_face_tex[6]   = {0, 2, 1, 1, 1, 1};

/* Corner and UV layout of the six faces of a box, in 0..16 model units.
   Index order: +Y, -Y, +X, -X, +Z, -Z. */
static void box_face(const Box *b, int f, int corner,
                     float *x, float *y, float *z, float *u, float *v) {
    float X0 = b->x0, Y0 = b->y0, Z0 = b->z0;
    float X1 = b->x1, Y1 = b->y1, Z1 = b->z1;
    float px[4], py[4], pz[4], pu[4], pv[4];
    switch (f) {
    case 0:  /* +Y */
        px[0]=X0; py[0]=Y1; pz[0]=Z0;  px[1]=X1; py[1]=Y1; pz[1]=Z0;
        px[2]=X1; py[2]=Y1; pz[2]=Z1;  px[3]=X0; py[3]=Y1; pz[3]=Z1;
        for (int i = 0; i < 4; i++) { pu[i] = px[i] / 16.0f; pv[i] = pz[i] / 16.0f; }
        break;
    case 1:  /* -Y */
        px[0]=X0; py[0]=Y0; pz[0]=Z1;  px[1]=X1; py[1]=Y0; pz[1]=Z1;
        px[2]=X1; py[2]=Y0; pz[2]=Z0;  px[3]=X0; py[3]=Y0; pz[3]=Z0;
        for (int i = 0; i < 4; i++) { pu[i] = px[i] / 16.0f; pv[i] = 1 - pz[i] / 16.0f; }
        break;
    case 2:  /* +X */
        px[0]=X1; py[0]=Y1; pz[0]=Z0;  px[1]=X1; py[1]=Y1; pz[1]=Z1;
        px[2]=X1; py[2]=Y0; pz[2]=Z1;  px[3]=X1; py[3]=Y0; pz[3]=Z0;
        for (int i = 0; i < 4; i++) { pu[i] = pz[i] / 16.0f; pv[i] = 1 - py[i] / 16.0f; }
        break;
    case 3:  /* -X */
        px[0]=X0; py[0]=Y1; pz[0]=Z1;  px[1]=X0; py[1]=Y1; pz[1]=Z0;
        px[2]=X0; py[2]=Y0; pz[2]=Z0;  px[3]=X0; py[3]=Y0; pz[3]=Z1;
        for (int i = 0; i < 4; i++) { pu[i] = 1 - pz[i] / 16.0f; pv[i] = 1 - py[i] / 16.0f; }
        break;
    case 4:  /* +Z */
        px[0]=X1; py[0]=Y1; pz[0]=Z1;  px[1]=X0; py[1]=Y1; pz[1]=Z1;
        px[2]=X0; py[2]=Y0; pz[2]=Z1;  px[3]=X1; py[3]=Y0; pz[3]=Z1;
        for (int i = 0; i < 4; i++) { pu[i] = 1 - px[i] / 16.0f; pv[i] = 1 - py[i] / 16.0f; }
        break;
    default: /* -Z */
        px[0]=X0; py[0]=Y1; pz[0]=Z0;  px[1]=X1; py[1]=Y1; pz[1]=Z0;
        px[2]=X1; py[2]=Y0; pz[2]=Z0;  px[3]=X0; py[3]=Y0; pz[3]=Z0;
        for (int i = 0; i < 4; i++) { pu[i] = px[i] / 16.0f; pv[i] = 1 - py[i] / 16.0f; }
        break;
    }
    /* model space is centred on the block, in block units */
    *x = px[corner] / 16.0f - 0.5f;
    *y = py[corner] / 16.0f - 0.5f;
    *z = pz[corner] / 16.0f - 0.5f;
    *u = pu[corner];
    *v = pv[corner];
}

/* The orientation an item is displayed at: blocks turned to show three faces,
   flat item sprites facing the viewer. */
static void item_display_pose(int item, Pose *p) {
    p->tx = p->ty = p->tz = 0;
    p->scale = 1.0f;
    if (item_def(item)->model >= MODEL_FIRST_ITEM) {
        p->rx = 0; p->ry = 0; p->rz = 0;
    } else {
        p->rx = -30.0f; p->ry = 45.0f; p->rz = 0.0f;
    }
}

/* ----------------------------------------------------------------------- */
/* Orthographic draw: inventory icons and the held item                     */
/* ----------------------------------------------------------------------- */

#define ICON_MAX 128
static float g_icon_depth[ICON_MAX * ICON_MAX];

/* Draws `item` into a size x size box whose top-left corner is (px,py).
   `pose` is applied on top of the item's display orientation. */
static void draw_item_ortho(uint32_t *dst, int dw, int dh, int px, int py,
                            int size, int item, const Pose *pose,
                            float phase, float bright) {
    if (size > ICON_MAX) size = ICON_MAX;
    if (size < 2) return;
    for (int i = 0; i < size * size; i++) g_icon_depth[i] = 1e30f;

    Pose disp;
    item_display_pose(item, &disp);
    Pose p = disp;
    if (pose) {
        p.rx += pose->rx; p.ry += pose->ry; p.rz += pose->rz;
        p.tx += pose->tx; p.ty += pose->ty; p.tz += pose->tz;
        p.scale = disp.scale * pose->scale;
    }
    Mat3 rot = pose_matrix(&p);

    Target t;
    t.color = dst; t.w = dw; t.h = dh;
    t.depth = g_icon_depth; t.dstride = size; t.dx0 = px; t.dy0 = py;
    t.cx0 = px < 0 ? 0 : px;
    t.cy0 = py < 0 ? 0 : py;
    t.cx1 = px + size > dw ? dw : px + size;
    t.cy1 = py + size > dh ? dh : py + size;
    if (t.cx0 >= t.cx1 || t.cy0 >= t.cy1) return;

    float half = size * 0.5f;
    float s = size * 0.58f * p.scale;      /* a block just fits the slot */
    float ccx = px + half + p.tx * size, ccy = py + half - p.ty * size;

    const ModelDef *m = item_model(item);
    for (int bi = 0; bi < m->n; bi++)
        for (int f = 0; f < 6; f++) {
            RVert v[4];
            for (int c = 0; c < 4; c++) {
                float mx, my, mz, u, uv;
                box_face(&m->box[bi], f, c, &mx, &my, &mz, &u, &uv);
                float rx, ry, rz;
                mat3_apply(rot, mx, my, mz, &rx, &ry, &rz);
                v[c].x = ccx + rx * s;
                v[c].y = ccy - ry * s;
                v[c].z = -rz + p.tz;
                v[c].u = u; v[c].v = uv;
            }
            float shade = g_face_shade[f] * bright;
            int face = g_face_tex[f];
            raster_tri(&t, v[0], v[1], v[2], item, face, phase, shade);
            raster_tri(&t, v[0], v[2], v[3], item, face, phase, shade);
        }
}

/* ----------------------------------------------------------------------- */
/* Perspective draw: items lying in the world                               */
/* ----------------------------------------------------------------------- */

typedef struct {
    float ox, oy, oz;                   /* eye                              */
    float fx, fy, fz;                   /* forward                          */
    float rx, ry, rz;                   /* right                            */
    float ux, uy, uz;                   /* up                               */
    float su0, dsu, tan_v;              /* screen mapping                   */
    int   w, h;
} CamView;

/* Draws an item at a world position, depth-tested against `depth` (the
   raycaster's per-pixel distance). Returns 1 if any pixel was written. */
static int draw_item_world(uint32_t *dst, float *depth, const CamView *cam,
                           int item, const Pose *pose, float wx, float wy,
                           float wz, float scale, float phase, float bright) {
    float relx = wx - cam->ox, rely = wy - cam->oy, relz = wz - cam->oz;
    float vz = relx * cam->fx + rely * cam->fy + relz * cam->fz;
    if (vz < 0.25f) return 0;                     /* behind the eye         */

    Pose disp;
    item_display_pose(item, &disp);
    Pose p = disp;
    if (pose) {
        p.rx += pose->rx; p.ry += pose->ry; p.rz += pose->rz;
        p.scale = disp.scale * pose->scale;
        wy += pose->ty;
    }
    Mat3 rot = pose_matrix(&p);

    Target t;
    t.color = dst; t.w = cam->w; t.h = cam->h;
    t.depth = depth; t.dstride = cam->w; t.dx0 = 0; t.dy0 = 0;
    t.cx0 = 0; t.cy0 = 0; t.cx1 = cam->w; t.cy1 = cam->h;

    const ModelDef *m = item_model(item);
    int drew = 0;
    for (int bi = 0; bi < m->n; bi++)
        for (int f = 0; f < 6; f++) {
            RVert v[4];
            int ok = 1;
            for (int c = 0; c < 4; c++) {
                float mx, my, mz, u, uv;
                box_face(&m->box[bi], f, c, &mx, &my, &mz, &u, &uv);
                float rx, ry, rz;
                mat3_apply(rot, mx * scale, my * scale, mz * scale,
                           &rx, &ry, &rz);
                float px = wx + rx - cam->ox;
                float py = wy + ry - cam->oy;
                float pz = wz + rz - cam->oz;
                float cz = px * cam->fx + py * cam->fy + pz * cam->fz;
                if (cz < 0.15f) { ok = 0; break; }
                float cxv = px * cam->rx + py * cam->ry + pz * cam->rz;
                float cyv = px * cam->ux + py * cam->uy + pz * cam->uz;
                float su = cxv / cz, sv = cyv / cz;
                v[c].x = (su - cam->su0) / cam->dsu - 0.5f;
                v[c].y = (1.0f - sv / cam->tan_v) * cam->h * 0.5f - 0.5f;
                v[c].z = cz;
                v[c].u = u; v[c].v = uv;
            }
            if (!ok) continue;
            float shade = g_face_shade[f] * bright;
            int face = g_face_tex[f];
            raster_tri(&t, v[0], v[1], v[2], item, face, phase, shade);
            raster_tri(&t, v[0], v[2], v[3], item, face, phase, shade);
            drew = 1;
        }
    return drew;
}

#endif /* RENDER3D_H */
