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

static strbuf_t STRBUF_NULL = {NULL, -1};

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
    if (at == NULL) {
        set_err_jmp(-1, exit);
    }

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

static csentry *parse_dsn(const char *dsn)
{
    http_scheme scheme;
    strbuf_t pubkey;
    strbuf_t seckey;
    strbuf_t host;
    uint64_t projid;
    csentry *client = NULL;

    assert_nonnull(dsn);

    if (strprefix(dsn, "http://")) {
        scheme = HTTP_SCHEME;
        dsn += STRLEN("http://");
    } else if (strprefix(dsn, "https://")) {
        scheme = HTTPS_SCHEME;
        dsn += STRLEN("https://");
    } else {
        errno = EDOM;
        goto out_exit;
    }

    if (parse_keys(dsn, &pubkey, &seckey) != 0) {
        errno = EDOM;
        goto out_exit;
    }

    if (strbuf_eq(&seckey, &STRBUF_NULL)) {
        dsn += pubkey.size + 1;                 /* +1 for '@' */
    } else {
        dsn += pubkey.size + seckey.size + 2;   /* +2 for ":@" */
    }

    /* TODO: parse host[:port] and projid */

out_exit:
    return client;
}

/**
 * DSN(Client key) format:
 *  SCHEME://PUBKEY[:SECKEY]@HOST[:PORT]/PROJECT_ID
 * The secret key is obsolete in newer DSN format(remain for compatible reason)
 */
void *csntry_new(char *dsn)
{

    return NULL;
}

int main(void)
{
    printf("hello world");
    return 0;
}
