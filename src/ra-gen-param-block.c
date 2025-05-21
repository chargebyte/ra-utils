/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is a command line tool which generates a parameter block used by the
 * chargebyte's safety controller on e.g. Charge SOM.
 *
 * Usage: ra-gen-param-block [<options>] <temp1> <temp2> <temp3> <temp4> <contactor1> <contactor2> <estop1> <estop2> <estop3> <filename>
 *
 * The temperatures are the thresholds in [0.1 °C] for each PTx channel of the safety controller.
 * E.g. use the value of 800 for 80.0 °C.
 * To disable a channel, use the special word 'disable'.
 *
 * For contactorX (high-voltage contactors), use 'none', 'with-feedback' or 'without-feedback'.
 *
 * For estopX (emergency stop inputs), use 'disable' or 'active-low'.
 *
 * The parameter block is saved to the file given as last parameter.
 *
 * Options:
 *         -V, --version           print version and exit
 *         -h, --help              print this usage and exit
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <endian.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cb_protocol.h>

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-gen-param-block (unknown version)"
#endif

/* command line options */
static const struct option long_options[] = {
    { "version",            no_argument,            0,      'V' },
    { "help",               no_argument,            0,      'h' },
    {} /* stop condition for iterator */
};

static const char *short_options = "Vh";

/* descriptions for the command line options */
static const char *long_options_descs[] = {
    "print version and exit",
    "print this usage and exit",
    NULL /* stop condition for iterator */
};

static void usage(char *p, int exitcode)
{
    const char **desc = long_options_descs;
    const struct option *op = long_options;
    int max_cmd_len = 0;
    int i;

    fprintf(stderr,
            "%s (%s) -- Command line tool to generate a binary parameter block for the Renesas safety MCU\n\n"
            "Usage: %s [<options>] <temp1> <temp2> <temp3> <temp4> <contactor1> <contactor2> <estop1> <estop2> <estop3> <filename>\n\n"
            "The temperatures are the thresholds in [0.1 °C] for each PTx channel of the safety controller.\n"
            "E.g. use the value of 800 for 80.0 °C.\n"
            "To disable a channel, use the special word 'disable'.\n\n"
            "For contactorX (high-voltage contactors), use 'none', 'with-feedback' or 'without-feedback'.\n\n"
            "For estopX (emergency stop inputs), use 'disable' or 'active-low'.\n\n"
            "The parameter block is saved to the file given as last parameter.\n\n",
            p, PACKAGE_STRING, p);

    fprintf(stderr,
            "Options:\n");

    while (op->name && desc) {
        if (op->val > 1) {
            fprintf(stderr, "\t-%c, --%-12s\t%s\n", op->val, op->name, *desc);
        } else {
            fprintf(stderr, "\t    --%-12s\t%s\n", op->name, *desc);
        }
        op++;
        desc++;
    }

    fprintf(stderr, "\n");

    exit(exitcode);
}

#define MARKER 0xC001F00D
#define CHANNEL_DISABLE_VALUE -32768

struct param_block {
    uint32_t sob;
    int16_t temp[CB_PROTO_MAX_PT1000S];
    uint8_t contactor[CB_PROTO_MAX_CONTACTORS];
    uint8_t estop[CB_PROTO_MAX_ESTOPS];
    uint32_t eob;
    uint8_t crc;
} __attribute__((packed));

/* to keep things easy, we use global variables here */
FILE *f;
struct param_block param_block;

#define ARGC_COUNT (CB_PROTO_MAX_PT1000S + CB_PROTO_MAX_CONTACTORS + CB_PROTO_MAX_ESTOPS + 1 /* filename */)

enum contactor_type {
    CONTACTOR_NONE = 0,
    CONTACTOR_WITHOUT_FEEDBACK,
    CONTACTOR_WITH_FEEDBACK,
    CONTACTOR_MAX,
};

static const char *contactor_type_to_str[CONTACTOR_MAX] = {
    "none",
    "without-feedback",
    "with-feedback",
};

enum contactor_type str_to_contactor_type(const char *s) {
    unsigned int i;

    for (i = CONTACTOR_NONE; i < CONTACTOR_MAX; ++i)
        if (strcasecmp(s, contactor_type_to_str[i]) == 0)
            return i;

