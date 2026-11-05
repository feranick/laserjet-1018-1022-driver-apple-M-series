/*
 * jbig_enc.c - Minimal JBIG1 (ITU-T T.82) encoder for ZjStream printers.
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * Written from the ITU-T T.82 specification. Only the subset of the
 * standard required by the HP ZjStream printers is implemented (see
 * jbig_enc.h). Output has been verified to be byte-identical to the JBIG
 * data produced by HP's own (x86_64) Mac filter for the same bitmaps.
 */
#include "jbig_enc.h"

#include <stdlib.h>
#include <string.h>

/* ---- T.82 Table 24: probability estimation state machine ---------------- */

static const uint16_t QE[113] = {
    0x5a1d, 0x2586, 0x1114, 0x080b, 0x03d8, 0x01da, 0x00e5, 0x006f,
    0x0036, 0x001a, 0x000d, 0x0006, 0x0003, 0x0001, 0x5a7f, 0x3f25,
    0x2cf2, 0x207c, 0x17b9, 0x1182, 0x0cef, 0x09a1, 0x072f, 0x055c,
    0x0406, 0x0303, 0x0240, 0x01b1, 0x0144, 0x00f5, 0x00b7, 0x008a,
    0x0068, 0x004e, 0x003b, 0x002c, 0x5ae1, 0x484c, 0x3a0d, 0x2ef1,
    0x261f, 0x1f33, 0x19a8, 0x1518, 0x1177, 0x0e74, 0x0bfb, 0x09f8,
    0x0861, 0x0706, 0x05cd, 0x04de, 0x040f, 0x0363, 0x02d4, 0x025c,
    0x01f8, 0x01a4, 0x0160, 0x0125, 0x00f6, 0x00cb, 0x00ab, 0x008f,
    0x5b12, 0x4d04, 0x412c, 0x37d8, 0x2fe8, 0x293c, 0x2379, 0x1edf,
    0x1aa9, 0x174e, 0x1424, 0x119c, 0x0f6b, 0x0d51, 0x0bb6, 0x0a40,
    0x5832, 0x4d1c, 0x438e, 0x3bdd, 0x34ee, 0x2eae, 0x299a, 0x2516,
    0x5570, 0x4ca9, 0x44d9, 0x3e22, 0x3824, 0x32b4, 0x2e17, 0x56a8,
    0x4f46, 0x47e5, 0x41cf, 0x3c3d, 0x375e, 0x5231, 0x4c0f, 0x4639,
    0x415e, 0x5627, 0x50e7, 0x4b85, 0x5597, 0x504f, 0x5a10, 0x5522,
    0x59eb
};

static const uint8_t NMPS[113] = {
      1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  13,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,
     29,  30,  31,  32,  33,  34,  35,   9,  37,  38,  39,  40,  41,  42,
     43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,
     57,  58,  59,  60,  61,  62,  63,  32,  65,  66,  67,  68,  69,  70,
     71,  72,  73,  74,  75,  76,  77,  78,  79,  48,  81,  82,  83,  84,
     85,  86,  87,  71,  89,  90,  91,  92,  93,  94,  86,  96,  97,  98,
     99, 100,  93, 102, 103, 104,  99, 106, 107, 103, 109, 107, 111, 109,
    111
};

static const uint8_t NLPS[113] = {
      1,  14,  16,  18,  20,  23,  25,  28,  30,  33,  35,   9,  10,  12,
     15,  36,  38,  39,  40,  42,  43,  45,  46,  48,  49,  51,  52,  54,
     56,  57,  59,  60,  62,  63,  32,  33,  37,  64,  65,  67,  68,  69,
     70,  72,  73,  74,  75,  77,  78,  79,  48,  50,  50,  51,  52,  53,
     54,  55,  56,  57,  58,  59,  61,  61,  65,  80,  81,  82,  83,  84,
     86,  87,  87,  72,  72,  74,  74,  75,  77,  77,  80,  88,  89,  90,
     91,  92,  93,  86,  88,  95,  96,  97,  99,  99,  93,  95, 101, 102,
    103, 104,  99, 105, 106, 107, 103, 105, 108, 109, 110, 111, 110, 112,
    112
};

/* States in which an LPS exchanges the meaning of MPS/LPS */
static int qm_switch(int st)
{
    return st == 0 || st == 14 || st == 36 || st == 64 || st == 80 ||
           st == 88 || st == 95 || st == 105 || st == 110 || st == 112;
}

