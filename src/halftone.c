/*
 * halftone.c - 8-bit grayscale to 1-bit conversion
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * The screen is generated at run time: a 45 degree round-dot screen with
 * two dots per N x N tile (N = 8 at 600 dpi, 16 at 1200 dpi, i.e. about
 * 106 lines per inch in both cases), which reproduces well on a laser
 * engine. Error diffusion is offered for photographs.
 */
#include "halftone.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct halftoner {
    ht_mode_t mode;
    uint32_t width;
    uint8_t curve[256];      /* tone correction                  */
    int n;                   /* screen tile size                 */
    uint8_t *thr;            /* n*n thresholds                   */
    int *err;                /* diffusion: 2 lines of errors     */
};

typedef struct {
    double spot;
    int idx;
    int dot;
} cell_t;

static int cell_cmp(const void *a, const void *b)
{
    const cell_t *p = a, *q = b;
    if (p->spot > q->spot) return -1;
    if (p->spot < q->spot) return 1;
    if (p->dot != q->dot) return p->dot - q->dot;
    return p->idx - q->idx;
}

static int build_screen(halftoner_t *h, int dpi)
{
    int n = dpi >= 1200 ? 16 : 8;
    int nn = n * n;
    cell_t *c = malloc(sizeof(cell_t) * (size_t)nn);

    h->thr = malloc((size_t)nn);
    if (!c || !h->thr) {
        free(c);
        return -1;
    }
    h->n = n;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            /* rotated lattice coordinates: dots sit on integer (u, w) */
            double u = ((x + 0.5) - (y + 0.5)) / n;
            double w = ((x + 0.5) + (y + 0.5)) / n;
            double fu = u - floor(u + 0.5);
            double fw = w - floor(w + 0.5);
            cell_t *e = &c[y * n + x];
            e->spot = cos(2 * M_PI * fu) + cos(2 * M_PI * fw);
            e->idx = y * n + x;
            /* which of the two dots of the tile this pixel belongs to */
            e->dot = ((int)floor(u + 0.5) + (int)floor(w + 0.5)) & 1;
        }
    }
    qsort(c, (size_t)nn, sizeof(cell_t), cell_cmp);
    for (int r = 0; r < nn; r++) {
        /* black when value > threshold; 0 never prints, 255 always does */
        int t = (int)floor((r + 0.5) * 255.0 / nn);
        if (t > 254) t = 254;
        h->thr[c[r].idx] = (uint8_t)t;
    }
    free(c);
    return 0;
}

halftoner_t *ht_new(ht_mode_t mode, int xdpi, int ydpi, uint32_t width,
                    double gamma)
{
    halftoner_t *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    h->mode = mode;
    h->width = width;
    if (gamma <= 0)
        gamma = 1.0;
    for (int i = 0; i < 256; i++) {
        double v = pow(i / 255.0, gamma) * 255.0 + 0.5;
        h->curve[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    h->curve[0] = 0;
    h->curve[255] = 255;

    if (mode == HT_SCREEN) {
        if (build_screen(h, xdpi < ydpi ? xdpi : ydpi) < 0) {
            ht_free(h);
            return NULL;
        }
    } else if (mode == HT_DIFFUSION) {
        h->err = calloc(2 * ((size_t)width + 2), sizeof(int));
        if (!h->err) {
            ht_free(h);
            return NULL;
        }
    }
    return h;
}

void ht_free(halftoner_t *h)
{
    if (!h)
        return;
    free(h->thr);
    free(h->err);
    free(h);
}

static void line_screen(halftoner_t *h, const uint8_t *k, uint32_t y,
                        uint8_t *out)
{
    const uint8_t *row = h->thr + (y % (uint32_t)h->n) * (uint32_t)h->n;
    uint32_t mask = (uint32_t)h->n - 1;   /* n is a power of two */

    for (uint32_t x = 0; x < h->width; x++) {
        uint8_t v = k[x];
        if (!v)
            continue;
        if (v == 255 || h->curve[v] > row[x & mask])
            out[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
    }
}

static void line_threshold(halftoner_t *h, const uint8_t *k, uint8_t *out)
{
    for (uint32_t x = 0; x < h->width; x++)
        if (k[x] >= 128)
            out[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
}

static void line_diffusion(halftoner_t *h, const uint8_t *k, uint32_t y,
                           uint8_t *out)
{
    uint32_t w = h->width;
    int *cur = h->err + 1 + (y & 1) * (w + 2);
    int *nxt = h->err + 1 + ((y + 1) & 1) * (w + 2);
    int dir = (y & 1) ? -1 : 1;           /* serpentine scan */
    long x = dir > 0 ? 0 : (long)w - 1;

    memset(nxt - 1, 0, (w + 2) * sizeof(int));
    for (uint32_t i = 0; i < w; i++, x += dir) {
        int v = k[x];
        int want, e;
        if (v == 0 || v == 255) {         /* keep solid areas crisp */
            if (v)
                out[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
            continue;
        }
        want = h->curve[v] * 16 + cur[x];
        if (want >= 128 * 16) {
            out[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
            e = want - 255 * 16;
        } else {
            e = want;
        }
        cur[x + dir] += e * 7 / 16;
        nxt[x - dir] += e * 3 / 16;
        nxt[x]       += e * 5 / 16;
        nxt[x + dir] += e * 1 / 16;
    }
}

void ht_line(halftoner_t *h, const uint8_t *k, uint32_t y, uint8_t *out)
{
    switch (h->mode) {
    case HT_DIFFUSION: line_diffusion(h, k, y, out); break;
    case HT_THRESHOLD: line_threshold(h, k, out); break;
    default:           line_screen(h, k, y, out); break;
    }
}