    return CONTACTOR_MAX;
}

enum emergeny_stop_type {
    EMERGENY_STOP_NONE = 0,
    EMERGENY_STOP_ACTIVE_LOW,
    EMERGENY_STOP_MAX,
};

static const char *emergeny_stop_to_str[EMERGENY_STOP_MAX] = {
    "disable",
    "active-low",
};

enum emergeny_stop_type str_to_emergeny_stop_type(const char *s) {
    unsigned int i;

    for (i = EMERGENY_STOP_NONE; i < EMERGENY_STOP_MAX; ++i)
        if (strcasecmp(s, emergeny_stop_to_str[i]) == 0)
            return i;

    return EMERGENY_STOP_MAX;
}

void parse_cli(int argc, char *argv[])
{
    int rc = EXIT_FAILURE;
    int i;

    while (1) {
        int c = getopt_long(argc, argv, short_options, long_options, NULL);

        /* detect the end of the options */
        if (c == -1)
            break;

        switch (c) {
        case 'V':
            printf("%s (%s)\n", argv[0], PACKAGE_STRING);
            exit(EXIT_SUCCESS);
        case '?':
        case 'h':
            rc = EXIT_SUCCESS;
            usage(argv[0], rc);
            break;
        case 0:
            /* getopt_long() set a variable by reference */
            break;
        default:
            rc = EXIT_FAILURE;
            fprintf(stderr, "Unknown option '%c'.\n", (char) c);
            usage(argv[0], rc);
        }
    }

    argc -= optind;
    argv += optind;

    /* check whether additional command line arguments were given */
    if (argc != ARGC_COUNT)
        usage(program_invocation_short_name, EXIT_FAILURE);

    for (i = 0; i < CB_PROTO_MAX_PT1000S; ++i) {
        if (strcasecmp(argv[i], "disable") == 0) {
            param_block.temp[i] = htole16(CHANNEL_DISABLE_VALUE);
        } else {
            char *endptr;
            long int val = strtol(argv[i], &endptr, 10);

            if (*endptr != '\0' || val < -800 || val > 2000) {
                fprintf(stderr, "Error: invalid temperature value: %s (allowed range: -80.0 °C - 200.0 °C)\n\n", argv[i]);
                usage(program_invocation_short_name, EXIT_FAILURE);
            }

            param_block.temp[i] = htole16(val);
        }
    }

    argc -= CB_PROTO_MAX_PT1000S;
    argv += CB_PROTO_MAX_PT1000S;

    for (i = 0; i < CB_PROTO_MAX_CONTACTORS; ++i) {
        param_block.contactor[i] = str_to_contactor_type(argv[i]);
        if (param_block.contactor[i] == CONTACTOR_MAX) {
            fprintf(stderr, "Error: invalid contactor specification: %s\n\n", argv[i]);
            usage(program_invocation_short_name, EXIT_FAILURE);
        }
    }

    argc -= CB_PROTO_MAX_CONTACTORS;
    argv += CB_PROTO_MAX_CONTACTORS;

    for (i = 0; i < CB_PROTO_MAX_ESTOPS; ++i) {
        param_block.estop[i] = str_to_emergeny_stop_type(argv[i]);
        if (param_block.estop[i] == EMERGENY_STOP_MAX) {
            fprintf(stderr, "Error: invalid emergency stop specification: %s\n\n", argv[i]);
            usage(program_invocation_short_name, EXIT_FAILURE);
        }
    }

    argc -= CB_PROTO_MAX_ESTOPS;
    argv += CB_PROTO_MAX_ESTOPS;

    f = fopen(argv[0], "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s' writing: %m", argv[0]);
        exit(EXIT_FAILURE);
    }
}

