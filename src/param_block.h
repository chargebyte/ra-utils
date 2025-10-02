/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <cb_protocol.h>

#define MARKER 0xC001F00D
#define CHANNEL_DISABLE_VALUE 0x1fff
#define OLD_CHANNEL_DISABLE_VALUE 0x8000

/* pre-versioned parameter block structure */
struct unversioned_param_block {
    uint32_t sob;
    int16_t temperature[4]; // use fixed sizes here since this version will not change anymore
    uint8_t contactor[2]; // this version only supports 2 contactors
    uint8_t estop[3]; // use fixed sizes here since this version will not change anymore
    uint32_t eob;
    uint8_t crc;
} __attribute__((packed));

/* latest parameter version this code understands */
#define PARAMETER_BLOCK_VERSION 1

struct param_block {
    uint32_t sob;

    uint16_t version;

    int16_t temperature[CB_PROTO_MAX_PT1000S];
    int16_t temperature_resistance_offset[CB_PROTO_MAX_PT1000S]; // offset for temperature sensor resistance in 10mOhm

    uint8_t contactor_type[CB_PROTO_MAX_CONTACTORS];
    uint8_t contactor_close_time[CB_PROTO_MAX_CONTACTORS]; // close time for HV contactor in multiples of 10ms
    uint8_t contactor_open_time[CB_PROTO_MAX_CONTACTORS]; // open time for HV contactor in multiples of 10ms

    uint8_t estop[CB_PROTO_MAX_ESTOPS];
    uint32_t eob;
    uint8_t crc;
} __attribute__((packed));


enum contactor_type {
    CONTACTOR_NONE = 0,
    CONTACTOR_WITHOUT_FEEDBACK,
    CONTACTOR_WITH_FEEDBACK_NO,
    CONTACTOR_WITH_FEEDBACK_NC,
    CONTACTOR_MAX,
};

enum emergeny_stop_type {
    EMERGENY_STOP_NONE = 0,
    EMERGENY_STOP_ACTIVE_LOW,
    EMERGENY_STOP_MAX,
};

int str_to_version(const char *s, uint16_t *version);
int version_to_str(char *buffer, size_t size, uint16_t version);

int str_to_temperature(const char *s, int16_t *temperature);
int temperature_to_str(char *buffer, size_t size, int16_t temperature);

int str_to_resistance_offset(const char *s, int16_t *offset);
int resistance_offset_to_str(char *buffer, size_t size, int16_t offset);

enum contactor_type str_to_contactor_type(const char *s);
const char *contactor_type_to_str(const enum contactor_type type);

int str_to_contactor_time(const char *s, uint8_t *time);
int contactor_time_to_str(char *buffer, size_t size, int8_t time);

enum emergeny_stop_type str_to_emergeny_stop_type(const char *s);
const char *emergeny_stop_type_to_str(const enum emergeny_stop_type type);

bool pb_is_pt1000_enabled(struct param_block *param_block, unsigned char n);
bool pb_is_contactor_enabled(struct param_block *param_block, unsigned char n);

void pb_refresh_crc(struct param_block *param_block);

/* returns true if the CRC is correct */
bool pb_check_crc(struct param_block *param_block);

void pb_init(struct param_block *param_block);
void pb_dump(struct param_block *param_block);

/* error return values for pb_read */
#define PB_READ_SUCCESS     0
#define PB_READ_ERROR_MAGIC 1
#define PB_READ_ERROR_CRC   2

/* returns 0 on success, -1 on generic error, or one of the PB_READ_... values above */
int pb_read(FILE *f, struct param_block *param_block);
int pb_write(struct param_block *param_block, FILE *f);
