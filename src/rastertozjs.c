/*
 * rastertozjs - CUPS filter for HP LaserJet 1018 / 1020 / 1022 (ZjStream)
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * Native replacement for HP's x86_64-only "rasterToHPZJS" filter. The
 * protocol was recovered by reverse engineering that filter; see zjs.h,
 * jbig_enc.c and firmware.c for details.
 *
 * Usage (as a CUPS filter):
 *   rastertozjs job user title copies options [file]
 * Input:  application/vnd.cups-raster, 1 or 8 bits gray (K, W, SW) or RGB
 * Output: PJL + ZjStream to stdout
 */
#include "zjs.h"
#include "jbig_enc.h"
#include "halftone.h"
#include "firmware.h"

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef HPZJS_VERSION
#define HPZJS_VERSION "2026.11.05.1"
#endif

static volatile sig_atomic_t canceled = 0;

static void on_term(int sig)
{
    (void)sig;
    canceled = 1;
}

/* ---- ZjStream writer ----------------------------------------------------- */

static FILE *out;

static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void zjs_chunk(uint32_t type, uint32_t items, uint32_t payload)
{
    unsigned char h[16];
    be32(h, 16 + items * 12 + payload);
    be32(h + 4, type);
    be32(h + 8, items);
    h[12] = (unsigned char)((items * 12) >> 8);
    h[13] = (unsigned char)(items * 12);
    h[14] = 'Z';
    h[15] = 'Z';
    fwrite(h, 1, sizeof(h), out);
}

static void zjs_item(uint16_t id, uint32_t value)
{
    unsigned char it[12];
    be32(it, 12);
    it[4] = (unsigned char)(id >> 8);
    it[5] = (unsigned char)id;
    it[6] = ZJI_TYPE_UINT32;
    it[7] = 0;
    be32(it + 8, value);
    fwrite(it, 1, sizeof(it), out);
}

static void zjs_start_doc(void)
{
    static const uint32_t items[][2] = ZJS_START_DOC_ITEMS;
    size_t n = sizeof(items) / sizeof(items[0]);

    fwrite(ZJS_MAGIC, 1, 4, out);
    zjs_chunk(ZJT_START_DOC, (uint32_t)n, 0);
    for (size_t i = 0; i < n; i++)
        zjs_item((uint16_t)items[i][0], items[i][1]);
}

typedef struct {
    uint32_t paper, copies, source, media, xres, yres;
    uint32_t width, height;     /* padded, pixels */
    uint32_t bpp;
    int ret, econo;
} page_info_t;

static void zjs_start_page(const page_info_t *p)
{
    zjs_chunk(ZJT_START_PAGE, 14, 0);
    zjs_item(ZJI_DMPAPER, p->paper);
    zjs_item(ZJI_DMCOPIES, p->copies);
    zjs_item(ZJI_DMDEFAULTSOURCE, p->source);
    zjs_item(ZJI_DMMEDIATYPE, p->media);
    zjs_item(ZJI_NBIE, 1);
    zjs_item(ZJI_RESOLUTION_X, p->xres);
    zjs_item(ZJI_RESOLUTION_Y, p->yres);
    zjs_item(ZJI_RASTER_X, p->width * p->bpp);
    zjs_item(ZJI_RASTER_Y, p->height);
    zjs_item(ZJI_VIDEO_BPP, p->bpp);
    zjs_item(ZJI_VIDEO_X, p->width);
    zjs_item(ZJI_VIDEO_Y, p->height);
    zjs_item(ZJI_RET, p->ret ? 1 : 0);
    zjs_item(ZJI_TONER_SAVE, p->econo ? 1 : 0);
}

static void zjs_jbig(const jbig_buf_t *bie)
{
    static const unsigned char zeros[16];
    const unsigned char *d = bie->data + 20;
    size_t rest = bie->len - 20;
    size_t pad = (16 - (rest & 15)) & 15;

    zjs_chunk(ZJT_JBIG_BIH, 0, 20);
    fwrite(bie->data, 1, 20, out);

    while (rest >= ZJS_HID_CHUNK) {
        zjs_chunk(ZJT_JBIG_HID, 0, ZJS_HID_CHUNK);
        fwrite(d, 1, ZJS_HID_CHUNK, out);
        d += ZJS_HID_CHUNK;
        rest -= ZJS_HID_CHUNK;
    }
    /* HP always sends a final (possibly empty) chunk, padded to 16 bytes */
    zjs_chunk(ZJT_JBIG_HID, 0, (uint32_t)(rest + pad));
    fwrite(d, 1, rest, out);
    fwrite(zeros, 1, pad, out);

    zjs_chunk(ZJT_END_JBIG, 0, 0);
}

/* ---- page parameters ----------------------------------------------------- */

/* DMPAPER code from the page size in points, as done by HP's filter
 * (+[AHBRasterPageHeader measurePaperSizeForHeader:]); 512 = custom. */
