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

void pthread_detach_safe(pthread_t thd);
void pthread_mutex_lock_safe(pthread_mutex_t *);
void pthread_mutex_unlock_safe(pthread_mutex_t *);
void pthread_mutex_destroy_safe(pthread_mutex_t *);
void pthread_cond_wait_safe(pthread_cond_t *, pthread_mutex_t *);
void pthread_cond_signal_safe(pthread_cond_t *);
void pthread_cond_destroy_safe(pthread_cond_t *);

int cjson_add_or_update_object(cJSON *, const char *, cJSON * _nullable);
int cjson_add_object(cJSON *, const char *, cJSON * _nullable);

cJSON * _nullable cjson_add_or_update_str_to_obj(
    cJSON *, const char *, const char *
);

cJSON * _nullable cjson_add_or_update_str_to_obj_x(
    cJSON *, const char *, const char *
);

cJSON * _nullable cjson_set_default_str_to_obj(
    cJSON *, const char *, const char *
);

#endif /* CSENTRY_UTILS_H */