static uint8_t crc8_table[256] = {
    0x00, 0x2f, 0x5e, 0x71, 0xbc, 0x93, 0xe2, 0xcd, 0x57, 0x78, 0x09, 0x26, 0xeb, 0xc4, 0xb5, 0x9a,
    0xae, 0x81, 0xf0, 0xdf, 0x12, 0x3d, 0x4c, 0x63, 0xf9, 0xd6, 0xa7, 0x88, 0x45, 0x6a, 0x1b, 0x34,
    0x73, 0x5c, 0x2d, 0x02, 0xcf, 0xe0, 0x91, 0xbe, 0x24, 0x0b, 0x7a, 0x55, 0x98, 0xb7, 0xc6, 0xe9,
    0xdd, 0xf2, 0x83, 0xac, 0x61, 0x4e, 0x3f, 0x10, 0x8a, 0xa5, 0xd4, 0xfb, 0x36, 0x19, 0x68, 0x47,
    0xe6, 0xc9, 0xb8, 0x97, 0x5a, 0x75, 0x04, 0x2b, 0xb1, 0x9e, 0xef, 0xc0, 0x0d, 0x22, 0x53, 0x7c,
    0x48, 0x67, 0x16, 0x39, 0xf4, 0xdb, 0xaa, 0x85, 0x1f, 0x30, 0x41, 0x6e, 0xa3, 0x8c, 0xfd, 0xd2,
    0x95, 0xba, 0xcb, 0xe4, 0x29, 0x06, 0x77, 0x58, 0xc2, 0xed, 0x9c, 0xb3, 0x7e, 0x51, 0x20, 0x0f,
    0x3b, 0x14, 0x65, 0x4a, 0x87, 0xa8, 0xd9, 0xf6, 0x6c, 0x43, 0x32, 0x1d, 0xd0, 0xff, 0x8e, 0xa1,
    0xe3, 0xcc, 0xbd, 0x92, 0x5f, 0x70, 0x01, 0x2e, 0xb4, 0x9b, 0xea, 0xc5, 0x08, 0x27, 0x56, 0x79,
    0x4d, 0x62, 0x13, 0x3c, 0xf1, 0xde, 0xaf, 0x80, 0x1a, 0x35, 0x44, 0x6b, 0xa6, 0x89, 0xf8, 0xd7,
    0x90, 0xbf, 0xce, 0xe1, 0x2c, 0x03, 0x72, 0x5d, 0xc7, 0xe8, 0x99, 0xb6, 0x7b, 0x54, 0x25, 0x0a,
    0x3e, 0x11, 0x60, 0x4f, 0x82, 0xad, 0xdc, 0xf3, 0x69, 0x46, 0x37, 0x18, 0xd5, 0xfa, 0x8b, 0xa4,
    0x05, 0x2a, 0x5b, 0x74, 0xb9, 0x96, 0xe7, 0xc8, 0x52, 0x7d, 0x0c, 0x23, 0xee, 0xc1, 0xb0, 0x9f,
    0xab, 0x84, 0xf5, 0xda, 0x17, 0x38, 0x49, 0x66, 0xfc, 0xd3, 0xa2, 0x8d, 0x40, 0x6f, 0x1e, 0x31,
    0x76, 0x59, 0x28, 0x07, 0xca, 0xe5, 0x94, 0xbb, 0x21, 0x0e, 0x7f, 0x50, 0x9d, 0xb2, 0xc3, 0xec,
    0xd8, 0xf7, 0x86, 0xa9, 0x64, 0x4b, 0x3a, 0x15, 0x8f, 0xa0, 0xd1, 0xfe, 0x33, 0x1c, 0x6d, 0x42
};

uint8_t crc8(uint8_t *p, size_t len)
{
    const uint8_t *ptr = p;
    uint8_t _crc = 0xff;

    while (len--)
        _crc = crc8_table[_crc ^ *ptr++];

    return ~_crc;
}

int main(int argc, char *argv[])
{
    int rv = EXIT_SUCCESS;

    /* handle command line options */
    parse_cli(argc, argv);

    param_block.sob = htole32(MARKER);
    param_block.eob = htole32(MARKER);
    param_block.crc = crc8((uint8_t *)&param_block, sizeof(param_block) - 1);

    if (fwrite(&param_block, sizeof(param_block), 1, f) != 1) {
        fprintf(stderr, "Error while writing: %m");
        rv = EXIT_FAILURE;

        /* close but do not look at result */
        fclose(f);
    } else {
        if (fclose(f) == EOF) {
            fprintf(stderr, "Error while closing: %m");
            rv = EXIT_FAILURE;
        }
    }

    return rv;
}
