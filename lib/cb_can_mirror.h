/*
 * Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int cb_can_mirror_open(const char *device);
int cb_can_mirror_close(int fd);

int cb_can_mirror_write(int fd, uint8_t com, uint64_t data);

#ifdef __cplusplus
}
#endif