static uint32_t dm_paper(unsigned w, unsigned h)
{
    static const struct { unsigned w, h, code; } t[] = {
        { 612,  792,   1 },   /* Letter       */
        { 612, 1008,   5 },   /* Legal        */
        { 522,  756,   7 },   /* Executive    */
        { 595,  842,   9 },   /* A4           */
        { 420,  595,  11 },   /* A5           */
        { 729, 1032,  12 },   /* B4 (JIS)     */
        { 297,  684,  20 },   /* Envelope #10 */
        { 312,  624,  27 },   /* Envelope DL  */
        { 842, 1191,   8 },   /* A3           */
        { 792, 1224,   4 },   /* Ledger       */
        { 516,  729, 357 },   /* B5 (JIS)     */
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (t[i].w == w && t[i].h == h)
            return t[i].code;
    return 512;
}

static uint32_t round_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) / a * a;
}

/* ---- options ------------------------------------------------------------- */

typedef struct {
    int ret, econo;
    ht_mode_t ht;
    double gamma;
    fw_mode_t fw_mode;
    const char *fw_path;
} job_opts_t;

static const char *marked(ppd_file_t *ppd, const char *opt)
{
    ppd_choice_t *c = ppd ? ppdFindMarkedChoice(ppd, opt) : NULL;
    return c ? c->choice : NULL;
}

static int is_true(const char *s, int def)
{
    if (!s)
        return def;
    return !strcasecmp(s, "True") || !strcasecmp(s, "On") ||
           !strcasecmp(s, "Yes") || !strcmp(s, "1");
}

static void get_options(ppd_file_t *ppd, int num, cups_option_t *opts,
                        job_opts_t *o)
{
    const char *v;
    ppd_attr_t *a;

    o->ret = is_true(marked(ppd, "REt"), 1);
    o->econo = is_true(marked(ppd, "EconoMode"), 0);

    if ((v = cupsGetOption("zjsHalftone", num, opts)) == NULL)
        v = marked(ppd, "zjsHalftone");
    o->ht = HT_SCREEN;
    if (v && !strcasecmp(v, "Diffusion"))
        o->ht = HT_DIFFUSION;
    else if (v && !strcasecmp(v, "Threshold"))
        o->ht = HT_THRESHOLD;

    o->gamma = 1.0;
    if ((v = cupsGetOption("zjsGamma", num, opts)) == NULL)
        v = marked(ppd, "zjsGamma");
    if (v)
        o->gamma = atof(v) > 0 ? atof(v) : 1.0;

    o->fw_mode = FW_AUTO;
    if ((v = cupsGetOption("zjsFirmware", num, opts)) == NULL)
        v = marked(ppd, "zjsFirmware");
    if (v && !strcasecmp(v, "Always"))
        o->fw_mode = FW_ALWAYS;
    else if (v && !strcasecmp(v, "Never"))
        o->fw_mode = FW_NEVER;

    o->fw_path = NULL;
    if (ppd && (a = ppdFindAttr(ppd, "zjsFirmwareFile", NULL)) != NULL)
        o->fw_path = a->value;
    if (getenv("ZJS_FIRMWARE"))
        o->fw_path = getenv("ZJS_FIRMWARE");
}

/* ---- raster line -> 8-bit K ---------------------------------------------- */

