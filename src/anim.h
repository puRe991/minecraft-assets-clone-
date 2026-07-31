/* Keyframe animation.
 *
 * A clip is a set of channels (three translations, three rotations, one
 * scale); a channel is a list of (time, value) keys. Sampling a clip at a
 * time produces a Pose, which the model renderer applies before drawing an
 * item's boxes. Clips are plain data, so adding a new one is a table entry
 * rather than code.
 *
 * Every item names an idle clip (played continuously while it is in hand or
 * lying on the ground) and a use clip (played once when you swing, eat, drink
 * or place it). The mapping lives in the generated item table.
 */
#ifndef ANIM_H
#define ANIM_H

#include <math.h>
#include <stdint.h>

#include "items.h"

enum { CH_TX, CH_TY, CH_TZ, CH_RX, CH_RY, CH_RZ, CH_SCALE, CH_COUNT };

typedef struct { float t, v; } Key;
typedef struct { const Key *key; uint8_t n; } Channel;

typedef struct {
    const char *name;
    float       len;         /* seconds */
    uint8_t     loop;
    Channel     ch[CH_COUNT];
} Clip;

typedef struct {
    float tx, ty, tz;        /* translation, in model units (1 = one block) */
    float rx, ry, rz;        /* rotation, degrees                          */
    float scale;
} Pose;

/* ----------------------------------------------------------------------- */
/* Clip data                                                                */
/* ----------------------------------------------------------------------- */

#define KEYS(name, ...) static const Key name[] = { __VA_ARGS__ }
#define CH(a) { a, (uint8_t)(sizeof(a) / sizeof(Key)) }

/* idle: a slow figure-of-eight, so a held item is never quite still */
KEYS(k_bob_ty, {0.0f, 0.0f}, {0.6f, 0.018f}, {1.2f, 0.0f}, {1.8f, -0.018f}, {2.4f, 0.0f});
KEYS(k_bob_rz, {0.0f, 0.0f}, {1.2f, 2.5f},   {2.4f, 0.0f});

/* plants lean in the breeze */
KEYS(k_sway_rz, {0.0f, -5.0f}, {1.6f, 5.0f}, {3.2f, -5.0f});
KEYS(k_sway_tx, {0.0f, -0.01f}, {1.6f, 0.01f}, {3.2f, -0.01f});

/* dropped items turn and hover */
KEYS(k_spin_ry, {0.0f, 0.0f}, {2.0f, 360.0f});
KEYS(k_spin_ty, {0.0f, 0.0f}, {0.5f, 0.06f}, {1.0f, 0.0f}, {1.5f, -0.06f}, {2.0f, 0.0f});

/* swinging a tool: wind up, strike, recover */
KEYS(k_swing_rx, {0.0f, 0.0f}, {0.08f, 22.0f}, {0.18f, -62.0f}, {0.30f, 0.0f});
KEYS(k_swing_tz, {0.0f, 0.0f}, {0.18f, -0.22f}, {0.30f, 0.0f});
KEYS(k_swing_ty, {0.0f, 0.0f}, {0.08f, 0.05f}, {0.18f, -0.10f}, {0.30f, 0.0f});

/* thrusting a spear */
KEYS(k_stab_tz, {0.0f, 0.0f}, {0.10f, 0.34f}, {0.16f, 0.34f}, {0.34f, 0.0f});
KEYS(k_stab_rx, {0.0f, 0.0f}, {0.10f, -12.0f}, {0.34f, 0.0f});

/* raising a bottle to drink */
KEYS(k_drink_rz, {0.0f, 0.0f}, {0.25f, -48.0f}, {0.70f, -52.0f}, {0.90f, 0.0f});
KEYS(k_drink_tx, {0.0f, 0.0f}, {0.25f, -0.12f}, {0.70f, -0.12f}, {0.90f, 0.0f});
KEYS(k_drink_ty, {0.0f, 0.0f}, {0.25f, 0.08f}, {0.70f, 0.10f}, {0.90f, 0.0f});

/* eating: small repeated bites */
KEYS(k_eat_tx, {0.0f, 0.0f}, {0.20f, -0.10f}, {0.35f, -0.06f}, {0.50f, -0.10f},
               {0.65f, -0.06f}, {0.80f, 0.0f});
KEYS(k_eat_rz, {0.0f, 0.0f}, {0.20f, -30.0f}, {0.80f, 0.0f});

/* drawing a bow */
KEYS(k_shoot_tz, {0.0f, 0.0f}, {0.45f, -0.18f}, {0.52f, 0.10f}, {0.60f, 0.0f});
KEYS(k_shoot_ry, {0.0f, 0.0f}, {0.45f, -18.0f}, {0.60f, 0.0f});

/* placing a block: a short shove forward */
KEYS(k_place_tz, {0.0f, 0.0f}, {0.10f, 0.20f}, {0.25f, 0.0f});
KEYS(k_place_ty, {0.0f, 0.0f}, {0.10f, -0.06f}, {0.25f, 0.0f});

/* opening a door / book / chest */
KEYS(k_open_ry, {0.0f, 0.0f}, {0.40f, 78.0f});
KEYS(k_open_tx, {0.0f, 0.0f}, {0.40f, -0.06f});

