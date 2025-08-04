/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/* default uart interface */
#define DEFAULT_UART_INTERFACE "/dev/ttyLP2"

/* name of environment variable to override compiled-in DEFAULT_UART_INTERFACE */
#define GETENV_UART_KEY "SAFETY_MCU_UART"
