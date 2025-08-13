/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * This is a command line tool which implements the UART protocol
 * of chargebyte's safety controller on e.g. Charge SOM.
 * It aims to support engineering validation or debug cases.
 *
 * Usage: ra-raw [<options>]
 *
 *  Options:
 *          -d, --uart              UART interface (default: /dev/ttyLP2)
 *          -S, --sync              initial receive sync (default: send packet first)
 *          -D, --no-dump           don't dump data (useful only in verbose mode to print only received frames)
 *          -C, --no-charge-control don't send Charge Control frames automatically
 *          -c, --gpiochip          GPIO chip device (default: /dev/gpiochip2)
 *          -r, --reset-gpio        GPIO name for controlling RESET pin of MCU (default: nSAFETY_RESET_INT)
 *          -m, --md-gpio           GPIO name for controlling MD pin of MCU (default: SAFETY_BOOTMODE_SET)
 *          -p, --reset-period      reset duration (in ms, default: 500)
 *          -R, --no-reset          don't reset the safety controller before starting UART communication
 *          -v, --verbose           verbose operation
 *          -V, --version           print version and exit
 *          -h, --help              print this usage and exit
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <endian.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <cb_protocol.h>
#include <cb_uart.h>
#include <logging.h>
#include <tools.h>
#include <uart.h>
#include <version.h>
#include "ra_gpio.h"
#include "stringify.h"
#include "uart-defaults.h"

/* fallback if not set by build system */
#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-utils (unknown version)"
#endif

/* command line options */
static const struct option long_options[] = {
    { "uart",               required_argument,      0,      'd' },
    { "sync",               no_argument,            0,      'S' },
    { "no-dump",            no_argument,            0,      'D' },
    { "no-charge-control",  no_argument,            0,      'C' },
    { "gpiochip",           required_argument,      0,      'c' },
    { "reset-gpio"   ,      required_argument,      0,      'r' },
    { "md-gpio",            required_argument,      0,      'm' },
    { "reset-period",       required_argument,      0,      'p' },
    { "no-reset",           no_argument,            0,      'R' },

    { "verbose",            no_argument,            0,      'v' },
    { "version",            no_argument,            0,      'V' },
    { "help",               no_argument,            0,      'h' },
    {} /* stop condition for iterator */
};

static const char *short_options = "d:SDCc:r:m:p:RvVh";

