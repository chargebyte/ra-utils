/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <endian.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <inttypes.h>
#include <tools.h>
#include "param_block_crc8.h"
#include "param_block.h"


int str_to_version(const char *s, uint16_t *version)
{
    char *endptr;
    long int val;

    val = strtol(s, &endptr, 10);

    /* conversion failed: no valid int*/
    if (errno != 0 || endptr == s || *endptr != '\0')
       return -1;

    /* range check */
    if (val < 1)
        return -1;
    if (val > UINT16_MAX)
        return -1;

    *version = val;

    return 0;
}

int version_to_str(char *buffer, size_t size, uint16_t version)
{
    return snprintf(buffer, size, "%" PRIu16, version);
}

int str_to_temperature(const char *s, int16_t *temperature)
{
    char *endptr;
    float val;
    int32_t ival;

    /* special case: disable[d], none or off */
    if (strcasecmp(s, "disable") == 0 || strcasecmp(s, "disabled") == 0 ||
        strcasecmp(s, "none") == 0 || strcasecmp(s, "off") == 0) {
        *temperature = htole16(CHANNEL_DISABLE_VALUE);
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

int str_to_resistance_offset(const char *s, int16_t *offset)
{
    char *endptr;
    float val;
    int32_t ival;

    val = strtof(s, &endptr);

    /* conversion failed: no valid float, or suffix not 'Ω' (with possible single whitespace before) */
    if (errno != 0 || endptr == s ||
        // Important note 1: we want to use strcmp here because we want a dump binary comparison
        // (our source code is UTF-8 and we expect the YAML also as UTF-8)
        // Important note 2: there is a dedicated Ohm symbol in UTF-8 (U+2126 = 0xe2 0x84 0xa6),
        // but very often the Greek letter Omega is also used. We accept both characters, but
        // output the Omega version. Next line is Greek letter, second line
        // is Ohm symbol - humans will probably see no difference.
        !(strcmp(endptr, "Ω") == 0 || strcmp(endptr, " Ω") == 0 ||
          strcmp(endptr, "Ω") == 0 || strcmp(endptr, " Ω") == 0))
       return -1;

    /* we store it as int with 0.001 Ω resolution, so multiply with 1000
     * and round the float to int */
    ival = (int32_t)roundf(val * 1000);

    /* (silently) clamp to range -32.0 Ω ... 32.0 Ω */
    if (ival >  32000) ival =  32000;
    if (ival < -32000) ival = -32000;

    *offset = htole16(ival);

    return 0;
}

int resistance_offset_to_str(char *buffer, size_t size, int16_t offset)
{
    return snprintf(buffer, size, "%.3f Ω", (int16_t)le16toh(offset) / 1000.0f);
}

static const char *contactor_type_to_string[CONTACTOR_MAX] = {
    "disabled",
    "without-feedback",
    "with-feedback-normally-open",
    "with-feedback-normally-closed",
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

    /* transition: handle 'with-feedback' as 'normally closed' */
    if (strcasecmp(s, "with-feedback") == 0)
        return CONTACTOR_WITH_FEEDBACK_NC;

    return CONTACTOR_MAX;
}

const char *contactor_type_to_str(const enum contactor_type type)
{
    if (type >= CONTACTOR_MAX)
        return "invalid";

    return contactor_type_to_string[type];
}

int str_to_contactor_time(const char *s, uint8_t *time)
{
    char *endptr;
    unsigned long val;

    val = strtoul(s, &endptr, 10);

    /* conversion failed: no valid int, or suffix not 'ms' (with possible single whitespace before) */
    if (errno != 0 || endptr == s ||
        !(strcmp(endptr, "ms") == 0 || strcmp(endptr, " ms") == 0))
       return -1;

    /* we store it as unsigned int with 10 ms resolution, so divide by 10 */
    val /= 10;

    /* (silently) clamp to range */
    if (val > 255) val = 255;

    *time = val;

    return 0;
}

int contactor_time_to_str(char *buffer, size_t size, int8_t time)
{
    return snprintf(buffer, size, "%u ms", time * 10);
}

int str_to_rcm_time(const char *s, uint8_t *time)
{
    char *endptr;
    unsigned long val;

    val = strtoul(s, &endptr, 10);

    /* conversion failed: no valid int, or suffix not 'ms' (with possible single whitespace before) */
    if (errno != 0 || endptr == s ||
        !(strcmp(endptr, "ms") == 0 || strcmp(endptr, " ms") == 0))
       return -1;

    /* we store it as unsigned int with 20 ms resolution, so divide by 20 */
    val /= 20;

    /* (silently) clamp to range */
    if (val > 255) val = 255;

    *time = val;

    return 0;
}

int rcm_time_to_str(char *buffer, size_t size, int8_t time)
{
    return snprintf(buffer, size, "%u ms", time * 20);
}

static const char *pin_polarity_to_string[PIN_POLARITY_MAX] = {
    "disabled",
    "active-low",
    "active-high",
};

enum pin_polarity_type str_to_pin_polarity_type(const char *s)
{
    unsigned int i;

    for (i = PIN_POLARITY_NONE; i < PIN_POLARITY_MAX; ++i)
        if (strcasecmp(s, pin_polarity_to_string[i]) == 0)
            return i;

    /* relaxed input handling: also accept 'disable', 'none' or 'off' */
    if (strcasecmp(s, "disable") == 0 || strcasecmp(s, "none") == 0 || strcasecmp(s, "off") == 0)
        return PIN_POLARITY_NONE;

    return PIN_POLARITY_MAX;
}

const char *pin_polarity_type_to_str(const enum pin_polarity_type type)
{
    if (type >= PIN_POLARITY_MAX)
        return "invalid";

    return pin_polarity_to_string[type];
}

/* this function is only used to short-circuit the configuration at this point
 * to make it more human-readable; in other words, it is used to document that
 * a given block does not have (sub-)properties and that means that it is
 * only allowed to disable a functionality. anything else is considered an
 * error (and would probably also be not valid YAML either). */
int str_to_disabled_flag(const char *s, bool *flag)
{
    char *endptr;

    /* special case: disable[d], none or off */
    if (strcasecmp(s, "disable") == 0 || strcasecmp(s, "disabled") == 0 ||
        strcasecmp(s, "none") == 0 || strcasecmp(s, "off") == 0) {
        *flag = false;
        return 0;
    }

    return 1;
}

bool pb_is_pt1000_enabled(struct param_block_v1*param_block, unsigned char n)
{
    return param_block->temperature[n] != htole16(CHANNEL_DISABLE_VALUE);
}

bool pb_is_contactor_enabled(struct param_block_v1 *param_block, unsigned char n)
{
    return param_block->contactor_type[n] != CONTACTOR_NONE;
}

bool pb_is_rcm_enabled(struct param_block_v2 *param_block)
{
    return param_block->rcm_fault_polarity != PIN_POLARITY_NONE &&
           param_block->rcm_test_polarity != PIN_POLARITY_NONE;
}

void pb_refresh_crc_v1(struct param_block_v1 *param_block)
{
    param_block->crc = crc8((uint8_t *)param_block, sizeof(*param_block) - 1);
}

void pb_refresh_crc_v2(struct param_block_v2 *param_block)
{
    param_block->crc = crc8((uint8_t *)param_block, sizeof(*param_block) - 1);
}

static bool pb_check_unversioned_param_block_crc(struct unversioned_param_block *param_block)
{
    return param_block->crc == crc8((uint8_t *)param_block, sizeof(*param_block) - 1);
}

bool pb_check_crc_v1(struct param_block_v1 *param_block)
{
    return param_block->crc == crc8((uint8_t *)param_block, sizeof(*param_block) - 1);
}

bool pb_check_crc_v2(struct param_block_v2 *param_block)
{
    return param_block->crc == crc8((uint8_t *)param_block, sizeof(*param_block) - 1);
}

void pb_init_v1(struct param_block_v1 *param_block)
{
    int i;

    memset(param_block, 0, sizeof(*param_block));

    param_block->sob = htole32(MARKER);
    param_block->eob = htole32(MARKER);

    param_block->version = 1;

    for (i = 0; i < ARRAY_SIZE(param_block->temperature); i++)
        param_block->temperature[i] = htole16(CHANNEL_DISABLE_VALUE);

    pb_refresh_crc_v1(param_block);
}

void pb_dump_v1(struct param_block_v1 *param_block)
{
    char buffer[32];
    int i;

    printf("pt1000s:\n");
    for (i = 0; i < ARRAY_SIZE(param_block->temperature); i++) {
        temperature_to_str(buffer, sizeof(buffer), param_block->temperature[i]);

        if (pb_is_pt1000_enabled(param_block, i)) {
            printf("  - abort-temperature: %s\n", buffer);

            resistance_offset_to_str(buffer, sizeof(buffer), param_block->temperature_resistance_offset[i]);
            printf("    resistance-offset: %s\n", buffer);
        } else {
            printf("  - %s\n", buffer);
        }
    }
    printf("\n");

    printf("contactors:\n");
    for (i = 0; i < ARRAY_SIZE(param_block->contactor_type); i++) {
        if (pb_is_contactor_enabled(param_block, i)) {
            printf("  - type: %s\n", contactor_type_to_str(param_block->contactor_type[i]));

            contactor_time_to_str(buffer, sizeof(buffer), param_block->contactor_close_time[i]);
            printf("    close-time: %s\n", buffer);

            contactor_time_to_str(buffer, sizeof(buffer), param_block->contactor_open_time[i]);
            printf("    open-time: %s\n", buffer);
        } else {
            printf("  - %s\n", contactor_type_to_str(param_block->contactor_type[i]));
        }
    }

    printf("\n");

    printf("estops:\n");
    for (i = 0; i < ARRAY_SIZE(param_block->estop); i++)
        printf("  - %s\n", pin_polarity_type_to_str(param_block->estop[i]));
}

void pb_init_v2(struct param_block_v2 *param_block)
{
    int i;

    memset(param_block, 0, sizeof(*param_block));

    param_block->sob = htole32(MARKER);
    param_block->eob = htole32(MARKER);

    param_block->version = 2;

    for (i = 0; i < ARRAY_SIZE(param_block->temperature); i++)
        param_block->temperature[i] = htole16(CHANNEL_DISABLE_VALUE);

    pb_refresh_crc_v2(param_block);
}

void pb_dump_v2(struct param_block_v2 *param_block)
{
    char buffer[32];

    if (pb_is_rcm_enabled(param_block)) {
        printf("rcm:\n");
        printf("  fault-polarity: %s\n", pin_polarity_type_to_str(param_block->rcm_fault_polarity));
        printf("  test-polarity: %s\n", pin_polarity_type_to_str(param_block->rcm_test_polarity));

        rcm_time_to_str(buffer, sizeof(buffer), param_block->rcm_test_trigger_time);
        printf("  test-trigger-time: %s\n", buffer);

        rcm_time_to_str(buffer, sizeof(buffer), param_block->rcm_test_check_tripped_time);
        printf("  test-check-tripped-time: %s\n", buffer);

        rcm_time_to_str(buffer, sizeof(buffer), param_block->rcm_test_check_normal_time);
        printf("  test-check-normal-time: %s\n", buffer);
    } else {
        printf("rcm: disabled\n");
    }
}

void pb_init(struct param_block_v2 *param_block)
{
    pb_init_v2(param_block);
}

void pb_dump(struct param_block_v2 *param_block)
{
    printf("version: %u\n", param_block->version);
    printf("\n");

    pb_dump_v1((struct param_block_v1 *)param_block);

    if (param_block->version >= 2) {
        printf("\n");
        pb_dump_v2(param_block);
    }
}

static void pb_migrate_unversioned_to_v1(struct unversioned_param_block *old, struct param_block_v1 *new)
{
    int i;

    /* we assume and rely on that the new parameter block has enough space */
    memcpy(new->temperature, old->temperature, sizeof(new->temperature));
    memcpy(new->contactor_type, old->contactor, sizeof(old->contactor));
    memcpy(new->estop, old->estop, sizeof(old->estop));

    /* old version only supported CONTACTOR_WITH_FEEDBACK which is now CONTACTOR_WITH_FEEDBACK_NO but
     * we migrate it to CONTACTOR_WITH_FEEDBACK_NC since this is the documented and recommended setting
     * note: we iterate over the size of the old array but modify the new one
     */
    for (i = 0; i < ARRAY_SIZE(old->contactor); i++)
        if (old->contactor[i] == CONTACTOR_WITH_FEEDBACK_NO)
            new->contactor_type[i] = CONTACTOR_WITH_FEEDBACK_NC;

    pb_refresh_crc_v1(new);
}

static void pb_migrate_v1_to_v2(struct param_block_v1 *old, struct param_block_v2 *new)
{
    /* copy the whole data block from v1 to v2 which has the same structure */
    memcpy(&new->temperature, &old->temperature,
           offsetof(struct param_block_v1, eob) - offsetof(struct param_block_v1, temperature));

    pb_refresh_crc_v2(new);
}

int pb_read(FILE *f, struct param_block_v2 *param_block)
{
    struct unversioned_param_block pb_unversioned;
    struct param_block_v1 pb_v1;

    /* try to read older, smaller parameter block first */
    if (fread(&pb_unversioned, sizeof(pb_unversioned), 1, f) != 1)
        return -1;

    /* we have to check the parameter block in order due to backwards compatibility */

    /* check whether parameter block has expected magic value at the beginning */
    if (pb_unversioned.sob != htole32(MARKER))
        return PB_READ_ERROR_MAGIC;

    /* if the second marker also matches, then this is probably an old version */
    if (pb_unversioned.eob == htole32(MARKER)) {
        /* let's migrate it without prior looking at the CRC */
        pb_init_v1(&pb_v1);
        pb_migrate_unversioned_to_v1(&pb_unversioned, &pb_v1);

        pb_init_v2(param_block);
        pb_migrate_v1_to_v2(&pb_v1, param_block);

        /* check CRC */
        if (!pb_check_unversioned_param_block_crc(&pb_unversioned))
            return PB_READ_ERROR_CRC;

        return 0;
    }

    /* looks not like an old (unversioned) parameter block, try to append the (missing) data for v1 */
    memcpy(&pb_v1, &pb_unversioned, sizeof(pb_unversioned));

    if (fread((char *)&pb_v1 + sizeof(pb_unversioned),
              sizeof(pb_v1) - sizeof(pb_unversioned), 1, f) != 1)
        return -1;

    /* now let's check whether the second marker matches for v1 and the version number indicates v1 */
    if (pb_v1.eob == htole32(MARKER) && pb_v1.version == 1) {
        /* let's migrate it without prior looking at the CRC */
        pb_init_v2(param_block);
        pb_migrate_v1_to_v2(&pb_v1, param_block);

        /* check CRC */
        if (!pb_check_crc_v1(&pb_v1))
            return PB_READ_ERROR_CRC;

        return 0;
    }

    /* looks not like an older parameter block, try to append the (missing) data */
    memcpy(param_block, &pb_v1, sizeof(pb_v1));

    if (fread((char *)param_block + sizeof(pb_v1),
              sizeof(*param_block) - sizeof(pb_v1), 1, f) != 1)
        return -1;

    /* now check the second magic value */
    if (param_block->eob != htole32(MARKER))
        return PB_READ_ERROR_MAGIC;

    /* check CRC */
    if (!pb_check_crc_v2(param_block))
        return PB_READ_ERROR_CRC;

    return 0;
}

int pb_write(struct param_block_v2 *param_block, FILE *f)
{
    pb_refresh_crc_v2(param_block);

    if (fwrite(param_block, sizeof(*param_block), 1, f) != 1)
        return -1;

    return 0;
}
