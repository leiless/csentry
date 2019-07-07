/*
 * Created 190622 lynnl
 */

#ifndef CSENTRY_UTILS_H
#define CSENTRY_UTILS_H

#include <assert.h>
#include <uuid/uuid.h>
#include <pthread.h>

#include <cjson/cJSON.h>

#ifndef assert_nonnull
#define assert_nonnull(p)       assert((p) != NULL)
#endif

#define ARRAY_SIZE(a)           (sizeof(a) / sizeof(*(a)))

/**
 * Should only used for `char[]'  NOT `char *'
 * Assume ends with null byte('\0')
 */
#define STRLEN(s)               (sizeof(s) - 1)

#ifndef MIN
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#endif

#define UNUSED(e, ...)          (void) ((void) (e), ##__VA_ARGS__)

#ifndef _nullable
#define _nullable                   /* Pseudo annotation */
#endif

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

#define ISO_8601_BUFSZ      20

void format_iso_8601_time(char *);

int uuid_parse32(const char *, uuid_t);
void uuid_string_random(uuid_string_t);

void pmtx_lock(pthread_mutex_t *mtx);
void pmtx_unlock(pthread_mutex_t *mtx);

int cjson_add_or_update_object(cJSON *, const char *, cJSON * _nullable);
int cjson_add_object(cJSON *, const char *, cJSON * _nullable);

#endif /* CSENTRY_UTILS_H */

