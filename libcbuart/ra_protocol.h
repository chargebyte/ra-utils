/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
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

/* Note: the following structures and their fields are documented in the
 * Renesas RA family's system specification document for the standard boot firmware
 */

struct signature_rsp {
    uint8_t sod;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t res;
    uint32_t sci;
    uint32_t rmb;
    uint8_t noa;
    uint8_t typ;
    union {
        struct {
            uint8_t bfv_major;
            uint8_t bfv_minor;
        } __attribute__((packed));
        uint16_t bfv;
    } __attribute__((packed));
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

int ra_get_signature(struct uart_ctx *uart, struct signature_rsp *signatur_rsp);

struct area_info_rsp {
    uint8_t sod;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t res;
    uint8_t koa;
    uint32_t sad;
    uint32_t ead;
    uint32_t eau;
    uint32_t wau;
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

int ra_get_area_info(struct uart_ctx *uart, uint8_t num, struct area_info_rsp *area_info_rsp);

/* kind of area types */
enum koa_type {
    KOA_USER_AREA_IN_CODE_FLASH,
    KOA_USER_AREA_IN_DATA_FLASH,
    KOA_CONFIG_AREA,
    KOA_TYPE_MAX
};

const char *koa_str(enum koa_type koa);

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

/* retrieve chip information */
struct ra_flash_area_info {
    uint32_t start_address;
    uint32_t end_address;
    size_t size;
    size_t erase_unit_size;
    size_t write_unit_size;
};

struct ra_chipinfo {
    struct ra_flash_area_info code;
    struct ra_flash_area_info data;
};

int ra_get_chipinfo(struct uart_ctx *uart, struct ra_chipinfo *info, bool verbose);
