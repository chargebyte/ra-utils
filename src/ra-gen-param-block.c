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
#include <version.h>
#include "param_block.h"

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

/* to keep things easy, we use global variables here */
FILE *f;
struct param_block param_block;

#define ARGC_COUNT (CB_PROTO_MAX_PT1000S + CB_PROTO_MAX_CONTACTORS + CB_PROTO_MAX_ESTOPS + 1 /* filename */)

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
            param_block.temperature[i] = htole16(CHANNEL_DISABLE_VALUE);
        } else {
            char *endptr;
            long int val = strtol(argv[i], &endptr, 10);

            if (*endptr != '\0' || val < -800 || val > 2000) {
                fprintf(stderr, "Error: invalid temperature value: %s (allowed range: -80.0 °C - 200.0 °C)\n\n", argv[i]);
                usage(program_invocation_short_name, EXIT_FAILURE);
            }

            param_block.temperature[i] = htole16(val);
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
        fprintf(stderr, "Error: cannot open '%s' for writing: %m\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    int rv = EXIT_SUCCESS;

    /* init parameter block with all disabled */
    pb_init(&param_block);

    /* handle command line options */
    parse_cli(argc, argv);

    /* update CRC */
    pb_refresh_crc(&param_block);

    if (fwrite(&param_block, sizeof(param_block), 1, f) != 1) {
        fprintf(stderr, "Error while writing: %m\n");
        rv = EXIT_FAILURE;

        /* close but do not look at result */
        fclose(f);
    } else {
        if (fclose(f) == EOF) {
            fprintf(stderr, "Error while closing: %m\n");
            rv = EXIT_FAILURE;
        }
    }

    return rv;
}
