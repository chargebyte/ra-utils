/*
 * Copyright © 2024 chargebyte GmbH
 */
#pragma once

#include <stdint.h>
#include "uart.h"

/*
 * environment setup functions
 */

int ra_comm_setup(struct uart_ctx *ctx);
int ra_set_baudrate(struct uart_ctx *uart, int baudrate);

/*
 * low-level functions
 */

int ra_inquiry(struct uart_ctx *uart);

enum rwe_command {
    RWE_ERASE,
    RWE_WRITE,
    RWE_READ,
};

int ra_rwe_cmd(struct uart_ctx *uart, enum rwe_command rwe, uint32_t start_addr, uint32_t end_addr);

/* only accepts payloads up to 1024 byte */
int ra_write_data(struct uart_ctx *uart, const uint8_t *payload, size_t len);

/* read at max 1024 from given address, confirm receipt is case of ack=1 */
int ra_read_data(struct uart_ctx *uart, uint8_t *buffer, size_t bufsize, bool ack);

/*
 * high-level functions
 */

int ra_read(struct uart_ctx *uart, uint8_t *buffer, uint32_t start_addr, size_t len);
int ra_write(struct uart_ctx *uart, uint32_t start_addr, uint8_t *buffer, size_t len);
