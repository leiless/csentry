/*
 * Created 190622 lynnl
 */

#include <string.h>
#include <time.h>

#include "utils.h"

/**
 * @return  Check if given string s1 starts with s2
 * XXX:     Always return 1 if s2 is empty
 *
 * Taken from xnu/osfmk/device/subrs.c#strprefix()
 */
int strprefix(const char *s1, const char *s2)
{
    int c;

    assert_nonnull(s1);
    assert_nonnull(s2);

    while ((c = *s2++) != '\0') {
        if (c != *s1++) return 0;
    }

    return 1;
}

#define ISO_8601_BUFSZ      20

/**
 * Format ISO 8601 datetime without trailing timezone
 */
void format_iso_8601_time(char *str)
{
    time_t now;

    assert_nonnull(str);
    (void) time(&now);

    *str = '\0';
    /* gmtime(3) should never return NULL in such case */
    (void) strftime(str, ISO_8601_BUFSZ, "%Y-%m-%dT%H:%M:%S", gmtime(&now));
}

