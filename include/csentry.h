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

void * _nullable csentry_new(const char *, const cJSON * _nullable, float, int);
void csentry_destroy(void *);

void csentry_capture_message(void *, const cJSON * _nullable, int, const char *);

void csentry_ctx_clear(void *);
int csentry_ctx_merge(void *, const cJSON * _nullable);

#endif /* __CSENTRY_H__ */

