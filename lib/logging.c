/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <stddef.h>
#include "logging.h"

static ra_utils_msg_cb debug_cb = NULL;

void ra_utils_set_debug_msg_cb(ra_utils_msg_cb cb)
{
    debug_cb = cb;
}

void debug(const char *format, ...)
{
    if (debug_cb) {
        va_list args;
        va_start(args, format);
        debug_cb(format, args);
        va_end(args);
    }
}

static ra_utils_msg_cb error_cb = NULL;

void ra_utils_set_error_msg_cb(ra_utils_msg_cb cb)
{
    error_cb = cb;
}

void error(const char *format, ...)
{
    if (error_cb) {
        va_list args;
        va_start(args, format);
        error_cb(format, args);
        va_end(args);
    }
}
