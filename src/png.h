/*
 * png.h - minimal, self-contained PNG codec (no external libraries).
 *
 *   png_load(path, &w, &h) -> malloc'd 0xAARRGGBB pixel buffer (or NULL)
 *   png_write(path, pixels, w, h) -> write an 8-bit RGBA PNG
 *
 * Decoder supports 8-bit PNGs, non-interlaced, colour types 0/2/3/4/6,
 * with a full DEFLATE inflate (stored + fixed + dynamic Huffman). The
 * inflate is a straightforward implementation of Mark Adler's "puff".
 * Encoder writes colour type 6 (RGBA) using stored DEFLATE blocks.
 */
#ifndef PNG_H
#define PNG_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>

/* ============================ inflate (puff) ============================ */

#define PUF_MAXBITS  15
#define PUF_MAXLCODES 286
#define PUF_MAXDCODES 30
#define PUF_MAXCODES (PUF_MAXLCODES + PUF_MAXDCODES)
#define PUF_FIXLCODES 288

struct puf_state {
    unsigned char *out; unsigned long outlen, outcnt;
    const unsigned char *in; unsigned long inlen, incnt;
    int bitbuf, bitcnt;
    jmp_buf env;
};
struct puf_huff { short *count; short *symbol; };

static int puf_bits(struct puf_state *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen) longjmp(s->env, 1);
        val |= (long)(s->in[s->incnt++]) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int puf_stored(struct puf_state *s) {
    unsigned len;
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return 2;
    len = s->in[s->incnt++];
    len |= s->in[s->incnt++] << 8;
    s->incnt += 2;                       /* skip nlen */
    if (s->incnt + len > s->inlen) return 2;
    while (len--) {
        if (s->outcnt < s->outlen) s->out[s->outcnt] = s->in[s->incnt];
        s->outcnt++; s->incnt++;
    }
    return 0;
}

static int puf_decode(struct puf_state *s, const struct puf_huff *h) {
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= PUF_MAXBITS; len++) {
        code |= puf_bits(s, 1);
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -10;
}

