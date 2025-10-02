/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is a command line tool to dump a binary parameter block file as YAML.
 *
 * Usage: ra-pb-dump [<options>] [<filename>]
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
#include "param_block_crc8.h"

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-pb-dump (unknown version)"
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
            "%s (%s) -- Command line tool to dump a parameter block file\n\n"
            "Usage: %s [<options>] [<filename>]\n\n"
            , p, PACKAGE_STRING, p);

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
    if (argc > 1)
        usage(program_invocation_short_name, EXIT_FAILURE);

    if (argc == 1) {
        f = fopen(argv[0], "rb");
        if (!f) {
            fprintf(stderr, "Error: cannot open '%s' for reading: %m\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    } else {
        f = stdin;
    }
}

int main(int argc, char *argv[])
{
    int rv = EXIT_FAILURE;

    /* handle command line options */
    parse_cli(argc, argv);

    /* read parameter block and try to auto-detect version, migrate if necessary */
    switch (pb_read(f, &param_block)) {
    case PB_READ_SUCCESS:
        rv = EXIT_SUCCESS;
        break;

    case PB_READ_ERROR_CRC:
        fprintf(stderr, "Warning: parameter block's CRC is wrong, dumping nevertheless.\n");
        break;

    case PB_READ_ERROR_MAGIC:
        fprintf(stderr, "Error: file does not look like a parameter block.\n");
        goto err_out;

    default:
        if (errno)
            fprintf(stderr, "Error while reading: %m\n");
        goto err_out;
    }

    pb_dump(&param_block);

err_out:
    /* close but do not look at result */
    fclose(f);

    return rv;
}
