/*
 * Created 190622 lynnl
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <uuid/uuid.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "utils.h"
#include "csentry.h"
#include "curl_ez.h"

typedef enum {
    HTTP_SCHEME = 0,
    HTTPS_SCHEME = 1,
} http_scheme;

typedef struct {
    const char *pubkey;
    const char *seckey;
    const char *store_url;
    cJSON *ctx;
    int sample_rate;
} csentry_t;

typedef struct {
    const char *str;
    ssize_t size;
} strbuf_t;

static strbuf_t STRBUF_NULL = {NULL, 0};

static const char *http_scheme_string[] = {
    "http://",
    "https://",
};

static int strbuf_eq(const strbuf_t *a, const strbuf_t *b)
{
    assert_nonnull(a);
    assert_nonnull(b);
    return a->size == b->size && a->str == b->str;
}

/**
 * @return      0 if success  -1 o.w.
 */
static int parse_keys(
        const char *str,
        strbuf_t *pubkey,
        strbuf_t *seckey)
{
    int e = 0;

    const char *colon;
    const char *at;

    assert_nonnull(str);
    assert_nonnull(pubkey);
    assert_nonnull(seckey);

    at = strchr(str, '@');
    if (at == NULL) set_err_jmp(-1, exit);

    colon = strchr(str, ':');
    if (colon != NULL) {
        if (colon < at) {
            pubkey->str = str;
            pubkey->size = colon - str;

            seckey->str = colon + 1;
            seckey->size = at - (colon + 1);
        } else {
            /* Likely matched [:PORT] part */
            goto out_no_seckey;
        }
    } else {
out_no_seckey:
        pubkey->str = str;
        pubkey->size = at - str;

        *seckey = STRBUF_NULL;
    }

out_exit:
    return e;
}

static int parse_host(const char *str, strbuf_t *host)
{
    int e = 0;
    const char *slash;

    assert_nonnull(str);
    assert_nonnull(host);

    slash = strchr(str, '/');
    if (slash == NULL) set_err_jmp(-1, exit);

    host->str = str;
    host->size = slash - str;

out_exit:
    return e;
}

static int parse_project_id(const char *str)
{
    int c;

    assert_nonnull(str);
    if (*str == '\0') return -1;

    while ((c = *str++) != '\0') {
        if (c < '0' || c > '9') return -1;
    }

    return 0;
}

/**
 * Parse DSN and populate required fields in Sentry client
 * @return      0 if success  -1 o.w.
 * see: https://docs.sentry.io/development/sdk-dev/overview/
 */
static int parse_dsn(csentry_t *client, const char *dsn)
{
    int e = 0;

    http_scheme scheme;
    strbuf_t pubkey;
    strbuf_t seckey;
    strbuf_t host;
    const char *projid;
    size_t n;

    assert_nonnull(client);
    assert_nonnull(dsn);

    if (strprefix(dsn, "http://")) {
        scheme = HTTP_SCHEME;
        dsn += STRLEN("http://");
    } else if (strprefix(dsn, "https://")) {
        scheme = HTTPS_SCHEME;
        dsn += STRLEN("https://");
    } else {
        set_err_jmp(-1, exit);
    }

    if (parse_keys(dsn, &pubkey, &seckey) != 0) {
        set_err_jmp(-1, exit);
    }

    if (strbuf_eq(&seckey, &STRBUF_NULL)) {
        dsn += pubkey.size + 1;                 /* +1 for '@' */
    } else {
        dsn += pubkey.size + seckey.size + 2;   /* +2 for ":@" */
    }

    if (parse_host(dsn, &host) != 0) {
        set_err_jmp(-1, exit);
    }
    dsn += host.size + 1;   /* +1 for '/' */

    if (parse_project_id(dsn) != 0) {
        set_err_jmp(-1, exit);
    }
    projid = dsn;

    printf("Scheme: %s\n", http_scheme_string[scheme]);
    printf("Pubkey: %.*s\n", (int) pubkey.size, pubkey.str);
    printf("Seckey: %.*s\n", (int) seckey.size, seckey.str);
    printf("Host: %.*s\n", (int) host.size, host.str);
    printf("Projid: %s\n", projid);

    client->pubkey = strndup(pubkey.str, pubkey.size);
    if (client->pubkey == NULL) set_err_jmp(-1, exit);

    if (seckey.str != NULL) {
        client->seckey = strndup(seckey.str, seckey.size);
        if (client->seckey == NULL) {
            free((void *) client->pubkey);
            set_err_jmp(-1, exit);
        }
    }

    n = strlen(http_scheme_string[scheme]) + host.size +
            STRLEN("/api/") + strlen(projid) + STRLEN("/store/") + 1;

    client->store_url = (char *) malloc(n);
    if (client->store_url == NULL) {
        free((void *) client->pubkey);
        free((void *) client->seckey);
        set_err_jmp(-1, exit);
    }

    (void) snprintf((char *) client->store_url, n, "%s%.*s/api/%s/store/",
            http_scheme_string[scheme], (int) host.size, host.str, projid);

    printf("> %s\n", client->pubkey);
    printf("> %s\n", client->seckey);
    printf("> %s\n", client->store_url);

out_exit:
    return e;
}

