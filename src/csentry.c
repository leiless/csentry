/*
 * Created 190622 lynnl
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

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
    int sample_rate;

    cJSON *ctx;
    uuid_t last_event_id;
    pthread_mutex_t mtx;    /* Protects ctx and last_event_id */
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

/*
 * static pthread mutex initialization always success by nature
 * see: https://stackoverflow.com/questions/14320041/pthread-mutex-initializer-vs-pthread-mutex-init-mutex-param
 */
static pthread_mutex_t builtin_mutex = PTHREAD_MUTEX_INITIALIZER;

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

    client->mtx = builtin_mutex;

    csentry_ctx_clear(client);
    if (csentry_ctx_update(client, ctx) != 0) {
        errno = ENOTSUP;
        csentry_destroy(client);
        client = NULL;
        goto out_exit;
    }

    client->sample_rate = (int) (sample_rate * 100);
    printf("> sample_rate: %d\n", client->sample_rate);
    printf("\n");

    if (install_handlers) {
        /* TODO: capture sginals */
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

        /* TODO: seems no need to protect ctx-freeing */
        //pmtx_lock(&client->mtx);
        cJSON_Delete(client->ctx);
        //pmtx_unlock(&client->mtx);

        free(client);
    }
}

/**
 * Print internal cSentry objects to stderr(debug purpose)
 */
void csentry_debug(void *client0)
{
    csentry_t *client = (csentry_t *) client0;
    uuid_string_t uu;
    char *ctx;

    if (client == NULL) {
        fprintf(stderr, "%s() called with NULL argument, do nop.\n", __func__);
        return;
    }

    uuid_unparse_lower(client->last_event_id, uu);
    ctx = cJSON_Print(client->ctx);

    fprintf(stderr, "cSentry handle: %p\n"
                    "\tpubkey: %s\n"
                    "\tseckey: %s\n"
                    "\tstore_url: %s\n"
                    "\tsample_rate: %d\n"
                    "\tctx: %s\n"
                    "\tlast_event_id: %s\n"
                    "\tmtx: %p\n",
            client, client->pubkey, client->seckey,
            client->store_url, client->sample_rate,
            ctx, uu, &client->mtx);

    free(ctx);
}

static void update_last_event_id(csentry_t *client, const curl_ez_reply *rep)
{
    cJSON *json;
    cJSON *id;
    const char *value;
    uuid_t uu;

    assert_nonnull(client);
    assert_nonnull(rep);

    if (rep->status_code != 200) return;
    assert_nonnull(rep->data);

    json = cJSON_Parse(rep->data);
    if (json == NULL) return;

    id = cJSON_GetObjectItem(json, "id");
    if (id != NULL) {
        value = cJSON_GetStringValue(id);
        if (value != NULL && uuid_parse32(value, uu) == 0) {
            /* XXX: client->mtx already locked previously */
            (void) memcpy(client->last_event_id, uu, sizeof(uuid_t));
        }
    }

    cJSON_Delete(json);
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
        update_last_event_id(client, &rep);

        printf("status code: %d\ndata: %s\n", rep.status_code, rep.data);
        free(rep.data);
    } else {
        printf("Cannot post message\n");
    }

    curl_ez_free(ez);
}

static const char *sentry_levels[] = {
    /* Default level is error */
    "error", "debug", "info", "warning", "fatal",
};

#define OPTIONS_TO_LEVEL(opt)   ((opt) >> 29)

static void msg_set_level_attr0(cJSON *json, uint32_t i)
{
    assert_nonnull(json);
    if (i < ARRAY_SIZE(sentry_levels)) {
        (void) cjson_add_or_update_str_to_obj(json, "level", sentry_levels[i]);
    }
}