/* descriptions for the command line options */
static const char *long_options_descs[] = {
    "UART interface (default: " DEFAULT_UART_INTERFACE ")",
    "initial receive sync (default: send packet first)",
    "don't dump data (useful only in verbose mode to print only received frames)",
    "don't send Charge Control frames automatically",
    "GPIO chip device (default: " DEFAULT_RA_GPIOCHIP ")",
    "GPIO name for controlling RESET pin of MCU (default: " DEFAULT_RA_GPIO_RESET_PIN ")",
    "GPIO name for controlling MD pin of MCU (default: " DEFAULT_RA_GPIO_MD_PIN ")",
    "reset duration (in ms, default: " __stringify(DEFAULT_RA_RESET_DELAY) ")",
    "don't reset the safety controller before starting UART communication",

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

/* to simplify, these are globals */
static bool verbose = false;
static bool intial_sync = false;
static bool no_dump = false;
static bool send_charge_control = true;
static bool no_reset = false;
static char *gpiochip = DEFAULT_RA_GPIOCHIP;
static char *reset_gpioname = DEFAULT_RA_GPIO_RESET_PIN;
static char *md_gpioname = DEFAULT_RA_GPIO_MD_PIN;
static unsigned int reset_duration = DEFAULT_RA_RESET_DELAY;
static char *uart_device = DEFAULT_UART_INTERFACE;

static void debug_cb(const char *format, va_list args)
{
    if (verbose) {
        printf("debug: ");
        vprintf(format, args);
        printf("\r\n");
    }
}

static void error_cb(const char *format, va_list args)
{
    fprintf(stderr, "Error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\r\n");
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

void make_stdin_unbuffered(struct termios *orig)
{
    struct termios termios_new;

    tcgetattr(STDIN_FILENO, orig);
    memcpy(&termios_new, orig, sizeof(termios_new));
    cfmakeraw(&termios_new);

    tcsetattr(STDIN_FILENO, TCSANOW, &termios_new);
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
        case 'S':
            intial_sync = true;
            break;
        case 'D':
            no_dump = true;
            break;
        case 'C':
            send_charge_control = false;
            break;
        case 'c':
            gpiochip = optarg;
            break;
        case 'r':
            reset_gpioname = optarg;
            break;
        case 'm':
            md_gpioname = optarg;
            break;
        case 'p':
            reset_duration = atoi(optarg);
            break;
        case 'R':
            no_reset = true;
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
    struct termios termios_orig;
    struct pollfd poll_fds[2]; /* stdin at [0], UART fd at [1] */
    int fds = 2;
    char *env_uart_device = NULL;
    struct uart_ctx uart = { .fd = -1 };
    struct safety_controller ctx = {};
    enum cb_uart_com com;
    uint64_t data;
    bool fw_version_requested = false;
    bool fw_version_received = false;
    bool git_hash_requested = false;
    bool git_hash_received = false;
    int rc = EXIT_FAILURE;
    int rv;

    /* check whether environment variable SAFETY_MCU_UART is set and use it
     * as default; so the resulting order is:
     * compiled-in default -> can be overridden by environment -> can be overridden by cmdline
     */
    env_uart_device = getenv(GETENV_UART_KEY);
    if (env_uart_device)
        uart_device = env_uart_device;

    /* handle command line options */
    parse_cli(argc, argv);

    /* register debug and error message callbacks */
    ra_utils_set_error_msg_cb(error_cb);
    ra_utils_set_debug_msg_cb(debug_cb);

    /* the baudrate of the MCU with running firmware should be 115200 */
    rv = uart_open(&uart, uart_device, 115200);
    if (rv) {
        error("opening '%s' failed: %m", uart_device);
        return -1;
    }

    /* ensure virgin start state and add stdin */
    memset(poll_fds, 0, sizeof(poll_fds));
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = uart.fd;
    poll_fds[1].events = POLLIN;

    /* maybe this need yet another option flag */
    uart_trace(&uart, verbose);

    /* unless not desired, reset the safety controller via GPIO */
    if (!no_reset) {
        struct gpio_ctx *gpio;

        gpio = ra_gpio_init(gpiochip, reset_gpioname, md_gpioname);
        if (!gpio) {
            error("could not acquire GPIOs: %m");
            goto close_out;
        }

        ra_set_reset_duration(gpio, reset_duration);

        rv = ra_reset_to_normal(gpio);

        /* release the GPIOs immediately so that programs in parallel can acquire them */
        ra_gpio_close(gpio);

        if (rv) {
            error("resetting safety controller failed: %m");
            goto close_out;
        }

        /* when successfully reseted, sleep until controller is ready again */
        msleep(CB_PROTO_STARTUP_DELAY);
    }

    if (intial_sync) {
        /* sync the receiving side */
        rv = cb_uart_recv_and_sync(&uart, &com, &data);
        if (rv) {
            error("could not synchronize to the safety controller: %m");
            return -1;
        }
    }

    /* make stdin unbuffered: otherwise poll will only react on <Enter> */
    make_stdin_unbuffered(&termios_orig);

    while (1) {
        int rv;

        if (!fw_version_requested) {
            rv = cb_send_uart_inquiry(&uart, COM_FW_VERSION);
            if (rv) {
                error("error while sending inquiry frame for '%s': %m", cb_uart_com_to_str(COM_FW_VERSION));
                goto close_out;
            }
            fw_version_requested = true;
        } else if (!git_hash_requested && fw_version_received) {
            rv = cb_send_uart_inquiry(&uart, COM_GIT_HASH);
            if (rv) {
                error("error while sending inquiry frame for '%s': %m", cb_uart_com_to_str(COM_GIT_HASH));
                goto close_out;
            }
            git_hash_requested = true;

            // this is a little bit tricky/small hack: in case we want to send out
            // charge control frames automatically, we just jump over the else condition
            // otherwise we had to duplicate code here
            if (send_charge_control)
                goto send_charge_control_frame;

        } else if (com == COM_CHARGE_STATE || com == COM_CHARGE_STATE_2) {
            if (send_charge_control) {
send_charge_control_frame:
                /* remember the timestamp */
                cb_proto_set_ts_str(&ctx, cb_proto_is_mcs_mode(&ctx) ? COM_CHARGE_CONTROL_2 : COM_CHARGE_CONTROL);
                /* send out charge control frame 1 when last received frame was a charge state 1 one */
                rv = cb_uart_send(&uart, cb_proto_is_mcs_mode(&ctx) ? COM_CHARGE_CONTROL_2 : COM_CHARGE_CONTROL, ctx.charge_control);
                if (rv) {
                    error("error while sending charge control frame: %m");
                    goto close_out;
                }
            }
        }

        rv = poll(poll_fds, fds, -1);
        if (rv == -1) {
            if (errno == EINTR)
                goto close_out;

            error("poll() failed: %m");
            continue;
        }
        if (rv == 0)
            continue;

        /* check stdin */
        if ((poll_fds[0].revents & POLLIN) != 0) {
            ssize_t len;
            char cmd;

            len = read(poll_fds[0].fd, &cmd, sizeof(cmd));
            if (len < 0) {
                error("Could not read command from STDIN: %m");
                goto close_out;
            }
            if (len == sizeof(cmd)) {
                if (!cb_proto_is_mcs_mode(&ctx)) {
                    switch (cmd) {
                    case 'e':
                        cb_proto_set_pwm_active(&ctx, 1);
                        break;
                    case 'E':
                        cb_proto_set_pwm_active(&ctx, 0);
                        break;
                    case 'r':
                        cb_proto_set_duty_cycle(&ctx, 50);
                        cb_proto_set_pwm_active(&ctx, 1);
                        break;
                    case 't':
                        cb_proto_set_duty_cycle(&ctx, 100);
                        cb_proto_set_pwm_active(&ctx, 1);
                        break;
                    case 'z':
                        cb_proto_set_duty_cycle(&ctx, 1000);
                        cb_proto_set_pwm_active(&ctx, 1);
                        break;
                    case '1':
                        cb_proto_contactorN_set_state(&ctx, 0, !cb_proto_contactorN_get_target_state(&ctx, 0));
                        break;
                    case '2':
                        cb_proto_contactorN_set_state(&ctx, 1, !cb_proto_contactorN_get_target_state(&ctx, 1));
                        break;
                    case '0':
                        cb_proto_set_duty_cycle(&ctx, 0);
                        break;
                    case '5':
                        cb_proto_set_duty_cycle(&ctx, 50);
                        break;
                    case '6':
                        cb_proto_set_duty_cycle(&ctx, 100);
                        break;
                    case '9':
                        cb_proto_set_duty_cycle(&ctx, 1000);
                        break;
                    case '-': {
                        unsigned int duty_cycle = cb_proto_get_target_duty_cycle(&ctx) - 10;
                        /* check for underflow */
                        if (duty_cycle > 1000)
                            duty_cycle = 0;
                        cb_proto_set_duty_cycle(&ctx, duty_cycle);
                        break;
                    }
                    case '+':
                        /* overflow is already checked in library */
                        cb_proto_set_duty_cycle(&ctx, cb_proto_get_target_duty_cycle(&ctx) + 10);
                        break;
                    case 's':
                        send_charge_control = !send_charge_control;
                        break;
                    case 'c':
                        cb_proto_set_ts_str(&ctx, COM_CHARGE_CONTROL);
                        rv = cb_uart_send(&uart, COM_CHARGE_CONTROL, ctx.charge_control);
                        if (rv) {
                            error("error while sending charge control frame: %m");
                            goto close_out;
                        }
                        break;
                    case 'q':
                    case 0x03: /* Ctrl-C */
                        goto close_out;
                    case '\r':
                    case '\n':
                        printf("\r\n");
                        break;
                    default:
                        if (isprint(cmd))
                            error("Unknown command '%c', use 'h' or '?' to show available commands.", cmd);
                        else
                            error("Unknown command '0x%02x', use 'h' or '?' to show available commands.", cmd);
                    }
                } else {
                    switch (cmd) {
                    case 'r':
                        cb_proto_set_ccs_ready(&ctx, true);
                        break;
                    case 'R':
                        cb_proto_set_ccs_ready(&ctx, false);
                        break;
                    case 'e':
                        cb_proto_set_estop(&ctx, true);
                        break;
                     case 's':
                        send_charge_control = !send_charge_control;
                        break;
                    case 'c':
                        cb_proto_set_ts_str(&ctx, COM_CHARGE_CONTROL_2);
                        rv = cb_uart_send(&uart, COM_CHARGE_CONTROL_2, ctx.charge_control);
                        if (rv) {
                            error("error while sending charge control frame: %m");
                            goto close_out;
                        }
                        break;
                    case 'q':
                    case 0x03: /* Ctrl-C */
                        goto close_out;
                    case '\r':
                    case '\n':
                        printf("\r\n");
                        break;
                    default:
                        if (isprint(cmd))
                            error("Unknown command '%c', use 'h' or '?' to show available commands.", cmd);
                        else
                            error("Unknown command '0x%02x', use 'h' or '?' to show available commands.", cmd);
                    }
                }
            }
        }

        /* check UART input */
        if ((poll_fds[1].revents & POLLIN) != 0) {
            rv = cb_uart_recv(&uart, &com, &data);
            if (rv) {
                uint8_t buf[64];
                ssize_t c;

                error("error while receiving frame from the safety controller: %m");

                c = read(uart.fd, &buf, sizeof(buf));
                if (c < 0) {
                    error("error while receiving unprocessed data: %m");
                    goto close_out;
                }

                error("unprocessed data in input buffer follows (%zu bytes):", c);

                uart_dump_frame(false, false, buf, c);
                goto close_out;
            }

            cb_proto_set_ts_str(&ctx, com);

            switch (com) {
            case COM_CHARGE_STATE_2:
                // in case we connect to an already running firmware we could receive
                // a Charge State 2 frame before we derived the platform from Firmware Version frame
                // so set this already here too
                cb_proto_set_mcs_mode(&ctx, true);
                __attribute__ ((fallthrough));
            case COM_CHARGE_STATE:
                ctx.charge_state = data;
                break;
            case COM_PT1000_STATE:
                ctx.pt1000 = data;
                break;
            case COM_FW_VERSION:
                ctx.fw_version = data;
                cb_proto_set_fw_version_str(&ctx);
                fw_version_received = true;
                if (cb_proto_fw_get_platform_type(&ctx) == FW_PLATFORM_TYPE_CCY)
                    cb_proto_set_mcs_mode(&ctx, true);
                break;
            case COM_GIT_HASH:
                ctx.git_hash = data;
                cb_proto_set_git_hash_str(&ctx);
                git_hash_received = true;
                break;
            default:
                /* not yet implemented */
            }
        }

        /* clear screen (in verbose mode, this does not make sense) */
        if (!verbose)
            printf("\033[H\033[J");

        if (!no_dump) {
            /* dump it */
            cb_proto_dump(&ctx);

            printf("\r\n");
            if (!cb_proto_is_mcs_mode(&ctx)) {
                printf("== Available commands ==\r\n"
                       "  e -- enable PWM                   E -- disable PWM\r\n"
                       "  r -- enable PWM with 5%%           t -- enable PWM with 10%%          z -- enable PWM with 100%%\r\n"
                       "  0 -- set PWM duty cycle to 0%%     5 -- set PWM duty cycle to 5%%     9 -- set PWM duty cycle to 100%%\r\n"
                       "  - -- decrease PWM value by 1%%     + -- increase PMW value by 1%%     6 -- set PWM duty cycle to 10%%\r\n"
                       "  1 -- toggle contactor 1           2 -- toggle contactor 2\r\n"
                       "  c -- (manually) send a Charge Control frame\r\n"
                       "  s -- toggle auto sending of Charge Control frames (auto-sending: %s)\r\n"
                       "  q -- quit the program\r\n", send_charge_control ? "on" : "off");
            } else {
                printf("== Available commands ==\r\n"
                       "  r -- set CCS Ready to Ready       R -- set CCS Ready to Not Ready\r\n"
                       "  e -- set CCS Ready to Emergency Stop\r\n"
                       "  c -- (manually) send a Charge Control frame\r\n"
                       "  s -- toggle auto sending of Charge Control frames (auto-sending: %s)\r\n"
                       "  q -- quit the program\r\n", send_charge_control ? "on" : "off");
            }
        }
    }

    rc = EXIT_SUCCESS;

close_out:
    if (uart.fd != -1) {
        rv = uart_close(&uart);
        if (rv)
            error("closing UART failed: %m");
    }

    /* restore terminal settings */
    tcsetattr(STDIN_FILENO, TCSANOW, &termios_orig);

    return rc;
}
