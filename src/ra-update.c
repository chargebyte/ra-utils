/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is a command line tool which implements the Renesas bootloader protocol.
 * Main purpose is to update the so called "safety controller" which is
 * used on various chargebyte boards, including the Charge SOM.
 * This implementation follows: Standard_Boot_Firmware.pdf that can be found
 * on chargebyte's internal NAS, or e.g. publicly available here:
 * https://www.renesas.com/us/en/document/apn/renesas-ra-family-system-specifications-standard-boot-firmware-0
 *
 * Usage: ra-update [<options>] <command> [<parameter>...]
 *
 * Commands:
 *         reset                -- reset MCU and exit
 *         hold-in-reset        -- reset MCU, hold reset until Ctrl+C is pressed, then release reset and exit
 *         bootloader           -- reset MCU and force bootloader mode
 *         fw_info [<filename>] -- print firmware info (if the  optional filename is given, read the info from this file)
 *         chipinfo             -- print chip info
 *         erase                -- erase MCU's flash
 *         flash <filename>     -- write given filename to MCU's flash
 *
 * Options:
 *         -c, --gpiochip          GPIO chip device (default: /dev/gpiochip2)
 *         -r, --reset-gpio        GPIO name for controlling RESET pin of MCU (default: nSAFETY_RESET_INT)
 *         -m, --md-gpio           GPIO name for controlling MD pin of MCU (default: SAFETY_BOOTMODE_SET)
 *         -d, --uart              UART interface (default: /dev/ttyLP2)
 *         -p, --reset-period      reset duration (in ms, default: 500)
 *         -a, --flash-area        target flash area (code or data, default: code)
 *         -v, --verbose           verbose operation
 *         -V, --version           print version and exit
 *         -h, --help              print this usage and exit
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ra_protocol.h>
#include <cb_protocol.h>
#include <logging.h>
#include <tools.h>
#include <uart.h>
#include "fw_file.h"
#include "ra_gpio.h"
#include "stringify.h"

/* default uart interface */
#define DEFAULT_UART_INTERFACE "/dev/ttyLP2"

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-utils (unknown version)"
#endif

/* the possible command arguments which are understood by this tool */
enum cmd {
    CMD_RESET,
    CMD_HOLD_IN_RESET,
    CMD_BOOTLOADER,
    CMD_FW_INFO,
    CMD_CHIPINFO,
    CMD_ERASE,
    CMD_FLASH,
    CMD_MAX
};

static const char *cmd_strings[CMD_MAX] = {
    "reset",
    "hold-in-reset",
    "bootloader",
    "fw_info",
    "chipinfo",
    "erase",
    "flash",
};

static const char *cmd_args[CMD_MAX] = {
    NULL,
    NULL,
    NULL,
    "[<filename>]",
    NULL,
    NULL,
    "<filename>",
};

static const char *cmd_descs[CMD_MAX] = {
    "reset MCU and exit",
    "reset MCU, hold reset until Ctrl+C is pressed, then release reset and exit",
    "reset MCU and force bootloader mode",
    "print firmware info (if the  optional filename is given, read the info from this file)",
    "print chip info",
    "erase MCU's flash",
    "write given filename to MCU's flash",
};

/* command line options */
static const struct option long_options[] = {
    { "gpiochip",           required_argument,      0,      'c' },
    { "reset-gpio"   ,      required_argument,      0,      'r' },
    { "md-gpio",            required_argument,      0,      'm' },
    { "uart",               required_argument,      0,      'd' },
    { "reset-period",       required_argument,      0,      'p' },
    { "flash-area",         required_argument,      0,      'a' },

    { "verbose",            no_argument,            0,      'v' },
    { "version",            no_argument,            0,      'V' },
    { "help",               no_argument,            0,      'h' },
    {} /* stop condition for iterator */
};

static const char *short_options = "c:r:m:d:p:a:vVh";