static void msg_set_level_attr(csentry_t *client, uint32_t options)
{
    uint32_t i = OPTIONS_TO_LEVEL(options);
    assert_nonnull(client);
    /* Default level is error, we'll skip it since it's optinal */
    if (i != 0) msg_set_level_attr0(client->ctx, i);
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
        uint32_t options,
        const char *format,
        ...)
{
    csentry_t *client = (csentry_t *) client0;
    uuid_t u;
    uuid_string_t uuid;
    char ts[ISO_8601_BUFSZ];
    cJSON *sdk;
    cJSON *json;
    va_list ap;
    int sz;
    int sz2;
    char *msg;

    assert_nonnull(client);
    assert_nonnull(format);

out_toctou:
    va_start(ap, format);
    sz = vsnprintf(NULL, 0, format, ap);
    va_end(ap);

    if (sz > 0) {
        msg = (char *) malloc(sz + 1);
        if (msg != NULL) {
            va_start(ap, format);
            sz2 = vsnprintf(msg, sz + 1, format, ap);
            va_end(ap);

            if (sz2 > sz) {
                free(msg);
                goto out_toctou;
            } else if (sz2 < 0) {
                free(msg);
                msg = (char *) format;
            }
        } else {
            msg = (char *) format;
        }
    } else {
        msg = (char *) format;
    }

    pmtx_lock(&client->mtx);

    msg_set_level_attr(client, options);

    (void) cjson_add_or_update_str_to_obj(client->ctx, "message", msg);
    uuid_generate(u);
    uuid_unparse_lower(u, uuid);
    /*
     * [sic] Hexadecimal string representing a uuid4 value.
     * The length is exactly 32 characters. Dashes are not allowed.
     *
     * XXX: as tested, uuid string with dashes is acceptable for Sentry server
     */
    (void) cjson_add_or_update_str_to_obj(client->ctx, "event_id", uuid);

    format_iso_8601_time(ts);
    (void) cjson_add_or_update_str_to_obj(client->ctx, "timestamp", ts);

    (void) cjson_set_default_str_to_obj(client->ctx, "logger", "(builtin)");
    (void) cjson_set_default_str_to_obj(client->ctx, "platform", "c");

    sdk = cJSON_GetObjectItem(client->ctx, "sdk");
    if (sdk == NULL) {
        sdk = cJSON_CreateObject();
        if (sdk != NULL) {
            (void) cJSON_AddStringToObject(sdk, "name", CSENTRY_NAME);
            (void) cJSON_AddStringToObject(sdk, "version", CSENTRY_VERSION);
            cJSON_AddItemToObject(client->ctx, "sdk", sdk);
        }
    } else {
        (void) cjson_set_default_str_to_obj(sdk, "name", CSENTRY_NAME);
        (void) cjson_set_default_str_to_obj(sdk, "version", CSENTRY_VERSION);
    }

    if (cJSON_IsObject(attrs)) {
        json = cJSON_GetObjectItem(attrs, "logger");
        if (cJSON_IsString(json)) {
            (void) cjson_add_or_update_str_to_obj_x(client->ctx, "logger", cJSON_GetStringValue(json));
        }

        json = cJSON_GetObjectItem(attrs, "context");
        if (json != NULL) (void) csentry_ctx_update(client, json);
    }

    char *str = cJSON_Print(client->ctx);
    printf("%s\n", str);
    free(str);

    post_data(client);
    cJSON_DeleteItemFromObject(client->ctx, "breadcrumbs");

    /* TODO: remove temporary attributes in `attr' */

    pmtx_unlock(&client->mtx);

    if (msg != format) free(msg);
}

static void breadcrumb_set_level_attr(cJSON *breadcrumb, uint32_t options)
{
    uint32_t i = OPTIONS_TO_LEVEL(options);
    assert_nonnull(breadcrumb);

    /* Switch position of error and info */
    if (i == 0 || i == 2) i ^= 2;

    /* Default breadcrumb level is info */
    if (i != 2) msg_set_level_attr0(breadcrumb, i);
}

static const char *breadcrumb_types[] = {
    /* Default breadcrumb type is "default" */
    "default", "http", "error",
};

#define OPTIONS_TO_TYPE(opt)    (((opt) >> 27) & 0x3)

static void breadcrumb_set_type_attr(cJSON *breadcrumb, uint32_t options)
{
    uint32_t i = OPTIONS_TO_TYPE(options);
    assert_nonnull(breadcrumb);
    if (i > 0 && i < ARRAY_SIZE(breadcrumb_types)) {
        (void) cJSON_AddStringToObject(breadcrumb, "type", breadcrumb_types[i]);
    }
}

/**
 * see: https://docs.sentry.io/enriching-error-data/breadcrumbs/?platform=csharp
 */
