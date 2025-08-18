/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <endian.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <tools.h>
#include "param_block.h"

int str_to_temperature(const char *s, int16_t *temperature)
{
    char *endptr;
    float val;
    int32_t ival;

    /* special case: disable[d], none or off */
    if (strcasecmp(s, "disable") == 0 || strcasecmp(s, "disabled") == 0 ||
        strcasecmp(s, "none") == 0 || strcasecmp(s, "off") == 0) {
        *temperature = CHANNEL_DISABLE_VALUE;
        return 0;
    }

    val = strtof(s, &endptr);

    /* conversion failed: no valid float, or suffix not '°C' (with possible single whitespace before) */
    if (errno != 0 || endptr == s ||
        // Important note: we want to use strcmp here because we want a dump binary comparison
        // (our source code is UTF-8 and we expect the YAML also as UTF-8)
        !(strcmp(endptr, "°C") == 0 || strcmp(endptr, " °C") == 0))
       return -1;

    /* we store it as int with 0.1 °C resolution, so multiply with 10
     * and round the float to int */
    ival = (int32_t)roundf(val * 10);

    /* (silently) clamp to range -80.0 °C ... 200.0 °C */
    if (ival > 2000) ival = 2000;
    if (ival < -800) ival = -800;

    *temperature = htole16(ival);

    return 0;
}

int temperature_to_str(char *buffer, size_t size, int16_t temperature)
{
    if (le16toh(temperature) == CHANNEL_DISABLE_VALUE)
        return snprintf(buffer, size, "%s", "disabled");
    /* older firmware versions used another magic value */
    if (le16toh(temperature) == OLD_CHANNEL_DISABLE_VALUE)
        return snprintf(buffer, size, "%s", "disabled");

    return snprintf(buffer, size, "%.1f °C", (int16_t)le16toh(temperature) / 10.0f);
}

static const char *contactor_type_to_string[CONTACTOR_MAX] = {
    "disabled",
    "without-feedback",
    "with-feedback",
};

enum contactor_type str_to_contactor_type(const char *s)
{
    unsigned int i;

    for (i = CONTACTOR_NONE; i < CONTACTOR_MAX; ++i)
        if (strcasecmp(s, contactor_type_to_string[i]) == 0)
            return i;

    /* relaxed input handling: also accept 'none' */
    if (strcasecmp(s, "none") == 0)
        return CONTACTOR_NONE;

    return CONTACTOR_MAX;
}

const char *contactor_type_to_str(const enum contactor_type type)
{
    if (type >= CONTACTOR_MAX)
        return "invalid";

    return contactor_type_to_string[type];
}

static const char *emergeny_stop_to_string[EMERGENY_STOP_MAX] = {
    "disabled",
    "active-low",
};

enum emergeny_stop_type str_to_emergeny_stop_type(const char *s)
{
    unsigned int i;

    for (i = EMERGENY_STOP_NONE; i < EMERGENY_STOP_MAX; ++i)
        if (strcasecmp(s, emergeny_stop_to_string[i]) == 0)
            return i;

    /* relaxed input handling: also accept 'none' or 'off' */
    if (strcasecmp(s, "none") == 0 || strcasecmp(s, "off") == 0)
        return EMERGENY_STOP_NONE;

    return EMERGENY_STOP_MAX;
}

const char *emergeny_stop_type_to_str(const enum emergeny_stop_type type)
{
    if (type >= EMERGENY_STOP_MAX)
        return "invalid";

    return emergeny_stop_to_string[type];
}

void pb_dump(struct param_block *param_block)
{
    int i;

    printf("pt1000s:\n");
    for (i = 0; i < ARRAY_SIZE(param_block->temperature); i++) {
        char buffer[32];
        temperature_to_str(buffer, sizeof(buffer), param_block->temperature[i]);
        printf("  - %s\n", buffer);
    }
    printf("\n");

    printf("contactors:\n");
    for (i = 0; i < ARRAY_SIZE(param_block->contactor); i++)
        printf("  - %s\n", contactor_type_to_str(param_block->contactor[i]));
    printf("\n");

    printf("estops:\n");
    for (i = 0; i < ARRAY_SIZE(param_block->estop); i++)
        printf("  - %s\n", emergeny_stop_type_to_str(param_block->estop[i]));
    printf("\n");
}
