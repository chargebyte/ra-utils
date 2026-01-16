/*
 * Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/* the default gpiochip device to use */
#define DEFAULT_RA_GPIOCHIP "/dev/gpiochip2"

/* the default gpio name of the MCU reset pin */
#define DEFAULT_RA_GPIO_RESET_PIN "nSAFETY_RESET_INT"

/* the default gpio name of the gpio to toggle the MCU boot mode */
#define DEFAULT_RA_GPIO_MD_PIN "SAFETY_BOOTMODE_SET"

/* name of environment variable to override compiled-in DEFAULT_RA_GPIOCHIP */
#define GETENV_GPIOCHIP_KEY "SAFETY_MCU_GPIOCHIP"

/* name of environment variable to override compiled-in DEFAULT_RA_GPIO_RESET_PIN */
#define GETENV_RESET_PIN_KEY "SAFETY_MCU_RESET_GPIO"

/* name of environment variable to override compiled-in DEFAULT_RA_GPIO_MD_PIN */
#define GETENV_MD_PIN_KEY "SAFETY_MCU_MD_GPIO"