/* flames and lamps breathe */
KEYS(k_flicker_sc, {0.0f, 1.0f}, {0.18f, 1.05f}, {0.32f, 0.97f}, {0.55f, 1.03f},
                   {0.80f, 1.0f});
KEYS(k_pulse_sc, {0.0f, 1.0f}, {1.0f, 1.06f}, {2.0f, 1.0f});

static const Clip g_clips[ANIM_COUNT] = {
    [ANIM_NONE]    = { "none",    1.0f, 1, {{0}} },
    [ANIM_BOB]     = { "bob",     2.4f, 1, { [CH_TY] = CH(k_bob_ty),
                                             [CH_RZ] = CH(k_bob_rz) } },
    [ANIM_SWAY]    = { "sway",    3.2f, 1, { [CH_TX] = CH(k_sway_tx),
                                             [CH_RZ] = CH(k_sway_rz) } },
    [ANIM_SPIN]    = { "spin",    2.0f, 1, { [CH_TY] = CH(k_spin_ty),
                                             [CH_RY] = CH(k_spin_ry) } },
    [ANIM_SWING]   = { "swing",   0.30f, 0, { [CH_TY] = CH(k_swing_ty),
                                              [CH_TZ] = CH(k_swing_tz),
                                              [CH_RX] = CH(k_swing_rx) } },
    [ANIM_STAB]    = { "stab",    0.34f, 0, { [CH_TZ] = CH(k_stab_tz),
                                              [CH_RX] = CH(k_stab_rx) } },
    [ANIM_DRINK]   = { "drink",   0.90f, 0, { [CH_TX] = CH(k_drink_tx),
                                              [CH_TY] = CH(k_drink_ty),
                                              [CH_RZ] = CH(k_drink_rz) } },
    [ANIM_EAT]     = { "eat",     0.80f, 0, { [CH_TX] = CH(k_eat_tx),
                                              [CH_RZ] = CH(k_eat_rz) } },
    [ANIM_SHOOT]   = { "shoot",   0.60f, 0, { [CH_TZ] = CH(k_shoot_tz),
                                              [CH_RY] = CH(k_shoot_ry) } },
    [ANIM_PLACE]   = { "place",   0.25f, 0, { [CH_TY] = CH(k_place_ty),
                                              [CH_TZ] = CH(k_place_tz) } },
    [ANIM_OPEN]    = { "open",    0.40f, 0, { [CH_TX] = CH(k_open_tx),
                                              [CH_RY] = CH(k_open_ry) } },
    [ANIM_FLICKER] = { "flicker", 0.80f, 1, { [CH_SCALE] = CH(k_flicker_sc) } },
    [ANIM_PULSE]   = { "pulse",   2.0f, 1, { [CH_SCALE] = CH(k_pulse_sc) } },
};

/* ----------------------------------------------------------------------- */
/* Sampling                                                                 */
/* ----------------------------------------------------------------------- */

static float channel_value(const Channel *c, float t, float dflt) {
    if (!c->n) return dflt;
    if (t <= c->key[0].t) return c->key[0].v;
    for (int i = 1; i < c->n; i++) {
        if (t <= c->key[i].t) {
            float t0 = c->key[i - 1].t, t1 = c->key[i].t;
            float f = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
            f = f * f * (3.0f - 2.0f * f);      /* smoothstep between keys */
            return c->key[i - 1].v + (c->key[i].v - c->key[i - 1].v) * f;
        }
    }
    return c->key[c->n - 1].v;
}

/* Samples `clip` at time t (seconds since it started). Looping clips wrap;
   one-shot clips hold their last key. */
static void anim_pose(int clip_id, float t, Pose *p) {
    const Clip *c = &g_clips[(unsigned)clip_id < ANIM_COUNT ? clip_id : 0];
    if (c->loop && c->len > 0.0f) {
        t = fmodf(t, c->len);
        if (t < 0) t += c->len;
    } else if (t > c->len) {
        t = c->len;
    }
    p->tx = channel_value(&c->ch[CH_TX], t, 0.0f);
    p->ty = channel_value(&c->ch[CH_TY], t, 0.0f);
    p->tz = channel_value(&c->ch[CH_TZ], t, 0.0f);
    p->rx = channel_value(&c->ch[CH_RX], t, 0.0f);
    p->ry = channel_value(&c->ch[CH_RY], t, 0.0f);
    p->rz = channel_value(&c->ch[CH_RZ], t, 0.0f);
    p->scale = channel_value(&c->ch[CH_SCALE], t, 1.0f);
}

/* Adds `b` on top of `a` -- an idle clip plus whatever one-shot is playing. */
static void anim_blend(const Pose *a, const Pose *b, Pose *out) {
    out->tx = a->tx + b->tx;   out->ty = a->ty + b->ty;
    out->tz = a->tz + b->tz;   out->rx = a->rx + b->rx;
    out->ry = a->ry + b->ry;   out->rz = a->rz + b->rz;
    out->scale = a->scale * b->scale;
}

static inline float anim_length(int clip_id) {
    return g_clips[(unsigned)clip_id < ANIM_COUNT ? clip_id : 0].len;
}
static inline int anim_loops(int clip_id) {
    return g_clips[(unsigned)clip_id < ANIM_COUNT ? clip_id : 0].loop;
}

#endif /* ANIM_H */