/* ---- output buffer ------------------------------------------------------ */

#define ESC   0xff
#define STUFF 0x00
#define SDNORM 0x02

void jbig_buf_free(jbig_buf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

typedef struct {
    jbig_buf_t *out;
    int err;
} sink_t;

static void put(sink_t *s, unsigned char c)
{
    jbig_buf_t *b = s->out;
    if (b->len == b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 65536;
        unsigned char *n = realloc(b->data, ncap);
        if (!n) {
            s->err = 1;
            return;
        }
        b->data = n;
        b->cap = ncap;
    }
    b->data[b->len++] = c;
}

/* PSCD byte: an ESC byte inside coded data must be followed by STUFF */
static void put_pscd(sink_t *s, unsigned char c)
{
    put(s, c);
    if (c == ESC)
        put(s, STUFF);
}

/* ---- QM arithmetic encoder (T.82 section 6.8) --------------------------- */

typedef struct {
    uint32_t c;        /* code register                     */
    uint32_t a;        /* interval register                 */
    int ct;            /* bits until next byte out          */
    long sc;           /* stacked 0xff bytes                */
    int buffer;        /* byte not yet written (-1 = none)  */
    uint8_t st[1024];  /* context states: bit7 = MPS, 0..6 = state index */
    sink_t *sink;
} qm_t;

static void qm_init(qm_t *q, sink_t *sink, int reset_states)
{
    if (reset_states)
        memset(q->st, 0, sizeof(q->st));
    q->c = 0;
    q->a = 0x10000;
    q->ct = 11;
    q->sc = 0;
    q->buffer = -1;
    q->sink = sink;
}

static void qm_byteout(qm_t *q)
{
    uint32_t temp = q->c >> 19;

    if (temp > 0xff) {
        /* carry propagates into the buffered byte */
        if (q->buffer >= 0)
            put_pscd(q->sink, (unsigned char)(q->buffer + 1));
        for (; q->sc; q->sc--)
            put(q->sink, 0x00);
        q->buffer = (int)(temp & 0xff);
    } else if (temp == 0xff) {
        q->sc++;
    } else {
        if (q->buffer >= 0)
            put_pscd(q->sink, (unsigned char)q->buffer);
        for (; q->sc; q->sc--) {
            put(q->sink, 0xff);
            put(q->sink, STUFF);
        }
        q->buffer = (int)temp;
    }
    q->c &= 0x7ffff;
    q->ct = 8;
}

static void qm_encode(qm_t *q, int cx, int pix)
{
    uint8_t *st = &q->st[cx];
    int idx = *st & 0x7f;
    int mps = *st >> 7;
    uint32_t qe = QE[idx];

    q->a -= qe;
    if (pix == mps) {
        if (q->a >= 0x8000)
            return;                      /* no renormalisation needed */
        if (q->a < qe) {                 /* conditional exchange */
            q->c += q->a;
            q->a = qe;
        }
        *st = (uint8_t)((mps << 7) | NMPS[idx]);
    } else {
        if (q->a >= qe) {                /* conditional exchange */
            q->c += q->a;
            q->a = qe;
        }
        if (qm_switch(idx))
            mps ^= 1;
        *st = (uint8_t)((mps << 7) | NLPS[idx]);
    }
    do {                                 /* RENORME */
        q->a <<= 1;
        q->c <<= 1;
        if (--q->ct == 0)
            qm_byteout(q);
    } while (q->a < 0x8000);
}

static void qm_flush(qm_t *q)
{
    /* CLEARBITS: pick the value in [C, C+A) with most trailing zeros */
    uint32_t temp = (q->a - 1 + q->c) & 0xffff0000u;
    q->c = (temp < q->c) ? temp + 0x8000 : temp;

    /* FINALWRITES */
    q->c <<= q->ct;
    if (q->c & 0xf8000000u) {
        if (q->buffer >= 0)
            put_pscd(q->sink, (unsigned char)(q->buffer + 1));
        if (q->c & 0x7fff800u)          /* zeros only if more data follows */
            for (; q->sc; q->sc--)
                put(q->sink, 0x00);
    } else {
        if (q->buffer >= 0)
            put_pscd(q->sink, (unsigned char)q->buffer);
        for (; q->sc; q->sc--) {
            put(q->sink, 0xff);
            put(q->sink, STUFF);
        }
    }
    /* trailing 0x00 bytes may be omitted (decoder assumes zeros) */
    if (q->c & 0x7fff800u) {
        put_pscd(q->sink, (unsigned char)((q->c >> 19) & 0xff));
        if (q->c & 0x7f800u)
            put_pscd(q->sink, (unsigned char)((q->c >> 11) & 0xff));
    }
}

/* ---- image coding ------------------------------------------------------- */

/* Context of the pseudo-pixel SLNTP for the two-line template (T.82) */
#define TPB2CX 0x195

static inline int px(const unsigned char *row, long x, uint32_t width)
{
    if (!row || x < 0 || (uint32_t)x >= width)
        return 0;
    return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

static int line_equal(const unsigned char *a, const unsigned char *b,
                      uint32_t width)
{
    size_t nbytes = width >> 3;
    unsigned rem = width & 7;

    if (!a) {                            /* compare against white line */
        for (size_t i = 0; i < nbytes; i++)
            if (b[i])
                return 0;
        if (rem && (b[nbytes] & (0xff00 >> rem) & 0xff))
            return 0;
        return 1;
    }
    if (memcmp(a, b, nbytes))
        return 0;
    if (rem && ((a[nbytes] ^ b[nbytes]) & (0xff00 >> rem) & 0xff))
        return 0;
    return 1;
}

static void put_be32(sink_t *s, uint32_t v)
{
    put(s, (unsigned char)(v >> 24));
    put(s, (unsigned char)(v >> 16));
    put(s, (unsigned char)(v >> 8));
    put(s, (unsigned char)v);
}

int jbig_encode(const unsigned char *bitmap, size_t stride,
                uint32_t width, uint32_t height, uint32_t l0,
                jbig_buf_t *out)
{
    sink_t sink = { out, 0 };
    qm_t *q;
    uint32_t y = 0;

    if (l0 == 0)
        l0 = 128;

    /* Bi-level Image Header (BIH), 20 bytes */
    put(&sink, 0);          /* DL */
    put(&sink, 0);          /* D  */
    put(&sink, 1);          /* P  */
    put(&sink, 0);          /* reserved */
    put_be32(&sink, width);
    put_be32(&sink, height);
    put_be32(&sink, l0);
    put(&sink, 0);          /* MX */
    put(&sink, 0);          /* MY */
    put(&sink, 0x03);       /* order: ILEAVE | SMID */
    put(&sink, 0x5c);       /* options: LRLTWO | TPDON | TPBON | DPON */

    q = malloc(sizeof(*q));
    if (!q)
        return -1;
    memset(q->st, 0, sizeof(q->st));

    /* "previous line was typical" flag; before the first line LNTP = 1.
     * It is carried across stripe boundaries, as is the context state. */
    int ltp_old = 0;

    while (y < height) {                /* one iteration per stripe */
        uint32_t yend = y + l0 < height ? y + l0 : height;

        qm_init(q, &sink, 0);   /* SDNORM: states carry over */

        for (; y < yend; y++) {
            const unsigned char *cur = bitmap + (size_t)y * stride;
            const unsigned char *up = y ? cur - stride : NULL;
            int ltp = line_equal(up, cur, width);

            qm_encode(q, TPB2CX, ltp == ltp_old);
            ltp_old = ltp;
            if (ltp)
                continue;           /* typical line: nothing more to code */

            /* two-line template, A pixel at default position (x-4, y):
             *   y-1:  x-3 x-2 x-1 x x+1 x+2   -> bits 9..4
             *   y  :  x-4 x-3 x-2 x-1         -> bits 3..0           */
            unsigned hu = (unsigned)((px(up, 0, width) << 2) |
                                     (px(up, 1, width) << 1) |
                                      px(up, 2, width));
            unsigned hc = 0;
            for (uint32_t x = 0; x < width; x++) {
                int pix = px(cur, x, width);
                qm_encode(q, (int)((hu << 4) | hc), pix);
                hu = ((hu << 1) & 0x3f) | (unsigned)px(up, (long)x + 3, width);
                hc = ((hc << 1) & 0x0f) | (unsigned)pix;
            }
        }
        qm_flush(q);
        put(&sink, ESC);
        put(&sink, SDNORM);
    }
    free(q);
    return sink.err ? -1 : 0;
}
