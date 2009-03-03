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

#ifdef SUPERSTAR2_ENABLE
extern const struct gps_type_t superstar2_binary;

static bool superstar2_initialize(void)
{
    return true;
}

static void superstar2_update(void)
{
}

static int superstar2_command(char line[])
{
    return COMMAND_UNKNOWN;
}

static void superstar2_wrap(void)
{
}

const struct monitor_object_t superstar2_mmt = {
    .initialize = superstar2_initialize,
    .update = superstar2_update,
    .command = superstar2_command,
    .wrap = superstar2_wrap,
    .min_y = 20, .min_x = 80,	/* size of the device window */
    .driver = &superstar2_binary,
};
#endif
