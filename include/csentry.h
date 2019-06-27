/*
 * Created 190622 lynnl
 *
 * see: LICENSE
 */

#ifndef __CSENTRY_H__
#define __CSENTRY_H__

#include <cjson/cJSON.h>

#define CSENTRY_NAME                "cSentry"
#define CSENTRY_VERSION             "0.1"

#define CSENTRY_CAPTURE_ASYNC       0x1

#define CSENTRY_LEVEL_DEBUG         0x10000000
#define CSENTRY_LEVEL_INFO          0x20000000
#define CSENTRY_LEVEL_WARN          0x30000000
#define CSENTRY_LEVEL_FATAL         0x40000000

void * _nullable csentry_new(const char *, const cJSON * _nullable, float, int);
void csentry_destroy(void *);

void csentry_capture_message(void *, const cJSON * _nullable, uint32_t, const char *);
void csentry_get_last_event_id(void *, uuid_t);
void csentry_get_last_event_id_string(void *, uuid_string_t);

const cJSON *csentry_ctx_get(void *);
int csentry_ctx_update_user(void *, const cJSON * _nullable);
int csentry_ctx_update_tags(void *, const cJSON * _nullable);
int csentry_ctx_update_extra(void *, const cJSON * _nullable);
int csentry_ctx_update(void *, const cJSON * _nullable);
void csentry_ctx_clear(void *);

#endif /* __CSENTRY_H__ */

