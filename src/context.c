/*
 * Created 190803 lynnl
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/utsname.h>

#if defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <sys/statvfs.h>
#include <sys/time.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#endif

#if defined(BSD)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__NetBSD__)
#include <uvm/uvm_extern.h>
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
#error "Unsupported operating system!"
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
#error "Unsupported operating system!"
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
#error "Unsupported operating system!"
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

#if defined(__FreeBSD__)
static int freebsd_get_page_size(void)
{
    static int mib[2] = {CTL_HW, HW_PAGESIZE};
    int pgsz;
    size_t len;
    int e;

    len = sizeof(pgsz);
    if (sysctl(mib, ARRAY_SIZE(mib), &pgsz, &len, NULL, 0) != 0) {
        e = errno;
        LOG_ERR("sysctl() hw.pagesize fail  errno: %d", e);
        errno = e;
        return -1;
    }
    assert(len == sizeof(pgsz));
    assert(pgsz >= 0);

    return pgsz;
}

static int64_t freebsd_get_free_memsize(void)
{
    int pgsz;
    uint32_t v[3];
    size_t len;

    if ((pgsz = freebsd_get_page_size()) < 0) return -1;

    len = sizeof(uint32_t);
    if (sysctlbyname("vm.stats.vm.v_inactive_count", &v[0], &len, NULL, 0) != 0) {
        LOG_ERR("sysctlbyname() vm.stats.vm.v_inactive_count fail  errno: %d", errno);
        return -1;
    }
    assert(len == sizeof(uint32_t));

    len = sizeof(uint32_t);
    if (sysctlbyname("vm.stats.vm.v_cache_count", &v[1], &len, NULL, 0) != 0) {
        LOG_ERR("sysctlbyname() vm.stats.vm.v_cache_count fail  errno: %d", errno);
        return -1;
    }
    assert(len == sizeof(uint32_t));

    len = sizeof(uint32_t);
    if (sysctlbyname("vm.stats.vm.v_free_count", &v[2], &len, NULL, 0) != 0) {
        LOG_ERR("sysctlbyname() vm.stats.vm.v_free_count fail  errno: %d", errno);
        return -1;
    }
    assert(len == sizeof(uint32_t));

    /* page_size * (inactive_count + cache_count + free_count) */
    return (int64_t) pgsz * (v[0] + v[1] + v[2]);
}

/**
 * see: https://github.com/ocochard/myscripts/blob/master/FreeBSD/freebsd-memory.sh
 */
static int64_t freebsd_get_usable_memsize(void)
{
    int pgsz;
    uint32_t pgcnt;
    size_t len;

    if ((pgsz = freebsd_get_page_size()) < 0) return -1;

    len = sizeof(pgcnt);
    if (sysctlbyname("vm.stats.vm.v_page_count", &pgcnt, &len, NULL, 0) != 0) {
        LOG_ERR("sysctlbyname() vm.stats.vm.v_page_count fail  errno: %d", errno);
        return -1;
    }
    assert(len == sizeof(pgcnt));

    /* Total real memory managed */
    return (int64_t) pgsz * pgcnt;
}
#endif

#if defined(__OpenBSD__)
/**
 * see: /usr/include/uvm/uvmexp.h
 */
static int openbsd_get_uvmexp(struct uvmexp *uvm)
{
    static int mib[] = {CTL_VM, VM_UVMEXP};
    size_t len = sizeof(*uvm);
    int e;

    assert_nonnull(uvm);

    if (sysctl(mib, ARRAY_SIZE(mib), uvm, &len, NULL, 0) != 0) {
        e = errno;
        LOG_ERR("sysctl() vm.uvmexp fail  errno: %d", e);
        errno = e;
        return -1;
    }
    assert(len == sizeof(*uvm));

    return 0;
}

/**
 * see: $(vmstat -s), $(systat uvm)
 */
static int64_t openbsd_get_free_memsize(void)
{
    struct uvmexp uvm;

    if (openbsd_get_uvmexp(&uvm) != 0) return -1;
    assert(uvm.free >= 0);
    assert(uvm.inactive >= 0);
    assert(uvm.pageshift > 0);

    return ((int64_t) + uvm.free + uvm.inactive) << uvm.pageshift;
}

static int64_t openbsd_get_usable_memsize(void)
{
    struct uvmexp uvm;

    if (openbsd_get_uvmexp(&uvm) != 0) return -1;
    assert(uvm.npages > 0);
    assert(uvm.pageshift > 0);

    return (int64_t) uvm.npages << uvm.pageshift;
}
#endif

#if defined(__NetBSD__)
static int netbsd_get_uvmexp2(struct uvmexp_sysctl *uvm)
{
    static const int mib[] = {CTL_VM, VM_UVMEXP2};
    size_t len = sizeof(*uvm);
    int e;

    assert_nonnull(uvm);

    if (sysctl(mib, ARRAY_SIZE(mib), uvm, &len, NULL, 0) != 0) {
        e = errno;
        LOG_ERR("sysctl() vm.uvmexp2 fail  errno: %d", e);
        errno = e;
        return -1;
    }
    assert(len == sizeof(*uvm));

    return 0;
}

static int64_t netbsd_get_free_memsize(void)
{
    struct uvmexp_sysctl uvm;

    if (netbsd_get_uvmexp2(&uvm) != 0) return -1;
    assert(uvm.free >= 0);
    assert(uvm.inactive >= 0);
    assert(uvm.pageshift > 0);

    return (uvm.free + uvm.inactive) << uvm.pageshift;
}

