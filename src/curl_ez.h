/*
 * Created 190623 lynnl
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

#define curl_ez_setopt(ez, opt, param) ({           \
        assert_nonnull(ez);                         \
        curl_easy_setopt(ez->curl, opt, param);     \
    })

#ifdef __cplusplus
extern "C" {
#endif

curl_ez_t * _nullable curl_ez_new(void);
void curl_ez_free(curl_ez_t * _nullable);

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
    (void) memcpy(mem->data + size, contents, n);
    mem->size += n;
    mem->data[mem->size] = '\0';

    return n;
}

curl_ez_reply curl_ez_post(
        const curl_ez_t *ez,
        const char *url,
        const char *data,
        size_t size,
        int compress)
{
    CURLcode e;
    int status_code;
    curl_ez_reply rep = {-1, NULL};

    assert_nonnull(ez);
    assert_nonnull(url);
    assert_nonnull(data);

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
    e = curl_ez_setopt(ez, CURLOPT_WRITEFUNCTION, ez_post_write_cb);
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

#endif /* CSENTRY_CURL_EZ_H */

