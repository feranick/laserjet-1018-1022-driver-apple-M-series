/*
 * zjs.h - ZjStream protocol definitions for HP LaserJet 1018/1020/1022
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * Recovered from HP's macOS filter "rasterToHPZJS" 1.6.1 (class
 * ZJSCommandInterface, cupsModelNumber 4).
 *
 * Stream layout produced by the HP filter for one job:
 *
 *   PJL (ppdEmitJCL)  ... @PJL ENTER LANGUAGE = ZJS\n
 *   "JZJZ"
 *   START_DOC   (3 items)
 *   for each page:
 *     START_PAGE (14 items)
 *     JBIG_BIH   (20-byte BIH)
 *     JBIG_HID   (<= 64 KiB of stripe data, last chunk padded to 16 bytes)
 *     ...
 *     END_JBIG
 *     END_PAGE
 *   END_DOC
 *   PJL (ppdEmitJCLEnd)
 *
 * All integers are big-endian.
 *
 * Chunk header (16 bytes):
 *   u32 size       total chunk size, header included
 *   u32 type       ZJT_*
 *   u32 items      number of items following
 *   u16 itemsize   items * 12
 *   u16 'ZZ'       0x5a5a
 *
 * Item (12 bytes):
 *   u32 size       12
 *   u16 id         ZJI_*
 *   u8  type       1 = uint32
 *   u8  0
 *   u32 value
 */
#ifndef ZJS_H
#define ZJS_H

#define ZJS_MAGIC "JZJZ"

enum {
    ZJT_START_DOC  = 0,
    ZJT_END_DOC    = 1,
    ZJT_START_PAGE = 2,
    ZJT_END_PAGE   = 3,
    ZJT_JBIG_BIH   = 4,
    ZJT_JBIG_HID   = 5,
    ZJT_END_JBIG   = 6
};

enum {
    ZJI_DMPAPER         = 3,   /* Windows DMPAPER_* code            */
    ZJI_DMCOPIES        = 4,
    ZJI_DMDEFAULTSOURCE = 5,   /* tray: 1 = tray 1, 4 = manual, 7 = auto */
    ZJI_DMMEDIATYPE     = 6,   /* cupsMediaType                      */
    ZJI_NBIE            = 7,   /* number of planes (1)               */
    ZJI_RESOLUTION_X    = 8,
    ZJI_RESOLUTION_Y    = 9,
    ZJI_RASTER_X        = 12,  /* padded width in bits               */
    ZJI_RASTER_Y        = 13,  /* padded height                      */
    ZJI_VIDEO_BPP       = 16,
    ZJI_VIDEO_X         = 17,  /* padded width in pixels             */
    ZJI_VIDEO_Y         = 18,
    ZJI_RET             = 22,  /* Resolution Enhancement technology  */
    ZJI_TONER_SAVE      = 23   /* EconoMode                          */
};

#define ZJI_TYPE_UINT32 1

/* Items of START_DOC, in the order HP sends them: {id, value} */
#define ZJS_START_DOC_ITEMS { {1, 0}, {0, 0}, {2, 1} }

/* Size of one JBIG_HID chunk payload */
#define ZJS_HID_CHUNK 65536

/* Line / bit alignment of the page bitmap (PPD HPRowPadding / default
 * stride alignment for monochrome) */
#define ZJS_ROW_PADDING 8
#define ZJS_BIT_ALIGN   64

#endif
