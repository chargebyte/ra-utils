/*
 * Copyright © 2024 chargebyte GmbH
 */
#pragma once

#include <gpiod.h>

/* the default gpiochip device to use */
#define DEFAULT_RA_GPIOCHIP "/dev/gpiochip2"

/* the default gpio name of the MCU reset pin */
#define DEFAULT_RA_GPIO_RESET_PIN "nSAFETY_RESET_INT"

/* the default gpio name of the gpio to toogle the MCU boot mode */
#define DEFAULT_RA_GPIO_MD_PIN "SAFETY_BOOTMODE_SET"

/* forward declaration */
struct gpio_ctx;

/* ms */
#define DEFAULT_RA_RESET_DELAY 500

struct gpio_ctx *ra_gpio_init(const char *gpiochip, const char *reset_gpioname, const char *md_gpioname);
void ra_gpio_close(struct gpio_ctx *ctx);

int ra_reset_to_bootloader(struct gpio_ctx *ctx);
int ra_reset_to_normal(struct gpio_ctx *ctx);
int ra_hold_reset(struct gpio_ctx *ctx);

void ra_set_reset_duration(struct gpio_ctx *ctx, unsigned int rst_duration);
