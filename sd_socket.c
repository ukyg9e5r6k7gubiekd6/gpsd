/*
 * This file is Copyright (c) 2011 by Eckhart Wörner
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <limits.h>
#include <stdlib.h>
#ifndef S_SPLINT_S
#include <unistd.h> /* confuses splint ('Internal Bug') */
#endif /* S_SPLINT_S */

#include "sd_socket.h"

int sd_get_socket_count(void) {
    unsigned long n;
    const char* env;

    env = getenv("LISTEN_PID");
    if (!env)
        return 0;

    n = strtoul(env, NULL, 10);
    if (n == ULONG_MAX || (pid_t)n != (pid_t)getpid())
        return 0;

    env = getenv("LISTEN_FDS");
    if (!env)
        return 0;

    n = strtoul(env, NULL, 10);
    if (n == ULONG_MAX)
        return 0;

    return (int)n;
}
