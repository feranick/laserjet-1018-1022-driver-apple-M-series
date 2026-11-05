/*
 * firmware.c - firmware download for HP LaserJet 1018/1020
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * The LaserJet 1018 and 1020 keep their firmware in RAM: it has to be
 * downloaded every time the printer is powered on. The procedure below is
 * the one used by HP's own Mac filter (methods -[AHBJob downloadFirmware*]
 * and -[AHBJob currentFirmwareVersion] of rasterToHPZJS 1.6.1):
 *
 *  1. version query (PJL -> ACL), 59 bytes:
 *       ESC%-12345X@PJL ENTER LANGUAGE=ACL\r\n
 *       00 ac 00 01 + 12 x 00
 *       ESC%-12345X
 *     The reply, read on the back channel, contains 00 ac 00 01 00 01
 *     followed by 8 ASCII digits (the firmware date, e.g. "20050309").
 *  2. if the printer version is lower than the first 8 bytes of the .acl
 *     file, send:
 *       ESC%-12345X@PJL ENTER LANGUAGE=ACL\r\n
 *       00 ac c0 de + u32 big-endian (file size - 8)
 *       <the whole .acl file>
 *       ESC%-12345X
 *  3. drain the output and wait for the printer to come back on line.
 */
#include "firmware.h"

#include <cups/cups.h>
#include <cups/sidechannel.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define UEL "\033%-12345X"
#define ENTER_ACL UEL "@PJL ENTER LANGUAGE=ACL\r\n"

static void dbg(const char *msg, const char *arg)
{
    fprintf(stderr, "DEBUG: hpzjs firmware: %s%s\n", msg, arg ? arg : "");
}

static int bidi_available(void)
{
    char data[8];
    int len = sizeof(data);

    if (cupsSideChannelDoRequest(CUPS_SC_CMD_GET_BIDI, data, &len, 3.0) !=
        CUPS_SC_STATUS_OK || len < 1)
        return 0;
    return data[0] == CUPS_SC_BIDI_SUPPORTED;
}

static void drain(FILE *out)
{
    char data[8];
    int len = sizeof(data);

    fflush(out);
    cupsSideChannelDoRequest(CUPS_SC_CMD_DRAIN_OUTPUT, data, &len, 60.0);
}

static long parse_version(const unsigned char *p)
{
    char tmp[9];
    for (int i = 0; i < 8; i++) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        tmp[i] = (char)p[i];
    }
    tmp[8] = 0;
    return atol(tmp);
}

/* Returns the firmware version reported by the printer, 0 if the printer
 * answered without a usable version, or -1 if nothing was received. */
static long query_version(FILE *out)
{
    static const unsigned char tail[] = {
        0x00, 0xac, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    static const unsigned char reply[] = { 0x00, 0xac, 0x00, 0x01, 0x00, 0x01 };
    unsigned char buf[1024];
    size_t n = 0;
    time_t start;

    fwrite(ENTER_ACL, 1, sizeof(ENTER_ACL) - 1, out);
    fwrite(tail, 1, sizeof(tail), out);
    fwrite(UEL, 1, sizeof(UEL) - 1, out);
    drain(out);

    start = time(NULL);
    while (n < sizeof(buf) && time(NULL) - start < 10) {
        ssize_t r = cupsBackChannelRead((char *)buf + n, sizeof(buf) - n, 5.0);
        if (r <= 0)
            break;
        n += (size_t)r;
        for (size_t i = 0; i + sizeof(reply) + 8 <= n; i++)
            if (!memcmp(buf + i, reply, sizeof(reply))) {
                long v = parse_version(buf + i + sizeof(reply));
                return v < 0 ? 0 : v;
            }
    }
    if (n) {
        char hex[3 * 48 + 1];
        size_t m = n < 48 ? n : 48;
        for (size_t i = 0; i < m; i++)
            snprintf(hex + 3 * i, 4, "%02x ", buf[i]);
        dbg("unrecognised reply: ", hex);
    }
    return n ? 0 : -1;
}

/* Fallback when the ACL query gets no answer: a 1018/1020 running its boot
 * loader does not report "FWVER" in its IEEE-1284 device ID. */
static int device_id_says_no_firmware(void)
{
    char id[1024];
    int len = sizeof(id) - 1;

    if (cupsSideChannelDoRequest(CUPS_SC_CMD_GET_DEVICE_ID, id, &len, 5.0) !=
        CUPS_SC_STATUS_OK || len <= 0)
        return 0;
    id[len] = 0;
    dbg("device ID: ", id);
    if (!strstr(id, "LaserJet 1018") && !strstr(id, "LaserJet 1020"))
        return 0;
    return strstr(id, "FWVER") == NULL;
}

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int send_firmware(FILE *out, const unsigned char *fw, size_t len)
{
    unsigned char hdr[8] = { 0x00, 0xac, 0xc0, 0xde };

    put_be32(hdr + 4, (uint32_t)(len - 8));
    fprintf(stderr, "INFO: Sending firmware to printer (%lu bytes)\n",
            (unsigned long)len);
    fwrite(ENTER_ACL, 1, sizeof(ENTER_ACL) - 1, out);
    fwrite(hdr, 1, sizeof(hdr), out);
    if (fwrite(fw, 1, len, out) != len)
        return -1;
    fwrite(UEL, 1, sizeof(UEL) - 1, out);
    drain(out);

    /* wait for the printer to reboot into the new firmware */
    for (int i = 0; i < 10; i++) {
        char state = 0;
        int slen = 1;
        if (cupsSideChannelDoRequest(CUPS_SC_CMD_GET_STATE, &state, &slen,
                                     3.0) == CUPS_SC_STATUS_OK &&
            slen == 1 && (state & CUPS_SC_STATE_ONLINE))
            break;
        sleep(1);
    }
    sleep(2);
    fprintf(stderr, "INFO: Firmware sent\n");
    return 1;
}

int fw_ensure(FILE *out, const char *path, fw_mode_t mode)
{
    FILE *f;
    unsigned char *fw;
    long size, file_ver, dev_ver = -1;
    int rc = 0, need = 0;

    if (mode == FW_NEVER || !path || !*path)
        return 0;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "WARNING: Firmware file %s not found, not sending "
                "firmware\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 64 || size > 16 * 1024 * 1024 || !(fw = malloc((size_t)size))) {
        fclose(f);
        return -1;
    }
    if (fread(fw, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(fw);
        return -1;
    }
    fclose(f);

    file_ver = parse_version(fw);
    if (file_ver < 0) {
        fprintf(stderr, "WARNING: %s is not an HP .acl firmware file\n", path);
        free(fw);
        return -1;
    }

    if (mode == FW_ALWAYS) {
        need = 1;
    } else if (bidi_available()) {
        dev_ver = query_version(out);
        if (dev_ver >= 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "%ld, file %ld", dev_ver, file_ver);
            dbg("printer firmware ", msg);
            need = dev_ver < file_ver;
        } else {
            dbg("no reply to version query", NULL);
            need = device_id_says_no_firmware();
        }
    } else {
        dbg("back channel not available", NULL);
        need = device_id_says_no_firmware();
    }

    if (need)
        rc = send_firmware(out, fw, (size_t)size);
    else
        dbg("printer does not need firmware", NULL);
    free(fw);
    return rc;
}