/**
 * DSN(Client key) format:
 *  SCHEME://PUBKEY[:SECKEY]@HOST[:PORT]/PROJECT_ID
 * The secret key is obsolete in newer DSN format(remain for compatible reason)
 */
void * _nullable csentry_new(
        const char *dsn,
        const cJSON * _nullable ctx,
        float sample_rate,
        int install_handlers)
{
    csentry_t *client = NULL;

    assert_nonnull(dsn);

    if (sample_rate < 0.0 || sample_rate > 1.0) {
        errno = EINVAL;
        goto out_exit;
    }

    client = (csentry_t *) malloc(sizeof(*client));
    if (client == NULL) goto out_exit;
    (void) memset(client, 0, sizeof(*client));

    if (parse_dsn(client, dsn) != 0) {
        errno = EDOM;
        free(client);
        client = NULL;
        goto out_exit;
    }

    csentry_ctx_clear(client);
    if (csentry_ctx_update(client, ctx) != 0) {
        errno = ENOTSUP;
        csentry_destroy(client);
        client = NULL;
        goto out_exit;
    }

    client->sample_rate = (int) (sample_rate * 100);
    printf("sample_rate: %d\n", client->sample_rate);
    printf("\n");

    if (install_handlers) {
        /* TODO */
    }

out_exit:
    return client;
}

void csentry_destroy(void *arg)
{
    csentry_t *client = (csentry_t *) arg;
    if (client != NULL) {
        free((void *) client->pubkey);
        free((void *) client->seckey);
        free((void *) client->store_url);
        cJSON_Delete(client->ctx);
        free(client);
    }
}

#define X_AUTH_HEADER_SIZE      256
#define SENTRY_PROTOCOL_VER     7

static void post_data(csentry_t *client)
{
    char xauth[X_AUTH_HEADER_SIZE];
    int n;
    assert_nonnull(client);

    /* TODO: ignore sentry_secret since it's obsoleted */
    if (client->seckey) {
        n = snprintf(xauth, X_AUTH_HEADER_SIZE,
                     "X-Sentry-Auth: "
                     "Sentry sentry_version=%d, "
                     "sentry_timestamp=%ld, "
                     "sentry_key=%s, "
                     "sentry_secret=%s, "
                     "sentry_client=%s/%s",
                     SENTRY_PROTOCOL_VER,
                     time(NULL), client->pubkey, client->seckey,
                     CSENTRY_NAME, CSENTRY_VERSION);
    } else {
        n = snprintf(xauth, X_AUTH_HEADER_SIZE,
                     "X-Sentry-Auth: "
                     "Sentry sentry_version=%d, "
                     "sentry_timestamp=%ld, "
                     "sentry_key=%s, "
                     "sentry_client=%s/%s",
                     SENTRY_PROTOCOL_VER,
                     time(NULL), client->pubkey,
                     CSENTRY_NAME, CSENTRY_VERSION);
    }

    assert(n < X_AUTH_HEADER_SIZE);

    printf("size: %d auth: %s\n", n, xauth);

    curl_ez_t *ez = curl_ez_new();
    assert_nonnull(ez);
    CURLcode e;
    e = curl_ez_set_header(ez, xauth);
    assert(e == CURLE_OK);

    curl_ez_reply rep = curl_ez_post_json(ez, client->store_url, client->ctx, 0);
    if (rep.status_code > 0) {
        printf("status code: %d\ndata: %s\n", rep.status_code, rep.data);
        free(rep.data);
    } else {
        printf("Cannot post message\n");
    }

    curl_ez_free(ez);
}

/**
 * char buf[1];
 * int n = vsnprintf(buf, 1, fmt, ap);
 * malloc(3) for size n+X(X >= 1) and vsnprintf() again
 * if malloc(3) fail try static buffer(set truncation attribute if possible)
 * the static buffer need a mutex
 *
 * Capture a message to Sentry server
 *
 * see: https://docs.sentry.io/development/sdk-dev/attributes/
 */
void csentry_capture_message(
        void *client0,
        const cJSON * _nullable attrs,
        int options,
        const char *msg)
{
    csentry_t *client = (csentry_t *) client0;
    uuid_t u;
    uuid_string_t uuid;
    char ts[ISO_8601_BUFSZ];

    assert_nonnull(client);
    assert_nonnull(msg);

    UNUSED(options, attrs);

    (void) cJSON_AddStringToObject(client->ctx, "message", msg);
    uuid_generate(u);
    uuid_unparse_lower(u, uuid);
    (void) cJSON_AddStringToObject(client->ctx, "event_id", uuid);

    (void) cJSON_AddStringToObject(client->ctx, "logger", "builtin");
    (void) cJSON_AddStringToObject(client->ctx, "platform", "c");
    format_iso_8601_time(ts);
    (void) cJSON_AddStringToObject(client->ctx, "timestamp", ts);

    cJSON *sdk = cJSON_CreateObject();
    assert_nonnull(sdk);
    (void) cJSON_AddStringToObject(sdk, "name", CSENTRY_NAME);
    (void) cJSON_AddStringToObject(sdk, "version", CSENTRY_VERSION);

    cJSON_AddItemReferenceToObject(client->ctx, "sdk", sdk);

    char *str = cJSON_Print(client->ctx);
    printf("%s\n", str);
    free(str);

    post_data(client);
}

