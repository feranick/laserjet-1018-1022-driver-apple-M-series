/*
 * firmware.h - firmware download for HP LaserJet 1018/1020
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 */
#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdio.h>

typedef enum {
    FW_AUTO = 0,   /* query the printer, upload if older/absent (HP behaviour) */
    FW_ALWAYS,     /* always upload before the job                           */
    FW_NEVER
} fw_mode_t;

/*
 * Make sure the printer runs the firmware contained in 'path' (an HP
 * ".acl" file: 8 ASCII digits of version followed by the image).
 * Data is written to 'out' (the printer, i.e. stdout of the filter);
 * replies are read through the CUPS back channel.
 * Returns 1 if firmware was sent, 0 if not needed, -1 on error.
 */
int fw_ensure(FILE *out, const char *path, fw_mode_t mode);

#endif
