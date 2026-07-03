/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gpiod.h>
#include <logging.h>
#include <tools.h>
#include "ra_gpio.h"

struct gpio_line {
    const char *pin;
    char *chip_path;
    unsigned int offset;
    struct gpiod_line_request *request;
};

struct gpio_ctx {
    struct gpio_line rst;
    struct gpio_line md;
    unsigned int rst_duration; /* ms */
};

static int is_gpiochip_name(const struct dirent *entry)
{
    return strncmp(entry->d_name, "gpiochip", strlen("gpiochip")) == 0;
}

static int gpio_line_autodetect(struct gpio_line *line)
{
    struct dirent **entries = NULL;
    int entries_count;
    int rv = -1;
    int i;

    entries_count = scandir("/dev", &entries, is_gpiochip_name, alphasort);
    if (entries_count < 0) {
        error("could not enumerate GPIO chips in '/dev': %m");
        return -1;
    }

    for (i = 0; i < entries_count; i++) {
        char *chip_path = NULL;
        int offset;
        struct gpiod_chip *chip = NULL;

        rv = asprintf(&chip_path, "/dev/%s", entries[i]->d_name);
        if (rv < 0) {
            chip_path = NULL;
            errno = ENOMEM;
            rv = -1;
            goto out;
        }

        chip = gpiod_chip_open(chip_path);
        if (!chip) {
            error("could not open '%s': %m", chip_path);
            free(chip_path);
            chip_path = NULL;
            continue;
        }

        offset = gpiod_chip_get_line_offset_from_name(chip, line->pin);
        gpiod_chip_close(chip);
        if (offset == -1) {
            free(chip_path);
            chip_path = NULL;
            continue;
        }

        line->chip_path = chip_path;
        line->offset = offset;
        rv = 0;
        break;
    }

    if (rv)
        error("could not find GPIO '%s' on any gpiochip device", line->pin);

out:
    for (i = 0; i < entries_count; i++)
        free(entries[i]);
    free(entries);

    return rv;
}

static struct gpiod_line_request *gpio_request_line(const char *chip_path, unsigned int offset)
{
    struct gpiod_chip *chip = NULL;
    struct gpiod_line_settings *line_settings = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *req_config = NULL;
    struct gpiod_line_request *request = NULL;

    chip = gpiod_chip_open(chip_path);
    if (!chip) {
        error("could not open '%s': %m", chip_path);
        goto out;
    }

    line_settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    req_config = gpiod_request_config_new();

    if (!line_settings || !line_config || !req_config)
        goto out;

    gpiod_request_config_set_consumer(req_config, program_invocation_name);

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT))
        goto out;
    if (gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_ACTIVE))
        goto out;

    if (gpiod_line_config_add_line_settings(line_config, &offset, 1, line_settings))
        goto out;

    request = gpiod_chip_request_lines(chip, req_config, line_config);
    if (!request)
        goto out;

out:
    gpiod_request_config_free(req_config);
    gpiod_line_settings_free(line_settings);
    gpiod_line_config_free(line_config);
    gpiod_chip_close(chip);

    return request;
}

struct gpio_ctx *ra_gpio_init(const char *reset_gpioname, const char *md_gpioname)
{
    struct gpio_ctx *ctx = NULL;

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    ctx->rst.pin = reset_gpioname;
    ctx->md.pin = md_gpioname;
    ctx->rst_duration = DEFAULT_RA_RESET_DELAY;

    if (gpio_line_autodetect(&ctx->rst))
        goto err_out;
    if (gpio_line_autodetect(&ctx->md))
        goto err_out;

    if (ctx->rst.offset == ctx->md.offset && strcmp(ctx->rst.chip_path, ctx->md.chip_path) == 0) {
        error("GPIO '%s' and GPIO '%s' resolve to the same line '%s' offset %u",
              ctx->rst.pin, ctx->md.pin, ctx->rst.chip_path, ctx->rst.offset);
        goto err_out;
    }

    ctx->rst.request = gpio_request_line(ctx->rst.chip_path, ctx->rst.offset);
    if (!ctx->rst.request)
        goto err_out;

    ctx->md.request = gpio_request_line(ctx->md.chip_path, ctx->md.offset);
    if (!ctx->md.request)
        goto err_out;

    return ctx;

err_out:
    ra_gpio_close(ctx);
    return NULL;
}

void ra_gpio_close(struct gpio_ctx *ctx)
{
    if (!ctx)
        return;

    if (ctx->rst.request)
        gpiod_line_request_release(ctx->rst.request);
    if (ctx->md.request)
        gpiod_line_request_release(ctx->md.request);

    free(ctx->rst.chip_path);
    free(ctx->md.chip_path);
    free(ctx);
}

static int ra_reset_with_bootmode_selection(struct gpio_ctx *ctx, bool force_bootloader, bool hold_until_signal)
{
    int rv;

    /* set RESET to LOW */
    rv = gpiod_line_request_set_value(ctx->rst.request, ctx->rst.offset, GPIOD_LINE_VALUE_INACTIVE);
    if (rv)
        return rv;

    /* choose boot mode by setting MD */
    rv = gpiod_line_request_set_value(ctx->md.request, ctx->md.offset,
                                      force_bootloader ? GPIOD_LINE_VALUE_INACTIVE : GPIOD_LINE_VALUE_ACTIVE);
    if (rv)
        return rv;

    if (hold_until_signal) {
        pause();
        /* pause sets errno, but here we wanted it so reset errno to zero */
        errno = 0;
    } else {
        /* hold reset for the configured delay */
        rv = usleep(ctx->rst_duration * 1000);
        if (rv)
            return rv;
    }

    /* set RESET to LOW */
    rv = gpiod_line_request_set_value(ctx->rst.request, ctx->rst.offset, GPIOD_LINE_VALUE_ACTIVE);
    if (rv)
        return rv;

    return 0;
}

int ra_reset_to_bootloader(struct gpio_ctx *ctx)
{
    return ra_reset_with_bootmode_selection(ctx, true, false);
}
int ra_reset_to_normal(struct gpio_ctx *ctx)
{
    return ra_reset_with_bootmode_selection(ctx, false, false);
}

int ra_hold_reset(struct gpio_ctx *ctx)
{
    return ra_reset_with_bootmode_selection(ctx, false, true);
}

void ra_set_reset_duration(struct gpio_ctx *ctx, unsigned int rst_duration)
{
    ctx->rst_duration = rst_duration;
}
