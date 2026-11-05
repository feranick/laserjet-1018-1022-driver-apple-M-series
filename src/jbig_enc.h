/*
 * jbig_enc.h - Minimal JBIG1 (ITU-T T.82) encoder for ZjStream printers.
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * Produces exactly the bi-level image entity (BIE) used by the HP LaserJet
 * 1018/1020/1022 ZjStream protocol:
 *   - one plane, one resolution layer (D = 0)
 *   - two-line template (LRLTWO), no adaptive template moves (MX = 0)
 *   - typical prediction on the base layer (TPBON)
 *   - L0 = 128 lines per stripe, stripes terminated with ESC SDNORM
 *   - BIH order = ILEAVE|SMID (0x03), options = 0x5c
 * These parameters were recovered from the original HP filter.
 */
#ifndef JBIG_ENC_H
#define JBIG_ENC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} jbig_buf_t;

void jbig_buf_free(jbig_buf_t *b);

/*
 * Encode a bi-level bitmap (1 = black, MSB first, rows of 'stride' bytes)
 * of 'width' x 'height' pixels. The complete BIE (20-byte BIH followed by
 * stripe data) is appended to 'out'. Returns 0 on success, -1 on ENOMEM.
 */
int jbig_encode(const unsigned char *bitmap, size_t stride,
                uint32_t width, uint32_t height, uint32_t l0,
                jbig_buf_t *out);

#endif
