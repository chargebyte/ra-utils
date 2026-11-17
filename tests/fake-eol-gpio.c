/* Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 * GPIO substitute for exercising Ctrl-R without physical hardware.
 */
#include "ra_gpio.h"
struct gpio_ctx { int unused; };
static struct gpio_ctx gpio;
struct gpio_ctx *ra_gpio_init(const char *reset, const char *md)
{
    (void)reset; (void)md;
    return &gpio;
}
void ra_gpio_close(struct gpio_ctx *ctx) { (void)ctx; }
void ra_set_reset_duration(struct gpio_ctx *ctx, unsigned int duration) { (void)ctx; (void)duration; }
int ra_reset_to_normal(struct gpio_ctx *ctx) { (void)ctx; return 0; }