static int64_t netbsd_get_usable_memsize(void)
{
    struct uvmexp_sysctl uvm;

    if (netbsd_get_uvmexp2(&uvm) != 0) return -1;
    assert(uvm.npages > 0);
    assert(uvm.pageshift > 0);

    return uvm.npages << uvm.pageshift;
}
#endif

static int64_t get_free_memsize(void)
{
#if defined(__linux__)
    return get_proc_meminfo("MemAvailable:");
#elif defined(__APPLE__) && defined(__MACH__)
    return xnu_get_free_memsize();
#elif defined(__FreeBSD__)
    return freebsd_get_free_memsize();
#elif defined(__OpenBSD__)
    return openbsd_get_free_memsize();
#elif defined(__NetBSD__)
    return netbsd_get_free_memsize();
#else
#error "Unsupported operating system!"
#endif
}

static int64_t get_usable_free_memsize(void)
{
#if defined(__linux__)
    return 0;   /* NYI */
#elif defined(__APPLE__) && defined(__MACH__)
    return xnu_get_usable_memsize();
#elif defined(__FreeBSD__)
    return freebsd_usable_usable_memsize();
#elif defined(__OpenBSD__)
    return openbsd_usable_free_memsize();
#elif defined(__NetBSD__)
    return netbsd_usable_free_memsize();
#else
#error "Unsupported operating system!"
#endif
}

static void *statvfs_root(struct statvfs *v)
{
    assert_nonnull(v);
    (void) memset(v, 0, sizeof(*v));

    if (statvfs("/", v) != 0) {
        /* NetBSD/DragonflyBSD implement statvfs() as a syscall */
        LOG_ERR("statvfs(3) fail  errno: %d\n", errno);
        return NULL;
    }

    return v;
}

static int64_t get_storage_size(void)
{
    struct statvfs v;
    if (statvfs_root(&v) == NULL) return -1;
    if (v.f_frsize <= 0 || v.f_blocks <= 0) return -1;
    return v.f_frsize * v.f_blocks;
}

static int64_t get_storage_free(void)
{
    struct statvfs v;
    if (statvfs_root(&v) == NULL) return -1;
    if (v.f_frsize <= 0 || v.f_bavail <= 0) return -1;
    return v.f_frsize * v.f_bavail;
}

#if defined(__linux__)
static int64_t linux_get_boot_time(void)
{
    static const char *cpuinfo = "/proc/stat";
    long long int out = -1;
    FILE *fp;
    char line[32];
    char *p;

    fp = fopen(cpuinfo, "r");
    if (fp == NULL) goto out_exit;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strprefix(line, "btime ")) {
            p = strchr(line, ' ');
            assert_nonnull(p);
            while (isspace(*++p)) continue;
            if (isdigit(*p)) {
                errno = 0;
                out = strtoll(p, NULL, 10);
                if (errno != 0) out = -1;
            }
            break;
        }
    }

    (void) fclose(fp);
out_exit:
    return (int64_t) out;
}
#endif

static int64_t get_boot_time(void)
{
#if defined(__linux__)
    return linux_get_boot_time();
#elif defined(BSD)
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    struct timeval tv;
    size_t sz = sizeof(tv);

    if (sysctl(mib, ARRAY_SIZE(mib), &tv, &sz, NULL, 0) != 0) {
        LOG_ERR("sysctl() kern.boottime fail  errno: %d", errno);
        return -1;
    }
    assert(sz == sizeof(tv));
    assert(tv.tv_sec > 0);
    assert(tv.tv_usec >= 0);

    return tv.tv_sec;
#else
#error "Unsupported operating system!"
#endif
}

/**
 * @param sz        At least 32 bytes(o.w. truncation will happen)
 * see:
 *  https://tools.ietf.org/html/rfc822#section-5
 *  http://hackage.haskell.org/package/time-http-0.5/docs/Data-Time-Format-RFC822.html
 *  https://validator.w3.org/feed/docs/warning/ProblematicalRFC822Date.html
 */
static void *fmt_epoch_to_rfc822(time_t t, char *buf, size_t sz)
{
    struct tm *tm;
    assert_nonnull(buf);
    tm = t >= 0 ? localtime(&t) : NULL;
    if (tm == NULL) return NULL;
    if (strftime(buf, sz, "%a, %d %b %Y %H:%M:%S %z", tm) == 0) return NULL;
    return buf;
}

void populate_contexts(cJSON *ctx)
{
    cJSON *contexts;
    cJSON *os;
    cJSON *device;
    cJSON *app;
    char buffer[160];
    ssize_t sz;
    int64_t val;

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

        val = get_phys_memsize();
        if (val > 0) (void) cJSON_AddNumberToObject(device, "memory_size", val);

        val = get_free_memsize();
        if (val > 0) (void) cJSON_AddNumberToObject(device, "free_memory", val);

        val = get_usable_free_memsize();
        if (val > 0) (void) cJSON_AddNumberToObject(device, "usable_memory", val);

        val = get_storage_size();
        if (val > 0) (void) cJSON_AddNumberToObject(device, "storage_size", val);

        val = get_storage_free();
        if (val > 0) (void) cJSON_AddNumberToObject(device, "storage_free", val);

        if (fmt_epoch_to_rfc822(get_boot_time(), buffer, sizeof(buffer))) {
            (void) cJSON_AddStringToObject(device, "boot_time", buffer);
        }
    }

    app = cJSON_AddObjectToObject(contexts, "app");
    if (app != NULL) {
        (void) cJSON_AddStringToObject(app, "build_type", CONST_CMAKE_BUILD_TYPE);
        (void) cJSON_AddNumberToObject(app, "pointer_bits", sizeof(void *) << 3u);
    }
}

