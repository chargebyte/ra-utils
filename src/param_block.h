/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <cb_protocol.h>

#define MARKER 0xC001F00D
#define CHANNEL_DISABLE_VALUE 0x1fff

struct param_block {
    uint32_t sob;
    int16_t temperature[CB_PROTO_MAX_PT1000S];
    uint8_t contactor[CB_PROTO_MAX_CONTACTORS];
    uint8_t estop[CB_PROTO_MAX_ESTOPS];
    uint32_t eob;
    uint8_t crc;
} __attribute__((packed));

enum contactor_type {
    CONTACTOR_NONE = 0,
    CONTACTOR_WITHOUT_FEEDBACK,
    CONTACTOR_WITH_FEEDBACK,
    CONTACTOR_MAX,
};

enum emergeny_stop_type {
    EMERGENY_STOP_NONE = 0,
    EMERGENY_STOP_ACTIVE_LOW,
    EMERGENY_STOP_MAX,
};

int str_to_temperature(const char *s, int16_t *temperature);
enum contactor_type str_to_contactor_type(const char *s);
enum emergeny_stop_type str_to_emergeny_stop_type(const char *s);

int temperature_to_str(char *buffer, size_t size, int16_t temperature);
const char *contactor_type_to_str(const enum contactor_type type);
const char *emergeny_stop_type_to_str(const enum emergeny_stop_type type);

void pb_dump(struct param_block *param_block);
