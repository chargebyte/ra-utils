/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <gpiod.h>
#include <logging.h>
#include <tools.h>
#include "ra_gpio.h"

struct gpio_ctx {
    const char *gpiochip;
    const char *rst_pin;
    const char *md_pin;
    unsigned int rst_duration; /* ms */

    struct gpiod_line_request *line_request;
    unsigned int rst_offset;
    unsigned int md_offset;
};

struct gpio_ctx *ra_gpio_init(const char *gpiochip, const char *reset_gpioname, const char *md_gpioname)
{
    struct gpio_ctx *ctx = NULL;
    struct gpiod_chip *chip = NULL;
    struct gpiod_line_settings *line_settings = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *req_config = NULL;

    ctx = (struct gpio_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    /* remember the parameters and setup defaults */
    ctx->gpiochip = gpiochip;
    ctx->rst_pin = reset_gpioname;
    ctx->md_pin = md_gpioname;
    ctx->rst_duration = DEFAULT_RA_RESET_DELAY;

    chip = gpiod_chip_open(ctx->gpiochip);
    if (!chip) {
        error("could not open '%s': %m", gpiochip);
        goto err_out;
    }

    ctx->rst_offset = gpiod_chip_get_line_offset_from_name(chip, ctx->rst_pin);
    if (ctx->rst_offset == -1) {
        error("could not use GPIO '%s' for RESET control: %m", ctx->rst_pin);
        goto err_out;
    }

    ctx->md_offset = gpiod_chip_get_line_offset_from_name(chip, ctx->md_pin);
    if (ctx->md_offset == -1) {
        error("could not use GPIO '%s' for MD control: %m", ctx->md_pin);
        goto err_out;
    }

    line_settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    req_config = gpiod_request_config_new();

    if (!line_settings || !line_config || !req_config)
        goto err_out;

    gpiod_request_config_set_consumer(req_config, program_invocation_name);

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT))
        goto err_out;
    if (gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_ACTIVE))
        goto err_out;

    if (gpiod_line_config_add_line_settings(line_config, &ctx->rst_offset, 1, line_settings))
        goto err_out;
    if (gpiod_line_config_add_line_settings(line_config, &ctx->md_offset, 1, line_settings))
        goto err_out;

    ctx->line_request = gpiod_chip_request_lines(chip, req_config, line_config);

    gpiod_request_config_free(req_config);
    gpiod_line_settings_free(line_settings);
    gpiod_line_config_free(line_config);
    gpiod_chip_close(chip);

    return ctx;

err_out:
    if (ctx)
        gpiod_line_request_release(ctx->line_request);
    gpiod_line_settings_free(line_settings);
    gpiod_line_config_free(line_config);
    gpiod_request_config_free(req_config);
    gpiod_chip_close(chip);
    free(ctx);
    return NULL;
}

void ra_gpio_close(struct gpio_ctx *ctx)
{
    if (ctx)
        gpiod_line_request_release(ctx->line_request);
    free(ctx);
}

static int ra_reset_with_bootmode_selection(struct gpio_ctx *ctx, bool force_bootloader, bool hold_until_signal)
{
    int rv;

    /* set RESET to LOW */
    rv = gpiod_line_request_set_value(ctx->line_request, ctx->rst_offset, GPIOD_LINE_VALUE_INACTIVE);
    if (rv)
        return rv;

    /* choose boot mode by setting MD */
    rv = gpiod_line_request_set_value(ctx->line_request, ctx->md_offset,
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
    rv = gpiod_line_request_set_value(ctx->line_request, ctx->rst_offset, GPIOD_LINE_VALUE_ACTIVE);
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