/**
 * @return      1 if added/updated 0 otherwise
 */
static int csentry_ctx_update0(
        void *client0,
        const char *name,
        const cJSON * _nullable data)
{
    csentry_t *client = (csentry_t *) client0;
    cJSON * _nullable copy;

    assert_nonnull(client);
    assert_nonnull(name);

    if (data == NULL) return 0;

    copy = cJSON_Duplicate(data, 1);
    /* if copy is NULL, cJSON_AddItemToObject() will do nothing */

    if (cJSON_GetObjectItem(client->ctx, name) == NULL) {
        cJSON_AddItemToObject(client->ctx, name, copy);
    } else {
        /* Replace won't success if name don't exist */
        cJSON_ReplaceItemInObject(client->ctx, name, copy);
    }

    if (cJSON_GetObjectItem(client->ctx, name) == NULL) {
        cJSON_Delete(copy);     /* Prevent potential memory leakage */
        return 0;
    }

    return 1;
}

static int is_known_ctx_name(const char *name)
{
    assert_nonnull(name);
    return !strcmp(name, "user") ||
            !strcmp(name, "tags") ||
            !strcmp(name, "extra");
}

/**
 * Merge Sentry context json into cSentry client
 *
 * @client0     An opaque cSentry client handle
 * @ctx         Sentry context json
 * @return      0 if success -1 o.w.(errno will be set)
 *              EINVAL if ctx isn't JSON object
 *              ENOTSUP if there is any unidentified context name
 *
 * see:
 *  https://github.com/DaveGamble/cJSON/issues/167
 *  https://docs.sentry.io/enriching-error-data/context/
 */
int csentry_ctx_update(void *client0, const cJSON * _nullable ctx)
{
    int e = 0;
    csentry_t *client = (csentry_t *) client0;
    cJSON *iter;

    assert_nonnull(client);
    if (ctx == NULL) goto out_exit;

    if (!cJSON_IsObject(ctx)) {
        e = -1;
        errno = EINVAL;
        goto out_exit;
    }

    cJSON_ArrayForEach(iter, ctx) {
        if (iter->string == NULL) continue;

        printf("%s\n", iter->string);

        if (is_known_ctx_name(iter->string)) {
            (void) csentry_ctx_update0(client, iter->string, iter);
            printf("Merging %s into cSentry context\n", iter->string);
        } else if (!strcmp(iter->string, "level")) {
            /* Level is ignored, it make sense only for posting message */
            continue;
        } else {
#if 0
            e = -1;
            errno = ENOTSUP;
            break;
#endif
        }
    }

out_exit:
    return e;
}

int csentry_ctx_update_user(void *client0, const cJSON * _nullable data)
{
    return csentry_ctx_update0(client0, "user", data);
}

int csentry_ctx_update_tags(void *client0, const cJSON * _nullable data)
{
    return csentry_ctx_update0(client0, "tags", data);
}

int csentry_ctx_update_extra(void *client0, const cJSON * _nullable data)
{
    return csentry_ctx_update0(client0, "extra", data);
}

const cJSON *csentry_ctx_get(void *client0)
{
    csentry_t *client = (csentry_t *) client0;
    assert_nonnull(client);
    /* TODO: remove message, event_id, logger, platform, timestamp, sdk attrs */
    return client->ctx;
}

void csentry_ctx_clear(void *client0)
{
    csentry_t *client = (csentry_t *) client0;
    assert_nonnull(client);

    cJSON_Delete(client->ctx);
    client->ctx = cJSON_CreateObject();
    assert_nonnull(client->ctx);
}

int main(void)
{
    void *d = csentry_new("https://a267a83de2c4a2d80bc41f91d8ef38@sentry.io:80/159723608", NULL, 1.0, 0);
    assert_nonnull(d);

    cJSON *json = cJSON_Parse("{\"array\":[1,2,3],\"boolean\":true,\"color\":\"#82b92c\",\"level\":null,\"number\":123,\"user\":{\"a\":\"b\",\"c\":\"d\",\"e\":\"f\"},\"string\":\"HelloWorld\"}");
    assert_nonnull(json);

    int e;
    e = csentry_ctx_update(d, json);
    if (e != 0) {
        printf("%d\n", errno);
    } else {
        char *str = cJSON_Print(csentry_ctx_get(d));
        assert_nonnull(str);
        printf("%s\n", str);
        free(str);
    }

    exit(0);

    void *d1 = csentry_new("https://a267a83de2c4a2d80bc41f91d8ef38@sentry.io:80/159723608", NULL, 1.0, 0);
    void *d2 = csentry_new("http://93ea558ffecdcee3ca9e7fab8927:be7e8d34da87071eb8c36eab55460f98@sentry.io:8080/159723482", NULL, 0, 0);

    csentry_capture_message(d1, NULL, 0, "hello");
    csentry_capture_message(d2, NULL, 0, "world");
    return 0;
}

