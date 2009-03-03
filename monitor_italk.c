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

#ifdef ITRAX_ENABLE
extern const struct gps_type_t italk_binary;

static bool italk_initialize(void)
{
    return true;
}

static void italk_update(void)
{
}

static int italk_command(char line[])
{
    return COMMAND_UNKNOWN;
}

static void italk_wrap(void)
{
}

const struct monitor_object_t italk_mmt = {
    .initialize = italk_initialize,
    .update = italk_update,
    .command = NULL,
    .wrap = NULL,
    .min_y = 20, .min_x = 80,	/* size of the device window */
    .driver = &italk_binary,
};
#endif
