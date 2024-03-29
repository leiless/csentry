/*
 * Created 190623 lynnl
 *
 * Code mainly inspired from
 *  https://github.com/nlohmann/crow/blob/master/include/thirdparty/curl_wrapper/curl_wrapper.hpp
 */

#ifndef CSENTRY_CURL_EZ_H
#define CSENTRY_CURL_EZ_H

#include <string.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "utils.h"

/* see: https://curl.haxx.se/libcurl/c/getinmemory.html */
struct memory_struct {
    char *data;
    size_t size;
};

static struct memory_struct null_memory_struct = {NULL, 0};

typedef struct {
    CURL *curl;
    struct curl_slist *headers;
    struct memory_struct chunk;     /* CURLOPT_WRITEDATA */
} curl_ez_t;

typedef struct {
    int status_code;
    char *data;
} curl_ez_reply;

static curl_ez_reply null_curl_ez_reply = {-1, NULL};

/** @return a CURLcode(CURLE_OK means success) */
#define curl_ez_setopt(ez, opt, param) ({           \
        assert_nonnull(ez);                         \
        curl_easy_setopt(ez->curl, opt, param);     \
    })

#ifdef __cplusplus
extern "C" {
#endif

curl_ez_t * _nullable curl_ez_new(void);
void curl_ez_free(curl_ez_t * _nullable);

CURLcode curl_ez_set_header(curl_ez_t *, const char *);

#define CURL_EZ_FLAG_HTTP_COMPRESS      0x1ULL

curl_ez_reply curl_ez_post(
    curl_ez_t *,
    const char *,
    const char * _nullable,
    size_t,
    uint64_t
);

curl_ez_reply curl_ez_post_json(
    curl_ez_t *,
    const char *,
    const cJSON *,
    uint64_t
);

#ifdef __cplusplus
}
#endif

curl_ez_t * _nullable curl_ez_new(void)
{
    curl_ez_t *ez = malloc(sizeof(*ez));
    CURLcode e;

    if (ez == NULL) goto out_exit;

    ez->curl = curl_easy_init();
    if (ez->curl == NULL) {
out_exit2:
        free(ez);
        ez = NULL;
        goto out_exit;
    }

    e = curl_ez_setopt(ez, CURLOPT_SSL_VERIFYPEER, 0L);
    if (e != CURLE_OK) {
        curl_easy_cleanup(ez->curl);
        goto out_exit2;
    }

    ez->headers = NULL;
    ez->chunk = null_memory_struct;

out_exit:
    return ez;
}

void curl_ez_free(curl_ez_t * _nullable ez)
{
    if (ez != NULL) {
        curl_slist_free_all(ez->headers);
        curl_easy_cleanup(ez->curl);
        assert(ez->chunk.data == null_memory_struct.data);
        assert(ez->chunk.size == null_memory_struct.size);
        free(ez);
    }
}

/**
 * Set header for a cURL handle
 * @return  CURLcode(CURLE_OK for success)
 * see: https://curl.haxx.se/libcurl/c/httpcustomheader.html
 */
CURLcode curl_ez_set_header(curl_ez_t *ez, const char *header)
{
    CURLcode e;
    struct curl_slist *list;

    assert_nonnull(ez);
    assert_nonnull(header);

    list = curl_slist_append(ez->headers, header);
    if (list != NULL) {
        ez->headers = list;
        e = curl_ez_setopt(ez, CURLOPT_HTTPHEADER, ez->headers);
    } else {
        e = CURLE_OUT_OF_MEMORY;
    }

    return e;
}

static size_t ez_post_write_cb(
        char *contents,
        size_t size,
        size_t nmemb,
        void *userdata)
{
    char *ptr;
    size_t n = size * nmemb;

    struct memory_struct *mem = (struct memory_struct *) userdata;
    assert_nonnull(mem);

    ptr = realloc(mem->data, mem->size + n + 1);
    if (ptr == NULL) return 0;

    mem->data = ptr;
    (void) memcpy(mem->data + mem->size, contents, n);
    mem->size += n;
    mem->data[mem->size] = '\0';

    return n;
}

/**
 * Perform cURL post with raw data
 * @return      Post reply, you're responsible to free the `data' field
 */
curl_ez_reply curl_ez_post(
        curl_ez_t *ez,
        const char *url,
        const char * _nullable data,
        size_t size,
        uint64_t flags)
{
    CURLcode e;
    int status_code;
    curl_ez_reply rep = null_curl_ez_reply;

    assert_nonnull(ez);
    assert_nonnull(url);
    assert(!!data | !size);

    if (flags & CURL_EZ_FLAG_HTTP_COMPRESS) {
        /*
         * see: https://en.wikipedia.org/wiki/HTTP_compression
         * TODO: NYI
         */
    } else {
        e = curl_ez_setopt(ez, CURLOPT_POSTFIELDS, data);
        if (e != CURLE_OK) goto out_exit;
        e = curl_ez_setopt(ez, CURLOPT_POSTFIELDSIZE, size);
        if (e != CURLE_OK) goto out_exit;
    }

    e = curl_ez_setopt(ez, CURLOPT_URL, url);
    if (e != CURLE_OK) goto out_exit;
    e = curl_ez_setopt(ez, CURLOPT_POST, 1);
    if (e != CURLE_OK) goto out_exit;
    e = curl_ez_setopt(ez, CURLOPT_WRITEFUNCTION, &ez_post_write_cb);
    if (e != CURLE_OK) goto out_exit;
    e = curl_ez_setopt(ez, CURLOPT_WRITEDATA, &ez->chunk);
    if (e != CURLE_OK) goto out_exit;

    e = curl_easy_perform(ez->curl);
    if (e != CURLE_OK) goto out_exit;

    e = curl_easy_getinfo(ez->curl, CURLINFO_RESPONSE_CODE, &status_code);
    if (e != CURLE_OK) goto out_exit;

    assert_nonnull(ez->chunk.data);
    assert(ez->chunk.size != 0);

    rep.data = strdup(ez->chunk.data);
    if (rep.data == NULL) goto out_exit;

    free(ez->chunk.data);
    ez->chunk = null_memory_struct;

    assert(status_code > 0);
    rep.status_code = status_code;
out_exit:
    return rep;
}

/**
 * Perform cURL post with json data
 * @return      Post reply, you're responsible to free the `data' field
 */
curl_ez_reply curl_ez_post_json(
        curl_ez_t *ez,
        const char *url,
        const cJSON *json,
        uint64_t flags)
{
    CURLcode e;
    curl_ez_reply rep = null_curl_ez_reply;
    char *data;

    assert_nonnull(ez);
    assert_nonnull(url);
    assert_nonnull(json);

    data = cJSON_Print(json);
    if (data == NULL) goto out_exit;

    e = curl_ez_set_header(ez, "Content-Type: application/json");
    if (e == CURLE_OK) {
        rep = curl_ez_post(ez, url, data, strlen(data), flags);
    }

    free(data);
out_exit:
    return rep;
}

#endif /* CSENTRY_CURL_EZ_H */

