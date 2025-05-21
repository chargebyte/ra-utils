/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

void error(const char *format, ...);
void debug(const char *format, ...);

typedef void (*ra_utils_msg_cb)(const char *format, va_list args);

void ra_utils_set_error_msg_cb(ra_utils_msg_cb cb);
void ra_utils_set_debug_msg_cb(ra_utils_msg_cb cb);

#ifdef __cplusplus
}
#endif
