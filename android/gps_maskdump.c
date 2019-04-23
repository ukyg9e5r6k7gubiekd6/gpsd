/* This is the hand-written Android version, since Android cannot make use of scons */

#include <stdio.h>

#include "gpsd.h"

const char *gps_maskdump(gps_mask_t set)
{
    static char buf[222];
    snprintf(buf, sizeof(buf), "{ 0x%llX }", (unsigned long long) set);
    return buf;
}

