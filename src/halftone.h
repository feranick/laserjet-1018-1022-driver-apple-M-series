/*
 * halftone.h - 8-bit grayscale to 1-bit conversion
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 */
#ifndef HALFTONE_H
#define HALFTONE_H

#include <stdint.h>

typedef enum {
    HT_SCREEN = 0,     /* 45 degree clustered-dot screen (~106 lpi) */
    HT_DIFFUSION = 1,  /* Floyd-Steinberg error diffusion           */
    HT_THRESHOLD = 2   /* plain 50% threshold (text / line art)     */
} ht_mode_t;

typedef struct halftoner halftoner_t;

/* 'gamma' > 1 lightens mid-tones to compensate for toner dot gain. */
halftoner_t *ht_new(ht_mode_t mode, int xdpi, int ydpi, uint32_t width,
                    double gamma);
void ht_free(halftoner_t *h);

/*
 * Convert one line. 'k' holds 'width' samples, 0 = white, 255 = black.
 * Bits are OR-ed into 'out' (MSB first, 1 = black); 'out' must be zeroed.
 */
void ht_line(halftoner_t *h, const uint8_t *k, uint32_t y, uint8_t *out);

#endif