static int puf_construct(struct puf_huff *h, const short *length, int n) {
    int symbol, len, left; short offs[PUF_MAXBITS + 1];
    for (len = 0; len <= PUF_MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= PUF_MAXBITS; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (len = 1; len < PUF_MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++) if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = symbol;
    return left;
}

static int puf_codes(struct puf_state *s, const struct puf_huff *lc, const struct puf_huff *dc) {
    static const short lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    int symbol;
    do {
        symbol = puf_decode(s, lc);
        if (symbol < 0) return symbol;
        if (symbol < 256) { if (s->outcnt < s->outlen) s->out[s->outcnt] = (unsigned char)symbol; s->outcnt++; }
        else if (symbol > 256) {
            unsigned len, dist;
            symbol -= 257; if (symbol >= 29) return -10;
            len = lens[symbol] + puf_bits(s, lext[symbol]);
            symbol = puf_decode(s, dc); if (symbol < 0) return symbol;
            dist = dists[symbol] + puf_bits(s, dext[symbol]);
            if (dist > s->outcnt) return -11;
            while (len--) { if (s->outcnt < s->outlen) s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    } while (symbol != 256);
    return 0;
}

static int puf_fixed(struct puf_state *s) {
    static short lcnt[PUF_MAXBITS + 1], lsym[PUF_FIXLCODES], dcnt[PUF_MAXBITS + 1], dsym[PUF_MAXDCODES];
    static struct puf_huff lc, dc; static int virgin = 1;
    if (virgin) {
        int symbol; short lengths[PUF_FIXLCODES];
        lc.count = lcnt; lc.symbol = lsym; dc.count = dcnt; dc.symbol = dsym;
        for (symbol = 0; symbol < 144; symbol++) lengths[symbol] = 8;
        for (; symbol < 256; symbol++) lengths[symbol] = 9;
        for (; symbol < 280; symbol++) lengths[symbol] = 7;
        for (; symbol < PUF_FIXLCODES; symbol++) lengths[symbol] = 8;
        puf_construct(&lc, lengths, PUF_FIXLCODES);
        for (symbol = 0; symbol < PUF_MAXDCODES; symbol++) lengths[symbol] = 5;
        puf_construct(&dc, lengths, PUF_MAXDCODES);
        virgin = 0;
    }
    return puf_codes(s, &lc, &dc);
}

static int puf_dynamic(struct puf_state *s) {
    int nlen, ndist, ncode, index, err;
    short lengths[PUF_MAXCODES];
    short lcnt[PUF_MAXBITS + 1], lsym[PUF_MAXLCODES], dcnt[PUF_MAXBITS + 1], dsym[PUF_MAXDCODES];
    struct puf_huff lc = {lcnt, lsym}, dc = {dcnt, dsym};
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    nlen = puf_bits(s, 5) + 257; ndist = puf_bits(s, 5) + 1; ncode = puf_bits(s, 4) + 4;
    if (nlen > PUF_MAXLCODES || ndist > PUF_MAXDCODES) return -3;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)puf_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    err = puf_construct(&lc, lengths, 19); if (err != 0) return -4;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = puf_decode(s, &lc); if (symbol < 0) return symbol;
        if (symbol < 16) lengths[index++] = (short)symbol;
        else {
            int len = 0;
            if (symbol == 16) { if (index == 0) return -5; len = lengths[index - 1]; symbol = 3 + puf_bits(s, 2); }
            else if (symbol == 17) symbol = 3 + puf_bits(s, 3);
            else symbol = 11 + puf_bits(s, 7);
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -9;
    err = puf_construct(&lc, lengths, nlen);
    if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -7;
    err = puf_construct(&dc, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -8;
    return puf_codes(s, &lc, &dc);
}

/* inflate a raw DEFLATE stream */
static int puf_inflate(unsigned char *dest, unsigned long *destlen,
                       const unsigned char *src, unsigned long srclen) {
    struct puf_state s;
    int last, type, err;
    s.out = dest; s.outlen = *destlen; s.outcnt = 0;
    s.in = src; s.inlen = srclen; s.incnt = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    if (setjmp(s.env)) err = 2;
    else {
        do {
            last = puf_bits(&s, 1);
            type = puf_bits(&s, 2);
            err = type == 0 ? puf_stored(&s) :
                  type == 1 ? puf_fixed(&s) :
                  type == 2 ? puf_dynamic(&s) : -1;
            if (err != 0) break;
        } while (!last);
    }
    *destlen = s.outcnt;
    return err;
}

/* ============================== PNG decode ============================= */

static unsigned png_be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static int png_paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* Returns malloc'd 0xAARRGGBB buffer (w*h), or NULL on failure. */
static uint32_t *png_load(const char *path, int *out_w, int *out_h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz < 8) { fclose(f); return NULL; }
    unsigned char *file = (unsigned char *)malloc(fsz);
    if (!file) { fclose(f); return NULL; }
    if (fread(file, 1, fsz, f) != (size_t)fsz) { free(file); fclose(f); return NULL; }
    fclose(f);

    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(file, sig, 8) != 0) { free(file); return NULL; }

    int W = 0, H = 0, bitdepth = 0, colortype = 0, interlace = 0;
    unsigned char *idat = NULL; unsigned long idat_len = 0, idat_cap = 0;
    unsigned char plte[256 * 3]; int plte_n = 0;
    unsigned char trns[256]; int trns_n = 0;

    long pos = 8;
    while (pos + 8 <= fsz) {
        unsigned len = png_be32(file + pos);
        const unsigned char *type = file + pos + 4;
        const unsigned char *data = file + pos + 8;
        if (pos + 12 + (long)len > fsz) break;
        if (memcmp(type, "IHDR", 4) == 0) {
            W = (int)png_be32(data); H = (int)png_be32(data + 4);
            bitdepth = data[8]; colortype = data[9]; interlace = data[12];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            plte_n = len / 3; if (plte_n > 256) plte_n = 256;
            memcpy(plte, data, plte_n * 3);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            trns_n = len > 256 ? 256 : len; memcpy(trns, data, trns_n);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_len + len > idat_cap) {
                idat_cap = (idat_len + len) * 2 + 64;
                idat = (unsigned char *)realloc(idat, idat_cap);
            }
            memcpy(idat + idat_len, data, len); idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }

    if (W <= 0 || H <= 0 || bitdepth != 8 || interlace != 0 || !idat) {
        free(file); free(idat); return NULL;
    }
    int channels = colortype == 0 ? 1 : colortype == 2 ? 3 :
                   colortype == 3 ? 1 : colortype == 4 ? 2 : colortype == 6 ? 4 : 0;
    if (channels == 0) { free(file); free(idat); return NULL; }

    unsigned long raw_len = (unsigned long)H * (1 + (unsigned long)W * channels);
    unsigned char *raw = (unsigned char *)malloc(raw_len);
    unsigned long got = raw_len;
    /* skip 2-byte zlib header, ignore 4-byte trailing adler */
    int rc = puf_inflate(raw, &got, idat + 2, idat_len >= 2 ? idat_len - 2 : 0);
    free(idat);
    if (rc != 0 || got < raw_len) { free(file); free(raw); return NULL; }

    /* unfilter in place -> contiguous scanlines of W*channels */
    int bpp = channels;
    int stride = W * channels;
    unsigned char *img = (unsigned char *)malloc((size_t)stride * H);
    for (int y = 0; y < H; y++) {
        unsigned char filter = raw[y * (stride + 1)];
        unsigned char *src = raw + y * (stride + 1) + 1;
        unsigned char *cur = img + y * stride;
        unsigned char *prev = y ? img + (y - 1) * stride : NULL;
        for (int x = 0; x < stride; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            int v = src[x];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += png_paeth(a, b, c); break;
                default: break;
            }
            cur[x] = (unsigned char)v;
        }
    }
    free(raw);

    uint32_t *pix = (uint32_t *)malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            unsigned char *p = img + y * stride + x * channels;
            int r, g, b, a = 255;
            switch (colortype) {
                case 0: r = g = b = p[0]; break;
                case 2: r = p[0]; g = p[1]; b = p[2]; break;
                case 3: {
                    int idx = p[0];
                    r = plte[idx * 3]; g = plte[idx * 3 + 1]; b = plte[idx * 3 + 2];
                    if (idx < trns_n) a = trns[idx];
                    break;
                }
                case 4: r = g = b = p[0]; a = p[1]; break;
                default: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
            }
            pix[y * W + x] = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    free(img); free(file);
    *out_w = W; *out_h = H;
    return pix;
}

/* ============================== PNG encode ============================= */
#ifdef PNG_ENABLE_WRITE

static const uint32_t *png_crc_table(void) {
    static uint32_t t[256]; static int done = 0;
    if (!done) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        done = 1;
    }
    return t;
}
static uint32_t png_crc(const unsigned char *d, size_t n) {
    const uint32_t *t = png_crc_table();
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) c = t[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
static void png_put32(FILE *f, uint32_t v) {
    unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16),
                          (unsigned char)(v >> 8), (unsigned char)v};
    fwrite(b, 1, 4, f);
}
static void png_chunk(FILE *f, const char *type, const unsigned char *data, size_t len) {
    png_put32(f, (uint32_t)len);
    unsigned char *buf = (unsigned char *)malloc(len + 4);
    memcpy(buf, type, 4);
    if (len) memcpy(buf + 4, data, len);
    fwrite(buf, 1, len + 4, f);
    png_put32(f, png_crc(buf, len + 4));
    free(buf);
}

