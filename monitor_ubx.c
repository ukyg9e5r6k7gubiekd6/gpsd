/* $Id$ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>

#include "gpsd_config.h"
#ifdef HAVE_NCURSES_H
#include <ncurses.h>
#else
#include <curses.h>
#endif /* HAVE_NCURSES_H */
#include "gpsd.h"

#include "bits.h"
#include "gpsmon.h"

#ifdef UBX_ENABLE
extern const struct gps_type_t ubx_binary;

static bool ubx_initialize(void)
{
    return true;
}

static void ubx_update(void)
{
}

static int ubx_command(char line[])
{
    return COMMAND_UNKNOWN;
}

static void ubx_wrap(void)
{
}

const struct monitor_object_t ubx_mmt = {
    .initialize = ubx_initialize,
    .update = ubx_update,
    .command = ubx_command,
    .wrap = ubx_wrap,
    .min_y = 20, .min_x = 80,	/* size of the device window */
    .driver = &ubx_binary,
};
#endif
