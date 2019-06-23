/*
 * Created 190622 lynnl
 */

#ifndef CSENTRY_UTILS_H
#define CSENTRY_UTILS_H

#include <assert.h>

#ifndef assert_nonnull
#define assert_nonnull(p)       assert((p) != NULL)
#endif

/**
 * Should only used for `char[]'  NOT `char *'
 * Assume ends with null byte('\0')
 */
#define STRLEN(s)               (sizeof(s) - 1)

#ifndef MIN
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#endif

#define UNUSED(e, ...)          (void) ((void) (e), ##__VA_ARGS__)

#define _nullable               /* Pseudo annotation */

#define set_error_and_jump(var, val, label)     \
    do {                                        \
        var = (val);                            \
        goto label;                             \
    } while (0)

/*
 * Assume all error variable named `e' and all goto label starts with `out_'
 */
#define set_err_jmp(val, label) set_error_and_jump(e, val, out_##label)

int strprefix(const char *, const char *);

#endif /* CSENTRY_UTILS_H */

