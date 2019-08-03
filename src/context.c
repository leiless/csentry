/*
 * Created 190803 lynnl
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/utsname.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#if defined(BSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include "log.h"
#include "utils.h"
#include "constants.h"
#include "context.h"

/*
 * Get kernel os type string
 */
static ssize_t get_kernel_name(char *buf, size_t sz)
{
    assert_nonnull(buf);

#if defined(__APPLE__)
    /* macOS is more meaningful instead of Darwin */
    return snprintf(buf, sz, "macOS");
#elif defined(__linux__)
    return snprintf(buf, sz, "Linux");
#elif define(BSD)
    int mib[2] = {CTL_KERN, KERN_OSTYPE};
    if (sysctl(mib, ARRAY_SIZE(mib), buf, &sz, NULL, 0) == 0) {
        return sz;
    }
    return snprintf(buf, sz, "BSD*");
#else
#pragma GCC error "Unsupported operating system!"
#endif
}

#ifdef __APPLE__
/*
 * Private #include <CoreFoundation/CFPriv.h>
 * see: DarwinTools/sw_vers.c
 */
CF_EXPORT CFDictionaryRef _CFCopyServerVersionDictionary(void);
CF_EXPORT CFDictionaryRef _CFCopySystemVersionDictionary(void);
CF_EXPORT const CFStringRef _kCFSystemVersionProductVersionKey;
CF_EXPORT const CFStringRef _kCFSystemVersionBuildVersionKey;
#endif

/**
 * Get short kernel version string
 * see: https://gist.github.com/anders/5105026
 * @return  Length of the os version string
 */
static ssize_t get_os_version(char *buf, size_t sz)
{
#ifdef __APPLE__
    ssize_t out = -1;
    CFDictionaryRef d;
    CFStringRef s;

    d = _CFCopyServerVersionDictionary();
    if (d == NULL) d = _CFCopySystemVersionDictionary();
    if (d == NULL) goto out_exit;

    s = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@ (%@)"),
            CFDictionaryGetValue(d, _kCFSystemVersionProductVersionKey),
            CFDictionaryGetValue(d, _kCFSystemVersionBuildVersionKey));
    if (s == NULL) goto out_dict;

    if (CFStringGetCString(s, buf, (CFIndex) sz, CFStringGetSystemEncoding())) {
        out = strlen(buf);
    }

    CFRelease(s);
out_dict:
    CFRelease(d);
out_exit:
    return out;
#endif

    struct utsname u;
    assert_nonnull(buf);
    int e = uname(&u);
    char *p;
    if (e == 0) {
        p = strchr(u.release, '-');
        if (p != NULL) {
            return snprintf(buf, sz, "%.*s", (int) (p - u.release), u.release);
        }
        return snprintf(buf, sz, "%s", u.release);
    }
    return -1;
}

static ssize_t get_kernel_version(char *buf, size_t sz)
{
    struct utsname u;
    assert_nonnull(buf);
    int e = uname(&u);
    if (e == 0) {
        return snprintf(buf, sz, "%s %s %s %s",
                u.sysname, u.release, u.version, u.machine);
    }
    return -1;
}

static ssize_t get_hw_model(char *buf, size_t sz)
{
    assert_nonnull(buf);

#if defined(__linux__)
    static const char *cpuinfo = "/proc/cpuinfo";
    ssize_t out = -1;
    FILE *fp;
    char line[160];
    char *p;
    size_t n;

    fp = fopen(cpuinfo, "r");
    if (fp == NULL) goto out_exit;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strprefix(line, "model name")) {
            p = strchr(line, ':');
            if (p != NULL) {
                while (isspace(*++p)) continue;

                n = strlen(p);
                if (p[n-1] == '\n') p[--n] = '\0';

                (void) strncpy(buf, p, sz);
                out = UTILS_MIN(sz, n);
            }
            break;
        }
    }

    (void) fclose(fp);
out_exit:
    return out;
#elif defined(__NetBSD__)
    if (sysctlbyname("machdep.cpu_brand", buf, &sz, NULL, 0) < 0) {
        return -1;
    }
    return sz;
#elif defined(BSD)
    int mib[2] = {CTL_HW, HW_MODEL};
    if (sysctl(mib, ARRAY_SIZE(mib), buf, &sz, NULL, 0) < 0) {
        return -1;
    }
    return sz;
#else
#pragma GCC error "Unsupported operating system!"
#endif
}

static ssize_t get_device_arch(char *buf, size_t sz)
{
    struct utsname u;
    assert_nonnull(buf);
    if (uname(&u) == 0) {
        (void) strncpy(buf, u.machine, sz);
        return strlen(buf);
    }
    return -1;
}

void populate_contexts(cJSON *ctx)
{
    cJSON *contexts;
    cJSON *os;
    cJSON *device;
    cJSON *app;
    char buffer[160];
    ssize_t sz;

    assert_nonnull(ctx);
    contexts = cJSON_AddObjectToObject(ctx, "contexts");
    if (contexts == NULL) {
        LOG_ERR("cJSON_AddObjectToObject() fail  name: contexts");
        return;
    }

    os = cJSON_AddObjectToObject(contexts, "os");
    if (os != NULL) {
        sz = get_kernel_name(buffer, sizeof(buffer));
        if (sz > 0) (void) cJSON_AddStringToObject(os, "name", buffer);

        sz = get_os_version(buffer, sizeof(buffer));
        if (sz > 0) (void) cJSON_AddStringToObject(os, "version", buffer);

        sz = get_kernel_version(buffer, sizeof(buffer));
        if (sz > 0) (void) cJSON_AddStringToObject(os, "kernel_version", buffer);
    }

    device = cJSON_AddObjectToObject(contexts, "device");
    if (device != NULL) {
        /* If contexts.device.model absent  "Unknown Device" will be displayed in Sentry */
        sz = get_hw_model(buffer, sizeof(buffer));
        if (sz > 0) (void) cJSON_AddStringToObject(device, "model", buffer);

        sz = get_device_arch(buffer, sizeof(buffer));
        if (sz > 0) (void) cJSON_AddStringToObject(device, "arch", buffer);

        (void) cJSON_AddNumberToObject(device, "memory_size", CONST_PHYS_MEM_TOTAL);
        (void) cJSON_AddNumberToObject(device, "free_memory", CONST_PHYS_MEM_FREE);

        (void) cJSON_AddNumberToObject(device, "core", CONST_PHYS_CORES);
        (void) cJSON_AddNumberToObject(device, "socket", CONST_LOGI_CORES);
    }

    app = cJSON_AddObjectToObject(contexts, "app");
    if (app != NULL) {
        (void) cJSON_AddStringToObject(app, "build_type", CONST_CMAKE_BUILD_TYPE);
        (void) cJSON_AddNumberToObject(app, "pointer_bits", sizeof(void *) << 3u);

        (void) cJSON_AddStringToObject(app, "c_flags", CONST_CMAKE_C_FLAGS);
        (void) cJSON_AddStringToObject(app, "compile_definitions", CONST_COMPILE_DEFINITIONS);
    }
}

