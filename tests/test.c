/*
 * Created 190706 lynnl
 *
 * Test code for cSentry client
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../include/csentry.h"
#include "../src/utils.h"

#define LOG(fmt, ...)       (void) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   (void) fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__)
#ifdef DEBUG
#define LOG_DBG(fmt, ...)   (void) printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#endif

static void baseline_test(void)
{
    void *handle;
    uuid_t u;
    uuid_string_t uu1;
    uuid_string_t uu2;
    const cJSON *ctx;
    char *ctx_str;

    handle = csentry_new("", NULL, 1.0, 0);
    assert(handle == NULL);
    csentry_debug(handle);
    csentry_destroy(handle);

    handle = csentry_new("http://35fe8d8277744ef7925c4784cb2e1d39:a5f54e0ce4774d1b968d612eca4131df@sentry.io:8080/123", NULL, 0.25, 0);
    assert_nonnull(handle);
    csentry_debug(handle);
    csentry_destroy(handle);

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.9, 0);
    assert_nonnull(handle);
    csentry_debug(handle);
    csentry_ctx_clear(handle);

    csentry_capture_message(handle, NULL, CSENTRY_LEVEL_DEBUG, "hello world");
    csentry_get_last_event_id(handle, u);
    csentry_get_last_event_id_string(handle, uu1);
    uuid_unparse_lower(u, uu2);
    assert(!strcmp(uu1, uu2));
    LOG("Last event id: %s", uu1);
    csentry_debug(handle);

    ctx = csentry_ctx_get(handle);
    assert_nonnull(ctx);
    ctx_str = cJSON_Print(ctx);
    LOG("ctx: %p %s", ctx, ctx_str);
    free(ctx_str);

    csentry_destroy(handle);
}

/*
 * see: https://jsonformatter.curiousconcept.com/
 */
static void baseline_test_v2(void)
{
    void *handle;
    cJSON *attrs;

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.5, 0);
    assert_nonnull(handle);

    attrs = cJSON_Parse("{\"logger\":\"custom_logger\"}");
    assert_nonnull(attrs);
    csentry_capture_message(handle, attrs, 0, "Test message #1");
    cJSON_Delete(attrs);

    attrs = cJSON_Parse("{\"logger\":\"_logger\",\"context\":{\"user\":{\"name\":\"lynnl\",\"uid\":1000}}}");
    assert_nonnull(attrs);
    csentry_capture_message(handle, attrs, 0, "Test message #2");
    cJSON_Delete(attrs);

    attrs = cJSON_Parse("{\"logger\":\"luke\",\"context\":{\"user\":{\"gid\":50,\"home\":\"/home/lynnl\",\"env\":[\"USER\",\"PATH\",\"LANG\",\"SHELL\",\"PWD\"]}}}");
    assert_nonnull(attrs);
    csentry_capture_message(handle, attrs, 0, "Test message #3");
    cJSON_Delete(attrs);

    attrs = cJSON_Parse("{\"context\":{\"tags\":{\"os\":\"Linux\",\"locale\":\"en_US.UTF-8\",\"hostname\":\"lynnl-hub\",\"early_access\":true,\"hit_counter\":5931}}}");
    assert_nonnull(attrs);
    csentry_capture_message(handle, attrs, 0, "Test message #4");
    cJSON_Delete(attrs);

    attrs = cJSON_Parse("{\"context\":{\"extra\":{\"network\":\"wifi\",\"ping\":38,\"paid_user\":true}}}");
    assert_nonnull(attrs);
    csentry_capture_message(handle, attrs, 0, "Test message #5");
    cJSON_Delete(attrs);

    csentry_destroy(handle);
}

static void ctx_test_v1(void)
{
    void *handle;
    cJSON *attrs;

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.5, 0);
    assert_nonnull(handle);

    attrs = cJSON_Parse("{\"shell\":\"bash\",\"flags\":2081,\"any_error\":true,\"array\":[1,\"foobar\",true]}");
    assert_nonnull(attrs);
    csentry_ctx_update_user(handle, attrs);
    csentry_capture_message(handle, NULL, 0, "Context test v1, message #1");
    cJSON_Delete(attrs);

    csentry_destroy(handle);
}

static void ctx_test_v2(void)
{
    void *handle;
    cJSON *attrs;

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.8, 0);
    assert_nonnull(handle);

    attrs = cJSON_Parse("{\"logger\":\"bird\",\"country_code\":86,\"locale_dst\":false}");
    assert_nonnull(attrs);
    csentry_ctx_update_tags(handle, attrs);
    csentry_capture_message(handle, NULL, 0, "Context test v2, message #1");
    cJSON_Delete(attrs);

    csentry_destroy(handle);
}

static void ctx_test_v3(void)
{
    void *handle;
    cJSON *attrs;

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.8, 0);
    assert_nonnull(handle);

    attrs = cJSON_Parse("{\"commit\":\"1c1edeb4ec4ac20f8f25e38af5cbc87ab4d29d8a\",\"version\":20190711,\"stable\":true}");
    assert_nonnull(attrs);
    csentry_ctx_update_extra(handle, attrs);
    csentry_capture_message(handle, NULL, 0, "Context test v3, message #1");
    cJSON_Delete(attrs);

    csentry_destroy(handle);
}

static void ctx_test_v4(void)
{
    void *handle;
    cJSON *attrs;

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.8, 0);
    assert_nonnull(handle);

    attrs = cJSON_Parse("{\"user\":{\"name\":\"lynnl\"},\"tags\":{\"root\":false},\"extra\":{\"uptime_secs\":3041},\"unrecognized\":{\"foo\":\"bar\"}}");
    assert_nonnull(attrs);
    csentry_ctx_update(handle, attrs);
    csentry_capture_message(handle, NULL, 0, "Context test v4, message #1");
    cJSON_Delete(attrs);

    csentry_destroy(handle);
}

static void breadcrumb_test(void)
{
    void *handle;

    handle = csentry_new("https://eeadde0381684a339597770ce54b4c66@sentry.io/1489851", NULL, 0.9, 0);
    assert_nonnull(handle);

    csentry_add_breadcrumb(handle, NULL, 0, "Event #1");
    csentry_add_breadcrumb(handle, NULL, CSENTRY_LEVEL_WARN, "Event #2");
    csentry_add_breadcrumb(handle, NULL, CSENTRY_LEVEL_DEBUG | CSENTRY_BC_TYPE_HTTP, "Event #3");
    csentry_capture_message(handle, NULL, 0, "Cannot get some info...");

    csentry_capture_message(handle, NULL, CSENTRY_LEVEL_INFO, "A plain info msg");

    csentry_debug(handle);

    csentry_destroy(handle);
}

int main(void)
{
    LOG_DBG("Debug build");

    UNUSED(baseline_test, breadcrumb_test);
    //baseline_test();
    //breadcrumb_test();

    //baseline_test_v2();
    UNUSED(baseline_test_v2);

    //ctx_test_v1();
    UNUSED(ctx_test_v1);

    //ctx_test_v2();
    UNUSED(ctx_test_v2);

    //ctx_test_v3();
    UNUSED(ctx_test_v3);

    ctx_test_v4();

    LOG("Pass!");
    return 0;
}

