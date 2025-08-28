/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* we expect at least one UART frame within this period, in ms */
#define CB_UART_RECV_INTERVAL 1000 /* FIXME */

/* when being async to safety controller, try at least this times to get in sync */
#define CB_UART_MAX_SYNC_TRIALS 3

/* values for COM fields */
enum cb_uart_com {
    COM_INQUIRY                   = 0xff,
    COM_DIGITAL_OUTPUT            = 0x00,
    COM_DIGITAL_INPUT             = 0x01,
    COM_ANALOG_INPUT_01           = 0x02,
    COM_ANALOG_INPUT_02           = 0x03,
    COM_ANALOG_INPUT_03           = 0x04,
    COM_ANALOG_INPUT_04           = 0x05,
    COM_CHARGE_CONTROL            = 0x06,
    COM_CHARGE_STATE              = 0x07,
    COM_PT1000_STATE              = 0x08,
    COM_DIAGNOSTIC_MEASUREMENTS   = 0x09,
    COM_FW_VERSION                = 0x0A,
    COM_GIT_HASH                  = 0x0B,
    COM_ERROR_MESSAGE             = 0x0E,
    COM_CHARGE_STATE_2            = 0x10,
    COM_CHARGE_CONTROL_2          = 0x11,
    COM_DIAGNOSTIC_MEASUREMENTS_2 = 0x12,
    COM_ANALOG_INPUT_05           = 0x13,

    /* special value: keep in sync with above values but ignore INQUIRY */
    COM_MAX = 0x14,
};

/* forward declaration so that it is not necessary to include uart.h completely */
struct uart_ctx;

const char *cb_uart_com_to_str(enum cb_uart_com com);

int cb_uart_send(struct uart_ctx *uart, enum cb_uart_com com, uint64_t data);

int cb_uart_recv(struct uart_ctx *uart, enum cb_uart_com *com, uint64_t *data);

int cb_uart_recv_and_sync(struct uart_ctx *uart, enum cb_uart_com *com, uint64_t *data);

#ifdef __cplusplus
}
#endif
