/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include "gpsd_config.h"

#ifndef HAVE_GETSID
#include <unistd.h>
#include <sys/syscall.h>

/* This implementation is required for Android */

pid_t getsid(pid_t pid) {
  return syscall(__NR_getsid, pid);
}
#endif /* HAVE_GETSID */