/* Write an 8-bit RGBA PNG from a 0xAARRGGBB buffer. Returns 0 on success. */
static int png_write(const char *path, const uint32_t *pix, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    unsigned char ihdr[13];
    ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
    ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);

    /* raw scanlines: filter byte 0 + RGBA */
    size_t stride = 1 + (size_t)w * 4;
    size_t raw_len = stride * h;
    unsigned char *raw = (unsigned char *)malloc(raw_len);
    for (int y = 0; y < h; y++) {
        raw[y * stride] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = pix[y * w + x];
            unsigned char *o = raw + y * stride + 1 + x * 4;
            o[0] = (c >> 16) & 0xff; o[1] = (c >> 8) & 0xff; o[2] = c & 0xff; o[3] = (c >> 24) & 0xff;
        }
    }

    /* zlib stream with stored DEFLATE blocks */
    size_t zcap = raw_len + (raw_len / 65535 + 1) * 5 + 16;
    unsigned char *z = (unsigned char *)malloc(zcap);
    size_t zn = 0;
    z[zn++] = 0x78; z[zn++] = 0x01;             /* zlib header */
    size_t off = 0;
    while (off < raw_len) {
        size_t block = raw_len - off; if (block > 65535) block = 65535;
        int last = (off + block >= raw_len);
        z[zn++] = last ? 1 : 0;
        z[zn++] = block & 0xff; z[zn++] = (block >> 8) & 0xff;
        z[zn++] = ~block & 0xff; z[zn++] = (~block >> 8) & 0xff;
        memcpy(z + zn, raw + off, block); zn += block; off += block;
    }
    /* adler32 of raw */
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    uint32_t adler = (b << 16) | a;
    z[zn++] = adler >> 24; z[zn++] = adler >> 16; z[zn++] = adler >> 8; z[zn++] = adler;

    png_chunk(f, "IDAT", z, zn);
    png_chunk(f, "IEND", NULL, 0);
    free(raw); free(z); fclose(f);
    return 0;
}

#endif /* PNG_ENABLE_WRITE */

#endif /* PNG_H */