/* descriptions for the command line options */
static const char *long_options_descs[] = {
    "GPIO chip device (default: " DEFAULT_RA_GPIOCHIP ")",
    "GPIO name for controlling RESET pin of MCU (default: " DEFAULT_RA_GPIO_RESET_PIN ")",
    "GPIO name for controlling MD pin of MCU (default: " DEFAULT_RA_GPIO_MD_PIN ")",
    "UART interface (default: " DEFAULT_UART_INTERFACE ")",
    "reset duration (in ms, default: " __stringify(DEFAULT_RA_RESET_DELAY) ")",
    "target flash area (code or data, default: code)",

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
            "%s (%s) -- Command line tool to control the Renesas safety MCU\n\n"
            "Usage: %s [<options>] <command> [<parameter>...]\n\n"
            "Commands:\n",
            p, PACKAGE_STRING, p);

    /* determine the maximum length over all commands - we need it for pretty printing */
    for (i = 0; i < CMD_MAX; i++)
        max_cmd_len = max(max_cmd_len, strlen(cmd_strings[i]) + (cmd_args[i] ? strlen(cmd_args[i]) : 0));

    for (i = 0; i < CMD_MAX; i++) {
        int field_length = max_cmd_len - strlen(cmd_strings[i]);

        fprintf(stderr, "\t%s %-*s -- %s\n", cmd_strings[i], field_length, cmd_args[i] ?: "", cmd_descs[i]);
    }

    fprintf(stderr,
            "\n"
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
static char *gpiochip = DEFAULT_RA_GPIOCHIP;
static char *reset_gpioname = DEFAULT_RA_GPIO_RESET_PIN;
static char *md_gpioname = DEFAULT_RA_GPIO_MD_PIN;
static char *uart_device = DEFAULT_UART_INTERFACE;
static unsigned int reset_duration = DEFAULT_RA_RESET_DELAY;
static enum cmd cmd = CMD_MAX;
static char *fw_filename = NULL;
static struct ra_chipinfo chipinfo;
static struct ra_flash_area_info *flash_area_info = &chipinfo.code; /* default to code */

static void debug_cb(const char *format, va_list args)
{
    if (verbose) {
        printf("debug: ");
        vprintf(format, args);
        printf("\n");
    }
}

static void error_cb(const char *format, va_list args)
{
    fprintf(stderr, "Error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

static void xerror(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    error_cb(format, args);
    va_end(args);
}

static void xdebug(const char *format, ...)
{
    if (verbose) {
        va_list args;
        va_start(args, format);
        debug_cb(format, args);
        va_end(args);
    }
}

static void xprint(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    printf("\n");
    va_end(args);
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

        case 'c':
            gpiochip = optarg;
            break;
        case 'r':
            reset_gpioname = optarg;
            break;
        case 'm':
            md_gpioname = optarg;
            break;
        case 'd':
            uart_device = optarg;
            break;
        case 'p':
            reset_duration = atoi(optarg);
            break;
        case 'a':
            if (strcasecmp(optarg, "code") == 0) {
                flash_area_info = &chipinfo.code;
            } else if (strcasecmp(optarg, "data") == 0) {
                flash_area_info = &chipinfo.data;
            } else {
                fprintf(stderr, "Unknown flash-area '%s'.\n", optarg);
                usage(argv[0], rc);
            }
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
            fprintf(stderr, "Unknown option '%c'.\n", (char)c);
            usage(argv[0], rc);
        }
    }

    argc -= optind;
    argv += optind;

    /* check that a command was given as command line argument */
    if (argc < 1)
        usage(program_invocation_short_name, EXIT_FAILURE);

    /* check which command  was requested */
    for (i = 0; i < CMD_MAX; i++) {
        if (strcasecmp(argv[0], cmd_strings[i]) == 0) {
            cmd = i;
            break;
        }
    }
    /* bail out if unknown command was given */
    if (i == CMD_MAX)
        usage(program_invocation_short_name, EXIT_FAILURE);

    /* adjust command line stuff */
    argc -= 1;
    argv += 1;

    /* the flash command requires a second argument */
    if (cmd == CMD_FLASH) {
        if (argc == 1) {
            fw_filename = argv[0];
            return;
        }
        usage(program_invocation_short_name, EXIT_FAILURE);
    }
    /* for fw_info it is optional */
    if (cmd == CMD_FW_INFO) {
        if (argc == 1) {
            fw_filename = argv[0];
            return;
        }
        if (argc == 0)
            return;
    }
    /* anything else does not take any arguments */
    if (argc)
        usage(program_invocation_short_name, EXIT_FAILURE);
}

static int setup_uart_communication(struct gpio_ctx *gpio, struct uart_ctx *uart)
{
    int rv;

    rv = ra_reset_to_bootloader(gpio);
    if (rv) {
        xerror("forcing into bootloader failed: %m");
        return -1;
    }

    /* we must open the UART with fixed baudrate in this bootmode */
    rv = uart_open(uart, uart_device, 9600);
    if (rv) {
        xerror("opening '%s' failed: %m", uart_device);
        return -1;
    }

    rv = ra_comm_setup(uart);
    if (rv) {
        xerror("communication setup with MCU failed: %m");
        return -1;
    }

    /* the manual proposes to send an inquiry command now and check for the correct response */
    rv = ra_inquiry(uart);
    if (rv) {
        xerror("inquiry command before baudrate change failed: %m");
        return -1;
    }

    /* now let's upgrade the baudrate */
    rv = ra_set_baudrate(uart, 115200);
    if (rv) {
        xerror("changing the baudrate from 9600 to 115200 failed: %m");
        return -1;
    }

    xdebug("switching baudrate now");

    rv = uart_reconfigure_baudrate(uart, 115200);
    if (rv) {
        xerror("switching UART baudrate to 115200 failed: %m");
        return -1;
    }

    usleep(10000);

    rv = ra_inquiry(uart);
    if (rv) {
        xerror("inquiry command after baudrate change failed: %m");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    struct version_app_infoblock version_info;
    struct uart_ctx uart = { .fd = -1 };
    struct gpio_ctx *gpio = NULL;
    uint8_t *fw_content = NULL;
    unsigned long fw_filesize = 0;
    bool reset_to_normal_on_exit = false;
    int rc = EXIT_FAILURE;
    int rv;

    /* handle command line options */
    parse_cli(argc, argv);

    /* register debug and error message callbacks */
    ra_utils_set_error_msg_cb(error_cb);
    ra_utils_set_debug_msg_cb(debug_cb);

    /* we need the GPIO stuff always except when only printing the fw_info from a file */
    if (!(cmd == CMD_FW_INFO && fw_filename)) {
        gpio = ra_gpio_init(gpiochip, reset_gpioname, md_gpioname);
        if (!gpio) {
            xerror("could not acquire GPIOs: %m");
            goto close_out;
        }

        ra_set_reset_duration(gpio, reset_duration);
    }

    /* if fw_filename is set, then make the file content via mmap available */
    if (fw_filename) {
        rv = fw_mmap(fw_filename, &fw_content, &fw_filesize);
        if (rv) {
            xerror("Could not open '%s': %m", fw_filename);
            goto close_out;
        }
    }

    switch (cmd) {
    case CMD_RESET:
        rv = ra_reset_to_normal(gpio);
        if (rv) {
            xerror("reset failed: %m");
            goto close_out;
        }
        break;

    case CMD_HOLD_IN_RESET:
        rv = ra_hold_reset(gpio);
        if (rv) {
            xerror("reset failed: %m");
            goto close_out;
        }
        break;

    case CMD_BOOTLOADER:
        rv = ra_reset_to_bootloader(gpio);
        if (rv) {
            xerror("forcing into bootloader failed: %m");
            goto close_out;
        }
        break;

    case CMD_FW_INFO:
        /* read and dump info block from given file */
        if (fw_filename) {
            /* we create a temporary copy since we do not want to/cannot touch the memory mapped file */
            memcpy(&version_info, &fw_content[CODE_FIRMWARE_INFORMATION_START_ADDRESS], sizeof(version_info));
        } else {
            rv = setup_uart_communication(gpio, &uart);
            if (rv) {
                /* no error logging here required, already done */
                goto reset_to_normal_out;
            }

            rv = ra_get_chipinfo(&uart, &chipinfo, verbose);
            if (rv) {
                /* no error logging here required, already done */
                goto reset_to_normal_out;
            }

            rv = ra_read(&uart, (uint8_t *)&version_info,
                         chipinfo.code.start_address + CODE_FIRMWARE_INFORMATION_START_ADDRESS, sizeof(version_info));
            if (rv) {
                xerror("reading version app infoblock failed: %m");
                goto reset_to_normal_out;
            }
        }

        fw_version_app_infoblock_to_host_endianess(&version_info);

        if (fw_print_amended_version_app_infoblock(&version_info, fw_filename ?: "Current MCU Firmware")) {
            /* looks invalid so jump out with EXIT_FAILURE */
            if (fw_filename)
                goto close_out;
            else
                goto reset_to_normal_out;
        }

        if (!fw_filename)
            reset_to_normal_on_exit = true;
        break;

    case CMD_ERASE:
    case CMD_FLASH:
        rv = setup_uart_communication(gpio, &uart);
        if (rv) {
            /* no error logging here required, already done */
            goto reset_to_normal_out;
        }

        rv = ra_get_chipinfo(&uart, &chipinfo, verbose);
        if (rv) {
            /* no error logging here required, already done */
            goto reset_to_normal_out;
        }

        /* before we do anything, let's check the filesize */
        if (cmd == CMD_FLASH) {
            /* it must not be larger than the area */
            if (fw_filesize == 0) {
                xerror("This file cannot be flashed, it is empty (length is zero).");
                goto reset_to_normal_out;
            }
            if (fw_filesize > flash_area_info->size) {
                xerror("This file cannot be flashed, it is too large (maximum possible size: %zu bytes).",
                       flash_area_info->size);
                goto reset_to_normal_out;
            }
            /* we require it to match the write unit size */
            if (fw_filesize % flash_area_info->write_unit_size != 0) {
                xerror("This file cannot be flashed. The file's size must be divisible by %zu without a remainder.",
                       flash_area_info->write_unit_size);
                goto reset_to_normal_out;
            }
        }

        /* to keep it simple, we erase the whole area */
        rv = ra_rwe_cmd(&uart, RWE_ERASE, flash_area_info->start_address, flash_area_info->end_address);
        if (rv) {
            xerror("Erasing the MCU's flash memory failed: %m");
            goto reset_to_normal_out;
        }

        if (cmd == CMD_FLASH) {
            rv = ra_write(&uart, flash_area_info->start_address, fw_content, fw_filesize);
            if (rv) {
                xerror("Flashing the file failed: %m");
                goto reset_to_normal_out;
            }
        }

        reset_to_normal_on_exit = true;
        break;

    case CMD_CHIPINFO:
        rv = setup_uart_communication(gpio, &uart);
        if (rv) {
            /* no error logging here required, already done */
            goto reset_to_normal_out;
        }

        ra_get_chipinfo(&uart, &chipinfo, true);

        reset_to_normal_on_exit = true;
        break;

    default:
        xerror("unknown command");
    }

    rc = EXIT_SUCCESS;
    if (!reset_to_normal_on_exit)
        goto close_out;

reset_to_normal_out:
    if (gpio) {
        rv = ra_reset_to_normal(gpio);
        if (rv)
            xerror("resetting into normal mode failed: %m");

        /* when successfully reseted, sleep until controller is ready again */
        if (!rv)
            msleep(CB_PROTO_STARTUP_DELAY);
    }

close_out:
    if (uart.fd != -1) {
        rv = uart_close(&uart);
        if (rv)
            xerror("closing UART failed: %m");
    }
    if (gpio)
        ra_gpio_close(gpio);

    return rc;
}
