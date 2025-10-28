/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Structure of the application validation block.
 *
 * This structure contains all information which are necessary to check
 * if the code of an application is valid and can be executed.
 */
struct version_app_infoblock {
    uint32_t start_magic_pattern;  ///< Magic pattern to ensure, that this block is valid
    uint32_t application_size;     ///< Size of the application
    uint32_t application_checksum; ///< Checksum (CRC32) of the application
    uint8_t sw_major_version;      ///< Software major version
    uint8_t sw_minor_version;      ///< Software minor version
    uint8_t sw_build_version;      ///< Software build version
    uint64_t git_hash;             ///< Git hash of the HEAD used to build this SW
    uint8_t sw_platform_type;      ///< Software platform type
    uint8_t sw_application_type;   ///< Software application type
    uint16_t parameter_version;    ///< Expected parameter file version
    uint8_t reserved;              ///< 1 bytes for future use
    uint32_t end_magic_pattern;    ///< Magic pattern to ensure, that this block is valid
} __attribute__((packed));

/* value for start_magic_pattern and end_magic_pattern fields */
#define INFO_MAGIC_PATTERN 0xCAFEBABE

/* location of version_app_infoblock inside the file/flash memory */
#define CODE_FIRMWARE_INFORMATION_START_ADDRESS 0x000003E0
#define CODE_FIRMWARE_INFORMATION_END_ADDRESS   0x000003FF

int fw_mmap_infile(const char *filename, uint8_t **content, unsigned long *filesize);
int fw_mmap_outfile(const char *filename, uint8_t **content, unsigned long filesize);

void fw_version_app_infoblock_to_host_endianess(struct version_app_infoblock *p);

void fw_dump_version_app_infoblock(struct version_app_infoblock *p);
bool fw_valid_version_app_infoblock(struct version_app_infoblock *p);

/* note: inversed logic - returns true in case the infoblock is invalid */
bool fw_print_amended_version_app_infoblock(struct version_app_infoblock *p, const char *header);

/* possible platform types (field 'sw_platform_type') */
#define SW_PLATFORM_TYPE_UNSPECIFIED    0xFF /* erased flash */
#define SW_PLATFORM_TYPE_UNKOWN         0x00
#define SW_PLATFORM_TYPE_DEFAULT        0x81
#define SW_PLATFORM_TYPE_CCY            0x82

/* possible values of field 'sw_application_type' */
#define SW_APPLICATION_TYPE_FIRMWARE    0x03
#define SW_APPLICATION_TYPE_EOL         0x04
#define SW_APPLICATION_TYPE_QUALI       0x05

const char *fw_sw_platform_type_to_str(uint8_t type);
const char *fw_sw_application_type_to_str(uint8_t type);
