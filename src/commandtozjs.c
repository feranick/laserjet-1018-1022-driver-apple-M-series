/*
 * commandtozjs - CUPS command filter for HP LaserJet 1018 / 1020 / 1022
 *
 * Copyright (c) 2026 Nicola Ferralis
 * SPDX-License-Identifier: MIT
 *
 * Handles application/vnd.cups-command files. Supported commands:
 *   Clean          print a cleaning page (@PJL CLEANPRINTER, as HP did)
 *   ReportLevels   no supply levels are available on these models
 *   ReportStatus   no-op
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define UEL "\033%-12345X"

int main(int argc, char *argv[])
{
    FILE *in = stdin;
    char line[1024];

    setbuf(stderr, NULL);
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "Usage: %s job user title copies options [file]\n",
                argv[0]);
        return 1;
    }
    if (argc == 7 && !(in = fopen(argv[6], "r"))) {
        fprintf(stderr, "ERROR: Unable to open %s: %s\n", argv[6],
                strerror(errno));
        return 1;
    }

    while (fgets(line, sizeof(line), in)) {
        char *p = line;
        size_t n;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || !*p)
            continue;
        n = strcspn(p, " \t\r\n");
        p[n] = 0;

        if (!strcasecmp(p, "Clean")) {
            fputs("INFO: Printing cleaning page\n", stderr);
            fputs(UEL "@PJL\r\n@PJL CLEANPRINTER\r\n" UEL, stdout);
        } else if (!strcasecmp(p, "ReportLevels")) {
            fputs("ATTR: marker-colors=#000000 marker-names=Black "
                  "marker-types=toner marker-levels=-1\n", stderr);
        } else if (!strcasecmp(p, "ReportStatus")) {
            /* nothing to do */
        } else {
            fprintf(stderr, "DEBUG: Ignoring unsupported command \"%s\"\n", p);
        }
    }
    fflush(stdout);
    if (in != stdin)
        fclose(in);
    return 0;
}
