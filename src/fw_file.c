/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <endian.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <tools.h>
#include "fw_file.h"

const char *fw_sw_platform_type_to_str(uint8_t type)
{
    switch (type) {
    case SW_PLATFORM_TYPE_UNSPECIFIED:
        return "unspecified";
    case SW_PLATFORM_TYPE_UNKOWN:
        return "unknown";
    case SW_PLATFORM_TYPE_DEFAULT:
        return "default";
    case SW_PLATFORM_TYPE_CCY:
        return "CCY";
    default:
        return "unknown value";
    }
}

const char *fw_sw_application_type_to_str(uint8_t type)
{
    switch (type) {
    case SW_APPLICATION_TYPE_FIRMWARE:
        return "firmware";
    case SW_APPLICATION_TYPE_EOL:
        return "eol";
    case SW_APPLICATION_TYPE_QUALI:
        return "qualification";
    default:
        return "unknown";
    }
}

int fw_mmap_infile(const char *filename, uint8_t **content, unsigned long *filesize)
{
    struct stat fileinfo;
    int saved_errno = 0, fd = -1, rv;

    fd = open(filename, O_RDONLY);
    if (fd == -1)
        return -1;

    rv = fstat(fd, &fileinfo);
    if (rv) {
        saved_errno = errno;
        goto err_out;
    }

    *content = mmap(NULL, fileinfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (*content == MAP_FAILED) {
        saved_errno = errno;
        rv = -1;
        goto err_out;
    }

    /* successfully memory mapped */
    *filesize = fileinfo.st_size;
    rv = 0;

    /* fd can be closed without affecting the memory map */

err_out:
    if (fd != -1)
        close(fd);

    errno = saved_errno;

    return rv;
}

int fw_mmap_outfile(const char *filename, uint8_t **content, unsigned long filesize)
{
    int saved_errno = 0, fd = -1, rv;

    fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
        return -1;

    rv = ftruncate(fd, filesize);
    if (rv) {
        saved_errno = errno;
        goto err_out;
    }

    *content = mmap(NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (*content == MAP_FAILED) {
        saved_errno = errno;
        rv = -1;
        goto err_out;
    }

    /* successfully memory mapped */
    rv = 0;

    /* fd can be closed without affecting the memory map */

err_out:
    if (fd != -1)
        close(fd);

    errno = saved_errno;

    return rv;
}

void fw_version_app_infoblock_to_host_endianess(struct version_app_infoblock *p)
{
    p->start_magic_pattern = le32toh(p->start_magic_pattern);
    p->application_size = le32toh(p->application_size);
    p->application_checksum = le32toh(p->application_checksum);
    p->git_hash = le64toh(p->git_hash);
    p->end_magic_pattern = le32toh(p->end_magic_pattern);
}

bool fw_valid_version_app_infoblock(struct version_app_infoblock *p)
{
    return p->start_magic_pattern == INFO_MAGIC_PATTERN
           && p->end_magic_pattern == INFO_MAGIC_PATTERN;
}

void fw_dump_version_app_infoblock(struct version_app_infoblock *p)
{
    printf("Start Magic Pattern:       0x%08" PRIx32 "\n", p->start_magic_pattern);
    printf("Firmware Size:             %" PRIu32 " (0x%0*" PRIx32 ")\n",
           p->application_size,
           (p->application_size > 0xffff) ? 8 : 4,
           p->application_size);
    printf("Firmware Checksum (CRC32): 0x%08" PRIx32 "\n", p->application_checksum);

    printf("Firmware Version:          %" PRIu8 ".%" PRIu8 ".%" PRIu8 "\n",
           p->sw_major_version, p->sw_minor_version, p->sw_build_version);
    printf("Firmware Platform Type:    %s (0x%02" PRIx8 ")\n",
           fw_sw_platform_type_to_str(p->sw_platform_type), p->sw_platform_type);
    printf("Firmware Application Type: %s (0x%02" PRIx8 ")\n",
            fw_sw_application_type_to_str(p->sw_application_type), p->sw_application_type);

    printf("Git Hash:                  %016" PRIx64 "\n", p->git_hash);

    printf("End Magic Pattern:         0x%08" PRIx32 "\n", p->end_magic_pattern);
}

bool fw_print_amended_version_app_infoblock(struct version_app_infoblock *p, const char *header)
{
    /*
     * Example output of fw_dump_version_app_infoblock:
     * Start Magic Pattern:       0xcafebabe
     * Firmware Size:             23248
     * Firmware Checksum (CRC32): 0xb6ca0819
     * Firmware Version:          0.1.0
     * Firmware Platform Type:    default (0x81)
     * Firmware Application Type: qualification (0x05)
     * Git Hash:                  a9653ba5c34eeba8
     * End Magic Pattern:         0xcafebabe
     *
     * We enclose it a little bit to make it nice.
     */
    const char *padding = "===============================================";
    bool is_valid = fw_valid_version_app_infoblock(p);
    int padding_length = strlen(padding) - 6 - strlen(header);

    printf("==[ %s ]%*.*s\n", header, padding_length, padding_length, padding);
    fw_dump_version_app_infoblock(p);

    padding_length = strlen(padding) - (is_valid ? 5 : 7) - 4 - 2;
    printf("%*.*s[ %s ]==\n", padding_length, padding_length, padding, is_valid ? "VALID" : "INVALID");

    return !is_valid;
}
