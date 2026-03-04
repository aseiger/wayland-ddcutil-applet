/*
 * volume.c - Display speaker volume control via DDC/CI
 *
 * Uses `ddcutil` to read/write VCP feature 0x62 (Audio Speaker Volume).
 * ddcutil must be installed: sudo apt install ddcutil
 */

#include "volume.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DDCUTIL     "ddcutil"
#define VCP_VOLUME  "62"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/*
 * Parse the current value from ddcutil getvcp output.
 *
 * Continuous VCP:  "... current value =  50, max value = 100"
 * Simple NC VCP:   "... Volume level: 50 (00x32)"
 *
 * We try both patterns.
 */
static int parse_getvcp(const char *vcp_code)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s getvcp %s 2>/dev/null", DDCUTIL, vcp_code);

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;

    int current = -1;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p;

        /* Try continuous format: "current value = X" */
        p = strstr(line, "current value =");
        if (p) {
            if (sscanf(p, "current value = %d", &current) == 1)
                break;
        }

        /* Try simple NC format: "level: X" or "Level: X" */
        p = strstr(line, "level:");
        if (!p) p = strstr(line, "Level:");
        if (p) {
            if (sscanf(p + 6, " %d", &current) == 1)
                break;
        }
    }
    pclose(fp);
    return current;
}

static int set_vcp(const char *vcp_code, int value)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s setvcp %s %d 2>/dev/null",
             DDCUTIL, vcp_code, value);
    return (system(cmd) == 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int volume_get(void)
{
    return parse_getvcp(VCP_VOLUME);
}

int volume_set(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    return set_vcp(VCP_VOLUME, percent);
}