void csentry_add_breadcrumb(
        void *client0,
        const cJSON * _nullable attrs,
        uint32_t options,
        const char *msg)
{
    csentry_t *client = (csentry_t *) client0;
    cJSON *breadcrumb;
    uuid_string_t uuid;
    char ts[ISO_8601_BUFSZ];
    cJSON *json;
    cJSON *cp;
    cJSON *values;
    cJSON *arr;

    assert_nonnull(client);
    assert_nonnull(msg);

    breadcrumb = cJSON_CreateObject();
    if (breadcrumb == NULL) return;     /* TODO */

    uuid_string_random(uuid);

    (void) cJSON_AddStringToObject(breadcrumb, "message", msg);
    (void) cJSON_AddStringToObject(breadcrumb, "event_id", uuid);

    format_iso_8601_time(ts);
    (void) cJSON_AddStringToObject(breadcrumb, "timestamp", ts);

    (void) cJSON_AddStringToObject(breadcrumb, "category", "(builtin)");

    breadcrumb_set_level_attr(breadcrumb, options);
    breadcrumb_set_type_attr(breadcrumb, options);

    if (cJSON_IsObject(attrs)) {
        json = cJSON_GetObjectItem(attrs, "category");
        if (cJSON_IsString(json)) {
            cp = cJSON_Duplicate(json, 1);
            if (!cjson_add_or_update_object(breadcrumb, "category", cp)) {
                cJSON_Delete(cp);
            }
        }

        /* Level and type in context will be ignored */

        json = cJSON_GetObjectItem(attrs, "data");
        if (json != NULL) {
            cp = cJSON_Duplicate(json, 1);
            if (!cjson_add_or_update_object(breadcrumb, "data", cp)) {
                cJSON_Delete(cp);
            }
        }
    }

    pmtx_lock(&client->mtx);

    json = cJSON_GetObjectItem(client->ctx, "breadcrumbs");
    if (cJSON_IsObject(json)) {
        values = cJSON_GetObjectItem(json, "values");

        if (cJSON_IsArray(values)) {
            cJSON_AddItemToArray(values, breadcrumb);
        } else if (values != NULL) {
            cJSON_ReplaceItemInObject(json, "values", breadcrumb);
        } else {
            goto out_add_values;
        }
    } else if (json == NULL) {
out_add_values:
        values = cJSON_CreateObject();
        arr = cJSON_CreateArray();

        if (values != NULL && arr != NULL) {
            cJSON_AddItemToArray(arr, breadcrumb);   /* Always success */

            if (!cjson_add_object(values, "values", arr)) {
                /* `breadbrumb' is ready attached to `arr' */
                cJSON_Delete(arr);
                cJSON_Delete(values);
                goto out_unlock;
            }

            if (!cjson_add_or_update_object(client->ctx, "breadcrumbs", values)) {
                /* `arr' is ready attached to `values' */
                cJSON_Delete(values);
                goto out_unlock;
            }
        } else {
            cJSON_Delete(arr);
            cJSON_Delete(values);
            cJSON_Delete(breadcrumb);
        }
    }

out_unlock:
    pmtx_unlock(&client->mtx);
}

void csentry_get_last_event_id(void *client0, uuid_t uuid)
{
    csentry_t *client = (csentry_t *) client0;
    assert_nonnull(client);
    assert_nonnull(uuid);
    pmtx_lock(&client->mtx);
    (void) memcpy(uuid, client->last_event_id, sizeof(uuid_t));
    pmtx_unlock(&client->mtx);
}

void csentry_get_last_event_id_string(void *client0, uuid_string_t out)
{
    csentry_t *client = (csentry_t *) client0;
    assert_nonnull(client);
    assert_nonnull(out);
    pmtx_lock(&client->mtx);
    uuid_unparse_lower(client->last_event_id, out);
    pmtx_unlock(&client->mtx);
}

/**
 * @param data  if data is NULL, corresponding `name' in context will be deleted
 * @return      1 if Sentry context has been modified
 */
static int csentry_ctx_update0(
        void *client0,
        const char *name,
        const cJSON * _nullable data)
{
    int dirty = 0;
    csentry_t *client = (csentry_t *) client0;
    cJSON *name_json;
    cJSON *iter;
    cJSON * _nullable copy;

    assert_nonnull(client);
    assert_nonnull(name);

    /* client->mtx already locked */

    name_json = cJSON_GetObjectItem(client->ctx, name);
    if (name_json != NULL) {
        if (data == NULL) {
            cJSON_DeleteItemFromObject(client->ctx, name);
            dirty = cJSON_GetObjectItem(client->ctx, name) == NULL;
        }

        /* if `data' is NULL, below cJSON_ArrayForEach() do nop */
        cJSON_ArrayForEach(iter, data) {
            if (iter->string == NULL) continue;

            copy = cJSON_Duplicate(iter, 1);

            if (cjson_add_or_update_object(name_json, iter->string, copy)) {
                dirty = 1;
            } else {
                cJSON_Delete(copy);
            }
        }
    } else {
        copy = cJSON_Duplicate(data, 1);

        if (cjson_add_object(client->ctx, name, copy)) {
            dirty = 1;
        } else {
            cJSON_Delete(copy);
        }
    }

    return dirty;
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
    if (ctx == NULL) {
        (void) csentry_ctx_update_user(client0, NULL);
        (void) csentry_ctx_update_tags(client0, NULL);
        (void) csentry_ctx_update_extra(client0, NULL);
        goto out_exit;
    }

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
        } else {
            /* Unknown context names will be simply ignored */
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

/*
 * XXX: access context in multithreading environment is dangerous
 */
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

    pmtx_lock(&client->mtx);
    cJSON_Delete(client->ctx);
    client->ctx = cJSON_CreateObject();
    assert_nonnull(client->ctx);
    pmtx_unlock(&client->mtx);
}

