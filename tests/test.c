//
// Created by Lei on 2019-07-06.
//

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "../include/csentry.h"
#include "../src/utils.h"

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
}

