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

typedef struct {
    CURL *curl;
    struct curl_slist *headers;
    struct memory_struct chunk;     /* CURLOPT_WRITEDATA */
} curl_ez_t;

typedef struct {
    int status_code;
    char *data;
} curl_ez_reply;

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

curl_ez_reply curl_ez_post(
    const curl_ez_t *,
    const char *,
    const char * _nullable,
    size_t,
    int
);

curl_ez_reply curl_ez_post_json(
    curl_ez_t *,
    const char *,
    const cJSON *,
    int
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
    (void) memset(&ez->chunk, 0, sizeof(ez->chunk));

out_exit:
    return ez;
}

void curl_ez_free(curl_ez_t * _nullable ez)
{
    if (ez != NULL) {
        curl_slist_free_all(ez->headers);
        curl_easy_cleanup(ez->curl);
    }
}

/**
 * Set header for a cURL handle
 * @return  CURLcode(CURLE_OK for success)
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
 * @return      Post reply, you're responsible to free the `data'
 */
curl_ez_reply curl_ez_post(
        const curl_ez_t *ez,
        const char *url,
        const char * _nullable data,
        size_t size,
        int compress)
{
    CURLcode e;
    int status_code;
    curl_ez_reply rep = {-1, NULL};

    assert_nonnull(ez);
    assert_nonnull(url);
    assert(!!data || !size);

    if (compress) {
        /* TODO */
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

    rep.data = strdup(ez->chunk.data);
    if (rep.data == NULL) goto out_exit;

    rep.status_code = status_code;
out_exit:
    return rep;
}

/**
 * Perform cURL post with json data
 */
curl_ez_reply curl_ez_post_json(
        curl_ez_t *ez,
        const char *url,
        const cJSON *json,
        int compress)
{
    CURLcode e;
    curl_ez_reply rep = {-1, NULL};
    char *data;

    assert_nonnull(ez);
    assert_nonnull(url);
    assert_nonnull(json);

    data = cJSON_Print(json);
    if (data == NULL) goto out_exit;

    /* TODO: check if Content-Type already set */
    e = curl_ez_set_header(ez, "Content-Type: application/json");
    if (e == CURLE_OK) {
        rep = curl_ez_post(ez, url, data, strlen(data), compress);
    }

    free(data);
out_exit:
    return rep;
}

#endif /* CSENTRY_CURL_EZ_H */

