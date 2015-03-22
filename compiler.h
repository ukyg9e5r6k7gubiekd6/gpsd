/*
 * compiler.h - compiler specific macros
 *
 * This software is distributed under a BSD-style license. See the
 * file "COPYING" in the toop-level directory of the distribution for details.
 */
#ifndef _GPSD_COMPILER_H_
#define _GPSD_COMPILER_H_

/*
 * Tell GCC that we want thread-safe behavior with _REENTRANT;
 * in particular, errno must be thread-local.
 * Tell POSIX-conforming implementations with _POSIX_THREAD_SAFE_FUNCTIONS.
 * See http://www.unix.org/whitepapers/reentrant.html
 */
#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifndef _POSIX_THREAD_SAFE_FUNCTIONS
#define _POSIX_THREAD_SAFE_FUNCTIONS
#endif

#include "gpsd_config.h"	/* is HAVE_STDATOMIC defined? */

/* Macro for declaring function with printf-like arguments. */
# if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 7)
#define PRINTF_FUNC(format_index, arg_index) \
    __attribute__((__format__(__printf__, format_index, arg_index)))
# else
#define PRINTF_FUNC(format_index, arg_indx)
#endif

/* Macro for declaring function arguments unused. */
#if defined(__GNUC__) || defined(__clang__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

/*
 * Macro for compile-time checking if argument is an array.
 * It expands to constant expression with int value 0.
 */
#if defined(__GNUC__)
#define COMPILE_CHECK_IS_ARRAY(arr) ( \
    0 * (int) sizeof(({ \
        struct { \
            int unused_int; \
            typeof(arr) unused_arr; \
        } zero_init = {0}; \
        typeof(arr) arg_is_not_array UNUSED = { \
            zero_init.unused_arr[0], \
        }; \
        1; \
    })) \
)
#else
#define COMPILE_CHECK_IS_ARRAY(arr) 0
#endif

/* Needed because 4.x versions of GCC are really annoying */
#define ignore_return(funcall) \
    do { \
        UNUSED ssize_t locresult = (funcall); \
        assert(locresult != -23); \
    } while (0)

#ifndef S_SPLINT_S
#ifdef HAVE_STDATOMIC_H
#ifndef __COVERITY__	/* Coverity is confused by a GNU typedef */
#include <stdatomic.h>
#endif /* __COVERITY__ */
#endif /* HAVE_STDATOMIC_H */
#endif /* S_SPLINT_S */

#ifdef HAVE_OSATOMIC_H
#include <libkern/OSAtomic.h>
#endif /* HAVE_STDATOMIC_H */

static /*@unused@*/ inline void memory_barrier(void)
/* prevent instruction reordering across any call to this function */
{
#ifndef S_SPLINT_S
#ifdef STD_ATOMIC_H
#ifndef __COVERITY__
    atomic_thread_fence(memory_order_seq_cst);
#endif /* __COVERITY__ */
#elif defined(HAVE_OSATOMIC_H)
    OSMemoryBarrier();
#elif defined(__GNUC__)
    asm volatile ("" : : : "memory");
#endif /* STD_ATOMIC_H */
#endif /* S_SPLINT_S */
}

#endif /* _GPSD_COMPILER_H_ */
