/*
 * Created 190622 lynnl
 */

#ifndef CSENTRY_UTILS_H
#define CSENTRY_UTILS_H

#include <assert.h>

#ifndef assert_nonnull
#define assert_nonnull(p)       assert((p) != NULL)
#endif

#ifndef STRLEN
/**
 * Should only used for `char[]'  NOT `char *'
 * Assume ends with null byte('\0')
 */
#define STRLEN(s)               (sizeof(s) - 1)
#endif

#ifndef MIN
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#endif

#define set_error_and_jump(var, val, label)     \
    do {                                        \
        var = (val);                            \
        goto label;                             \
    } while (0)

#define set_errno_and_jump(val, label)  \
    set_error_and_jump(errno, val, label)

/*
 * Assume all error variable named `e' and all goto label starts with `out_'
 */
#define set_err_jmp(val, label) set_error_and_jump(e, val, out_##label)

int strprefix(const char *, const char *);

#endif /* CSENTRY_UTILS_H */

