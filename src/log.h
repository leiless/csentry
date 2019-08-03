/*
 * Created 190803 lynnl
 */

#ifndef _CSENTRY_LOG_H
#define _CSENTRY_LOG_H

#include <stdio.h>

#define _LOG_STDERR(fmt, ...)   (void) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define LOG(fmt, ...)           (void) printf(fmt "\n", ##__VA_ARGS__)

#ifdef DEBUG
#define LOG_DBG(fmt, ...)       LOG("[DBG] " fmt, ##__VA_ARGS__)
#endif

#define LOG_ERR(fmt, ...)       _LOG_STDERR("[ERR] " fmt "  at: %s()#L%d", \
                                    ##__VA_ARGS__, __func__, __LINE__)

#define LOG_WARN(fmt, ...)      _LOG_STDERR("[WARN] " fmt "  at: %s()#L%d", \
                                    ##__VA_ARGS__, __func__, __LINE__)

#endif