static int convert_line(const cups_page_header2_t *h, const unsigned char *in,
                        unsigned char *k)
{
    uint32_t w = h->cupsWidth;
    int white_is_zero = h->cupsColorSpace == CUPS_CSPACE_K;

    if (h->cupsBitsPerPixel == 1) {
        for (uint32_t x = 0; x < w; x++) {
            int bit = (in[x >> 3] >> (7 - (x & 7))) & 1;
            k[x] = (bit ^ !white_is_zero) ? 255 : 0;
        }
    } else if (h->cupsBitsPerPixel == 8) {
        if (white_is_zero)
            memcpy(k, in, w);
        else
            for (uint32_t x = 0; x < w; x++)
                k[x] = (unsigned char)(255 - in[x]);
    } else if (h->cupsBitsPerPixel == 24 && h->cupsBitsPerColor == 8) {
        for (uint32_t x = 0; x < w; x++, in += 3) {
            unsigned lum = (in[0] * 77u + in[1] * 150u + in[2] * 29u) >> 8;
            k[x] = (unsigned char)(255 - lum);
        }
    } else {
        return -1;
    }
    return 0;
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int fd = 0, page = 0, status = 0;
    cups_raster_t *ras;
    cups_page_header2_t h;
    ppd_file_t *ppd = NULL;
    cups_option_t *opts = NULL;
    int num_opts = 0;
    job_opts_t jo;
    struct sigaction sa;

    setbuf(stderr, NULL);
    out = stdout;

    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts(HPZJS_VERSION);
        return 0;
    }
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "Usage: %s job user title copies options [file]\n"
                "rastertozjs %s - HP LaserJet 1018/1020/1022 ZjStream "
                "filter\n", argv[0], HPZJS_VERSION);
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);

    if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
        fprintf(stderr, "ERROR: Unable to open %s: %s\n", argv[6],
                strerror(errno));
        return 1;
    }

    if (getenv("PPD") && (ppd = ppdOpenFile(getenv("PPD"))) != NULL) {
        ppdMarkDefaults(ppd);
        num_opts = cupsParseOptions(argv[5], 0, &opts);
        cupsMarkOptions(ppd, num_opts, opts);
    } else {
        num_opts = cupsParseOptions(argv[5], 0, &opts);
    }
    get_options(ppd, num_opts, opts, &jo);

    ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
    if (!ras) {
        fputs("ERROR: Unable to read raster data\n", stderr);
        return 1;
    }

    while (!canceled && cupsRasterReadHeader2(ras, &h)) {
        uint32_t w = h.cupsWidth, hgt = h.cupsHeight;
        uint32_t pw, ph, stride;
        unsigned char *bitmap, *line, *k;
        halftoner_t *ht;
        jbig_buf_t bie = { 0 };
        page_info_t pi;

        if (!w || !hgt || !h.cupsBytesPerLine || w > 40000 || hgt > 60000) {
            fputs("ERROR: Bad raster page header\n", stderr);
            status = 1;
            break;
        }
        page++;
        fprintf(stderr, "INFO: Printing page %d\n", page);

        if (page == 1) {
            if (jo.fw_path)
                fw_ensure(out, jo.fw_path, jo.fw_mode);
            if (ppd)
                ppdEmitJCL(ppd, out, atoi(argv[1]), argv[2], argv[3]);
            else
                fputs("\033%-12345X@PJL JOB\n@PJL ENTER LANGUAGE = ZJS \n",
                      out);
            zjs_start_doc();
        }

        pw = round_up(w, ZJS_BIT_ALIGN);
        ph = round_up(hgt, ZJS_ROW_PADDING);
        stride = pw / 8;
        bitmap = calloc((size_t)stride, ph);
        line = malloc(h.cupsBytesPerLine);
        k = malloc(w);
        ht = ht_new(jo.ht, (int)h.HWResolution[0], (int)h.HWResolution[1],
                    w, jo.gamma);
        if (!bitmap || !line || !k || !ht) {
            fputs("ERROR: Out of memory\n", stderr);
            status = 1;
            free(bitmap); free(line); free(k); ht_free(ht);
            break;
        }

        for (uint32_t y = 0; y < hgt && !canceled; y++) {
            if (cupsRasterReadPixels(ras, line, h.cupsBytesPerLine) !=
                h.cupsBytesPerLine)
                break;
            if (convert_line(&h, line, k) < 0) {
                fprintf(stderr, "ERROR: Unsupported raster format (%u bpp, "
                        "color space %u)\n", h.cupsBitsPerPixel,
                        h.cupsColorSpace);
                status = 1;
                break;
            }
            ht_line(ht, k, y, bitmap + (size_t)y * stride);
        }
        ht_free(ht);
        free(line);
        free(k);

        if (status) {
            free(bitmap);
            break;
        }

        if (jbig_encode(bitmap, stride, pw, ph, 128, &bie) < 0) {
            fputs("ERROR: Out of memory (JBIG)\n", stderr);
            free(bitmap);
            status = 1;
            break;
        }
        free(bitmap);

        pi.paper = dm_paper((unsigned)h.PageSize[0], (unsigned)h.PageSize[1]);
        pi.copies = h.NumCopies ? h.NumCopies : 1;
        pi.source = h.MediaPosition ? h.MediaPosition : 7;
        pi.media = h.cupsMediaType;
        pi.xres = h.HWResolution[0];
        pi.yres = h.HWResolution[1];
        pi.width = pw;
        pi.height = ph;
        pi.bpp = 1;
        pi.ret = jo.ret;
        pi.econo = jo.econo;

        zjs_start_page(&pi);
        zjs_jbig(&bie);
        zjs_chunk(ZJT_END_PAGE, 0, 0);
        fflush(out);
        jbig_buf_free(&bie);

        fprintf(stderr, "PAGE: %d %u\n", page, pi.copies);
    }

    if (page > 0) {
        zjs_chunk(ZJT_END_DOC, 0, 0);
        if (ppd)
            ppdEmitJCLEnd(ppd, out);
        else
            fputs("\033%-12345X@PJL EOJ\n\033%-12345X", out);
        fflush(out);
    }

    cupsRasterClose(ras);
    if (fd)
        close(fd);
    cupsFreeOptions(num_opts, opts);
    if (ppd)
        ppdClose(ppd);

    if (page == 0 && !status)
        fputs("INFO: No pages found\n", stderr);
    return status;
}
