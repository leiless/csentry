/*
 * Created 190803 lynnl
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/utsname.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
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

#if defined(__APPLE__) && defined(__MACH__)
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

#if defined(__APPLE__) && defined(__MACH__)
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
#if defined(__APPLE__) && defined(__MACH__)
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

#ifdef __linux__
/**
 * Fetch information from /proc/meminfo
 * @return      Memory info size in bytes
 *              -1 if any error
 */
static int64_t get_proc_meminfo(const char *category)
{
    static const char *cpuinfo = "/proc/meminfo";
    long long bytes = -1;
    FILE *fp;
    char line[160];
    char *p;

    assert_nonnull(category);

    fp = fopen(cpuinfo, "r");
    if (fp == NULL) goto out_exit;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strprefix(line, category)) {
            for (p = line; *p && !isdigit(*p); p++) continue;
            if (*p != '\0' && parse_llong(p, ' ', 10, &bytes)) {
                assert(bytes >= 0);
                bytes <<= 10u;
                assert(bytes >= 0);
            }
            break;
        }
    }

    (void) fclose(fp);
out_exit:
    return (int64_t) bytes;
}
#endif

/**
 * @return  Total physical memory size in bytes
 */
static int64_t get_phys_memsize(void)
{
#if defined(__linux__)
    return get_proc_meminfo("MemTotal:");
#elif defined(BSD)
#if defined(__APPLE__) && defined(__MACH__)
    int mib[] = {CTL_HW, HW_MEMSIZE};
#elif defined(HW_PHYSMEM64)
    /* NetBSD/OpenBSD uses HW_PHYSMEM64 */
    int mib[] = {CTL_HW, HW_PHYSMEM64};
#else
    int mib[] = {CTL_HW, HW_PHYSMEM};
#endif

    int64_t memsize;
    size_t n = sizeof(memsize);
    if (sysctl(mib, ARRAY_SIZE(mib), &memsize, &n, NULL, 0) != 0) {
        return -1;
    }
    assert(memsize > 0);
    return memsize;
#else
#pragma GCC error "Unsupported operating system!"
#endif
}

#if defined(__APPLE__) && defined(__MACH__)
/**
 * @return      1 if success, 0 otherwise.
 */
static int vm_stat64(
        vm_statistics64_data_t * const v,
        vm_size_t * const pgsz)
{
    mach_port_t host = mach_host_self();
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    assert_nonnull(v);
    assert_nonnull(pgsz);

    return host_page_size(host, pgsz) == KERN_SUCCESS &&
           host_statistics64(host, HOST_VM_INFO64,
                             (host_info64_t) v, &count) == KERN_SUCCESS;
}

/**
 * @return      Free memory size in bytes
 *              0 if any failure
 * see:
 *  https://www.jianshu.com/p/74ee9ed5546c
 *  http://archive.fo/B1k5L
 *  sentry-cocoa/Sources/SentryCrash/Recording/Monitors/SentryCrashMonitor_System.m
 */
static uint64_t xnu_get_free_memsize(void)
{
    uint64_t mem = 0;
    vm_statistics64_data_t v;
    vm_size_t pgsz;

    if (vm_stat64(&v, &pgsz)) {
        mem = (v.free_count + v.purgeable_count + v.external_page_count) * pgsz;
    }

    return mem;
}

static uint64_t xnu_get_usable_memsize(void)
{
    uint64_t mem = 0;
    vm_statistics64_data_t v;
    vm_size_t pgsz;

    if (vm_stat64(&v, &pgsz)) {
        mem = (v.free_count + v.active_count +
               v.inactive_count + v.wire_count) * pgsz;
    }

    return mem;
}
#endif

static int64_t get_free_memsize(void)
{
#if defined(__linux__)
    return get_proc_meminfo("MemAvailable:");
#elif defined(__APPLE__) && defined(__MACH__)
    return xnu_get_free_memsize();
#elif defined(BSD)
#pragma GCC error "TODO: support various BSD systems!"
#else
#pragma GCC error "Unsupported operating system!"
#endif
}

void populate_contexts(cJSON *ctx)
{
    cJSON *contexts;
    cJSON *os;
    cJSON *device;
    cJSON *app;
    char buffer[160];
    ssize_t sz;
    int64_t mem;

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

        mem = get_phys_memsize();
        if (mem > 0) (void) cJSON_AddNumberToObject(device, "memory_size", mem);

        mem = get_free_memsize();
        if (mem > 0) (void) cJSON_AddNumberToObject(device, "free_memory", mem);

#if defined(__APPLE__) && defined(__MACH__)
        mem = xnu_get_usable_memsize();
        if (mem > 0) (void) cJSON_AddNumberToObject(device, "usable_memory", mem);
#endif

        /* TODO: get core/socket info? */
    }

    app = cJSON_AddObjectToObject(contexts, "app");
    if (app != NULL) {
        (void) cJSON_AddStringToObject(app, "build_type", CONST_CMAKE_BUILD_TYPE);
        (void) cJSON_AddNumberToObject(app, "pointer_bits", sizeof(void *) << 3u);

        (void) cJSON_AddStringToObject(app, "c_flags", CONST_CMAKE_C_FLAGS);
        (void) cJSON_AddStringToObject(app, "compile_definitions", CONST_COMPILE_DEFINITIONS);
    }
}

