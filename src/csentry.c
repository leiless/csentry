/*
 * Created 190622 lynnl
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "csentry.h"

typedef enum {
    HTTP_SCHEME = 0,
    HTTPS_SCHEME = 1,
} http_scheme;

typedef struct {
    const char *pubkey;
    const char *seckey;
    const char *store_url;
} csentry;

typedef struct {
    const char *str;
    ssize_t size;
} strbuf_t;

static strbuf_t STRBUF_NULL = {NULL, 0};

static const char *http_scheme_string[] = {
        "http://",
        "https://",
};

static inline int strbuf_eq(const strbuf_t *a, const strbuf_t *b)
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

static csentry *parse_dsn(const char *dsn)
{
    http_scheme scheme;
    strbuf_t pubkey;
    strbuf_t seckey;
    strbuf_t host;
    const char *projid;
    csentry *client = NULL;

    assert_nonnull(dsn);

    if (strprefix(dsn, "http://")) {
        scheme = HTTP_SCHEME;
        dsn += STRLEN("http://");
    } else if (strprefix(dsn, "https://")) {
        scheme = HTTPS_SCHEME;
        dsn += STRLEN("https://");
    } else {
        set_errno_and_jump(EDOM, out_exit);
    }

    if (parse_keys(dsn, &pubkey, &seckey) != 0) {
        set_errno_and_jump(EDOM, out_exit);
    }

    if (strbuf_eq(&seckey, &STRBUF_NULL)) {
        dsn += pubkey.size + 1;                 /* +1 for '@' */
    } else {
        dsn += pubkey.size + seckey.size + 2;   /* +2 for ":@" */
    }

    if (parse_host(dsn, &host) != 0) {
        set_errno_and_jump(EDOM, out_exit);
    }
    dsn += host.size + 1;   /* +1 for '/' */

    if (parse_project_id(dsn) != 0) {
        set_errno_and_jump(EDOM, out_exit);
    }
    projid = dsn;

    printf("Scheme: %s\n", http_scheme_string[scheme]);
    printf("Pubkey: %.*s\n", (int) pubkey.size, pubkey.str);
    printf("Seckey: %.*s\n", (int) seckey.size, seckey.str);
    printf("Host: %.*s\n", (int) host.size, host.str);
    printf("Projid: %s\n", projid);
    printf("\n");

out_exit:
    return client;
}

/**
 * DSN(Client key) format:
 *  SCHEME://PUBKEY[:SECKEY]@HOST[:PORT]/PROJECT_ID
 * The secret key is obsolete in newer DSN format(remain for compatible reason)
 */
void *csentry_new(char *dsn)
{
    assert_nonnull(dsn);
    parse_dsn(dsn);
    return NULL;
}

int main(void)
{
    csentry_new("https://a267a83de2c4a2d80bc41f91d8ef38@sentry.io:80/159723608");
    csentry_new("http://93ea558ffecdcee3ca9e7fab8927:be7e8d34da87071eb8c36eab55460f98@sentry.io:8080/159723482");
    return 0;
}

