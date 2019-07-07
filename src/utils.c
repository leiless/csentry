/*
 * Created 190622 lynnl
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "utils.h"

/**
 * @return  Check if given string s1 starts with s2
 * XXX:     Always return 1 if s2 is empty
 *
 * Taken from xnu/osfmk/device/subrs.c#strprefix()
 */
int strprefix(const char *s1, const char *s2)
{
    int c;

    assert_nonnull(s1);
    assert_nonnull(s2);

    while ((c = *s2++) != '\0') {
        if (c != *s1++) return 0;
    }

    return 1;
}

#define ISO_8601_BUFSZ      20

/**
 * Format ISO 8601 datetime without trailing timezone
 */
void format_iso_8601_time(char *str)
{
    time_t now;

    assert_nonnull(str);
    (void) time(&now);

    *str = '\0';
    /* gmtime(3) should never return NULL in such case */
    (void) strftime(str, ISO_8601_BUFSZ, "%Y-%m-%dT%H:%M:%S", gmtime(&now));
}

/**
 * Convert an input UUID string(without hyphens) into binary representation
 * @param in        Input UUID string(must be 32-length long)
 * @param uu        [OUT] Binary UUID representation
 * @return          0 if success  -1 otherwise
 */
int uuid_parse32(const char *in, uuid_t uu)
{
    uuid_string_t str;

    assert_nonnull(in);
    assert_nonnull(uu);

    if (strlen(in) != 32) return -1;
    (void) sprintf(str, "%.*s-%.*s-%.*s-%.*s-%s",
            8, in,
            4, in + 8,
            4, in + 12,
            4, in + 16,
            in + 20);

    return uuid_parse(str, uu);
}

/**
 * Generate random UUID string
 * @param str   [OUT] UUID string
 */
void uuid_string_random(uuid_string_t str)
{
    uuid_t uu;
    assert_nonnull(str);
    uuid_generate(uu);
    uuid_unparse_lower(uu, str);
}


void pmtx_lock(pthread_mutex_t *mtx)
{
    int e;
    assert_nonnull(mtx);
    e = pthread_mutex_lock(mtx);
    assert(e == 0);
}

void pmtx_unlock(pthread_mutex_t *mtx)
{
    int e;
    assert_nonnull(mtx);
    e = pthread_mutex_unlock(mtx);
    assert(e == 0);
}

/**
 * Add/update an object to json in a non-atomic way
 * @return      1 if added/updated 0 otherwise
 */
int cjson_add_or_update_object(cJSON *json, const char *name, cJSON * _nullable item)
{
    assert_nonnull(json);
    assert_nonnull(name);

    if (item == NULL) return 0;

    /* if `item' is NULL, cJSON_AddItemToObject(), cJSON_ReplaceItemInObject() will do nothing */

    if (cJSON_GetObjectItem(json, name) != NULL) {
        cJSON_ReplaceItemInObject(json, name, item);
    } else {
        cJSON_AddItemToObject(json, name, item);
    }

    return cJSON_GetObjectItem(json, name) == item;
}

int cjson_add_object(cJSON *json, const char *name, cJSON * _nullable item)
{
    assert_nonnull(json);
    assert_nonnull(name);
    if (item != NULL) {
        cJSON_AddItemToObject(json, name, item);
        return cJSON_GetObjectItem(json, name) == item;
    }
    return 0;
}

cJSON * _nullable cjson_add_or_update_str_to_obj(
        cJSON *obj,
        const char *name,
        const char *str)
{
    cJSON *item;

    assert_nonnull(obj);
    assert_nonnull(name);
    assert_nonnull(str);

    if (cJSON_GetObjectItem(obj, name) != NULL) {
        item = cJSON_CreateString(str);
        (void) cJSON_ReplaceItemInObject(obj, name, item);
        return item;
    }

    return cJSON_AddStringToObject(obj, name, str);
}

cJSON * _nullable cjson_set_default_str_to_obj(
        cJSON *obj,
        const char *name,
        const char *str)
{
    cJSON *item;

    assert_nonnull(obj);
    assert_nonnull(name);
    assert_nonnull(str);

    item = cJSON_GetObjectItem(obj, name);
    if (item != NULL) {
        if (cJSON_IsString(item)) return item;

        /* Try to correct with string value */
        item = cJSON_CreateString(str);
        (void) cJSON_ReplaceItemInObject(obj, name, item);
        return item;
    }

    return cJSON_AddStringToObject(obj, name, str);
}

