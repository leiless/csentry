/*
 * Created 190622 lynnl
 *
 * see: LICENSE
 */

#ifndef __CSENTRY_H__
#define __CSENTRY_H__

#include <cjson/cJSON.h>
#include <uuid/uuid.h>

#ifndef _nullable
#define _nullable                   /* Pseudo annotation */
#endif

#define CSENTRY_NAME                "cSentry"
#define CSENTRY_VERSION             "0.1"

/* Enclose backtrace into message capture */
#define CSENTRY_CAPTURE_ENCLOSE_BT  0x1u

/*
 * Most significant 3 bits denoted log level
 * Hence at most 8 levels can be supported
 */
#define CSENTRY_LEVEL_ERROR         0x00000000u     /* Unnecessary */
#define CSENTRY_LEVEL_DEBUG         0x20000000u
#define CSENTRY_LEVEL_INFO          0x40000000u
#define CSENTRY_LEVEL_WARN          0x60000000u
#define CSENTRY_LEVEL_FATAL         0x80000000u

#define CSENTRY_BC_TYPE_DEFAULT     0x00000000u     /* Unnecessary */
#define CSENTRY_BC_TYPE_HTTP        0x08000000u
#define CSENTRY_BC_TYPE_ERROR       0x10000000u

void * _nullable csentry_new(const char *, const cJSON * _nullable, float, int);
void csentry_destroy(void *);
void csentry_debug(void *);

void csentry_capture_message(void *, uint32_t, const char *, ...);
void csentry_capture_exception(void *, const char *, ...);
void csentry_add_breadcrumb(void *, const cJSON * _nullable, uint32_t, const char *, ...);

void csentry_get_last_event_id(void *, uuid_t);
void csentry_get_last_event_id_string(void *, uuid_string_t);

char * _nullable csentry_ctx_get(void *);
int csentry_ctx_update_user(void *, const cJSON * _nullable);
int csentry_ctx_update_tags(void *, const cJSON * _nullable);
int csentry_ctx_update_extra(void *, const cJSON * _nullable);
int csentry_ctx_update(void *, const cJSON * _nullable);
void csentry_ctx_clear(void *);

void csentry_set_enable(void *, int);

#endif /* __CSENTRY_H__ */

