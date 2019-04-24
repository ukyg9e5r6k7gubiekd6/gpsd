/*
 * Constants used for GPS time detection and rollover correction.
 *
 * Correct for week beginning 2019-04-18T00:00:00
 *
 * This file is Copyright (c) 2019 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */
#define BUILD_CENTURY	2000
#define BUILD_WEEK	2                   # Assumes 10-bit week counter
#define BUILD_LEAPSECONDS	19
#define BUILD_ROLLOVERS	2         # Assumes 10-bit week counter
