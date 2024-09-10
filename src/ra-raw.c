/*
 * Copyright © 2024 chargebyte GmbH
 *
 * This is a command line tool which implements the low-level UART protocol
 * of chargebyte's safety controller on e.g. Charge SOM.
 * It aims to support engineering validation or debug cases.
 *
 * Usage: ra-raw [<options>]
 *
 * Options:
 *         -d, --uart              UART interface (default: /dev/ttyLP2)
 *         -v, --verbose           verbose operation
 *         -V, --version           print version and exit
 *         -h, --help              print this usage and exit
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cb_protocol.h"
#include "stringify.h"
#include "tools.h"
#include "uart.h"

/* default uart interface */
#define DEFAULT_UART_INTERFACE "/dev/ttyLP2"

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-utils (unknown version)"
#endif

/* command line options */
static const struct option long_options[] = {
    { "uart",               required_argument,      0,      'd' },

    { "verbose",            no_argument,            0,      'v' },
    { "version",            no_argument,            0,      'V' },
    { "help",               no_argument,            0,      'h' },
    {} /* stop condition for iterator */
};

static const char *short_options = "d:vVh";

/* descriptions for the command line options */
static const char *long_options_descs[] = {
    "UART interface (default: " DEFAULT_UART_INTERFACE ")",

    "verbose operation",
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
            "%s (%s) -- Command line tool to retrieve raw values of the Renesas safety MCU\n\n"
            "Usage: %s [<options>]\n\n",
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

/* to simplify, this is global */
static bool verbose = false;

/* here, too - to simplify, use these as globals */
static char *uart_device = DEFAULT_UART_INTERFACE;

void debug(const char *format, ...)
{
    if (verbose) {
        va_list args;
        va_start(args, format);
        printf("debug: ");
        vprintf(format, args);
        printf("\n");
        va_end(args);
    }
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
        case 'd':
            uart_device = optarg;
            break;

        case 'v':
            verbose = true;
            break;
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
    if (argc != 0)
        usage(program_invocation_short_name, EXIT_FAILURE);
}


int main(int argc, char *argv[])
{
    struct safety_controller data = { .duty_cycle = 5, .pp_sae_iec = true };
    struct uart_ctx uart = { .fd = -1 };
    int rc = EXIT_FAILURE;
    int rv;

    /* handle command line options */
    parse_cli(argc, argv);

    /* the baudrate of the MCU with running firmware should be 115200 */
    rv = uart_open(&uart, uart_device, 115200);
    if (rv) {
        error("opening '%s' failed: %m", uart_device);
        return -1;
    }

    while (1) {
        /* (re-)enable PP pull-up: since we read the hardware state back
         * we must prevent that it is turned off by accident */
        data.pp_sae_iec = true;

        /* query all data to our stucture */
        rv = cb_single_run(&uart, &data);
        if (rv) {
            error("Error while retrieving data: %m");
            goto close_out;
        }

        /* clear screen (in verbose mode, this does not make sense) */
        if (!verbose)
            printf("\033[H\033[J");

        /* dump it */
        cb_dump_data(&data);
    }

    rc = EXIT_SUCCESS;

close_out:
    if (uart.fd != -1) {
        rv = uart_close(&uart);
        if (rv)
            error("closing UART failed: %m");
    }

    return rc;
}
