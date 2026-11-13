/*
 * Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * Charge SOM interactive EOL monitor, initial CP PWM 5%.
 *
 * Usage: ra-eol [options]
 *
 *  Options:
 *          -d, --uart DEVICE        UART (default: /dev/ttyLP2)
 *          -r, --reset-gpio NAME    RESET GPIO (default: nSAFETY_RESET_INT)
 *          -m, --md-gpio NAME       MD GPIO (default: SAFETY_BOOTMODE_SET)
 *          -p, --reset-period MS    Reset duration (default: 500 ms)
 *          -R, --no-reset           Skip initial controller reset
 *          -M, --can-mirror IFACE   Mirror RX/TX to CAN
 *          -D, --no-dump            Suppress state overview
 *          -v, --verbose            Enable UART tracing
 *          -V, --version            Print version
 *          -h, --help               Print help
 *
 * SAFETY_MCU_UART, SAFETY_MCU_RESET_GPIO and SAFETY_MCU_MD_GPIO override defaults.
 * CP_INVERT and PP_SAE_IEC follow normal firmware. Exit leaves the last outputs unchanged.
 * PT1000 measurement alternates automatically between channels 1+3 and 2+4.
 * Keys: r/t/z enable at 5/10/100%; 0/5/6/9 select 0/5/10/100%; +/- change by 1%.
 *       e/E enable/disable; s toggle auto-send; c send once; Ctrl-R reset; q/Ctrl-C quit.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <cb_protocol.h>
#include <cb_uart.h>
#include <logging.h>
#include <tools.h>
#include <uart.h>
#include <version.h>
#include "ra_gpio.h"
#include "gpio-defaults.h"
#include "uart-defaults.h"
#include "eol_state.h"

#define EOL_INTERVAL_MS 100

static volatile sig_atomic_t interrupted;
static bool verbose, no_dump, no_reset;
static const char *uart_device, *reset_gpioname, *md_gpioname, *can_mirror_device;
static unsigned int reset_duration = DEFAULT_RA_RESET_DELAY;

static void handle_signal(int signum)
{
    (void)signum;
    interrupted = 1;
}

static void error_cb(const char *format, va_list args)
{
    fprintf(stderr, "Error: ");
    vfprintf(stderr, format, args);
    fputs("\r\n", stderr);
}

static void debug_cb(const char *format, va_list args)
{
    if (!verbose)
        return;
    vprintf(format, args);
    fputs("\r\n", stdout);
}

static const char *env_default(const char *key, const char *fallback)
{
    const char *value = getenv(key);
    return value ? value : fallback;
}

static void usage(const char *program)
{
    fprintf(stderr,
        "%s (%s) -- Charge SOM interactive EOL monitor, initial CP PWM 5%%\n"
        "Usage: %s [options]\n"
        "  -d, --uart DEVICE        UART (default: " DEFAULT_UART_INTERFACE ")\n"
        "  -r, --reset-gpio NAME    RESET GPIO (default: " DEFAULT_RA_GPIO_RESET_PIN ")\n"
        "  -m, --md-gpio NAME       MD GPIO (default: " DEFAULT_RA_GPIO_MD_PIN ")\n"
        "  -p, --reset-period MS    Reset duration (default: 500 ms)\n"
        "  -R, --no-reset           Skip initial controller reset\n"
        "  -M, --can-mirror IFACE   Mirror RX/TX to CAN\n"
        "  -D, --no-dump            Suppress state overview\n"
        "  -v, --verbose            Enable UART tracing\n"
        "  -V, --version            Print version\n"
        "  -h, --help               Print help\n"
        "SAFETY_MCU_UART, SAFETY_MCU_RESET_GPIO and SAFETY_MCU_MD_GPIO override defaults.\n"
        "CP_INVERT and PP_SAE_IEC follow normal firmware. Exit leaves the last outputs unchanged.\n"
        "PT1000 measurement alternates automatically between channels 1+3 and 2+4.\n"
        "Keys: r/t/z enable at 5/10/100%%; 0/5/6/9 select 0/5/10/100%%; +/- change by 1%%.\n"
        "      e/E enable/disable; s toggle auto-send; c send once; Ctrl-R reset; q/Ctrl-C quit.\n",
        program, PACKAGE_STRING, program);
}

static int reset_controller(const char *reset, const char *md, unsigned int duration)
{
    struct gpio_ctx *gpio = ra_gpio_init(reset, md);
    int rv;

    if (!gpio)
        return -1;
    ra_set_reset_duration(gpio, duration);
    rv = ra_reset_to_normal(gpio);
    ra_gpio_close(gpio);
    if (!rv)
        msleep(CB_PROTO_STARTUP_DELAY);
    return rv;
}

static void parse_cli(int argc, char **argv)
{
    static const struct option options[] = {
        { "uart", required_argument, NULL, 'd' },
        { "reset-gpio", required_argument, NULL, 'r' },
        { "md-gpio", required_argument, NULL, 'm' },
        { "reset-period", required_argument, NULL, 'p' },
        { "no-reset", no_argument, NULL, 'R' },
        { "can-mirror", required_argument, NULL, 'M' },
        { "no-dump", no_argument, NULL, 'D' },
        { "verbose", no_argument, NULL, 'v' },
        { "version", no_argument, NULL, 'V' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    int c;

    uart_device = env_default(GETENV_UART_KEY, DEFAULT_UART_INTERFACE);
    reset_gpioname = env_default(GETENV_RESET_PIN_KEY, DEFAULT_RA_GPIO_RESET_PIN);
    md_gpioname = env_default(GETENV_MD_PIN_KEY, DEFAULT_RA_GPIO_MD_PIN);
    while ((c = getopt_long(argc, argv, "d:r:m:p:RM:DvVh", options, NULL)) != -1) {
        switch (c) {
        case 'd': uart_device = optarg; break;
        case 'r': reset_gpioname = optarg; break;
        case 'm': md_gpioname = optarg; break;
        case 'p': {
            char *end;
            unsigned long value;
            errno = 0;
            value = strtoul(optarg, &end, 10);
            if (errno || *end || end == optarg || *optarg == '-' || value > UINT_MAX) {
                fprintf(stderr, "Invalid reset duration: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            reset_duration = value;
            break;
        }
        case 'R': no_reset = true; break;
        case 'M': can_mirror_device = optarg; break;
        case 'D': no_dump = true; break;
        case 'v': verbose = true; break;
        case 'V': printf("%s (%s)\n", argv[0], PACKAGE_STRING); exit(EXIT_SUCCESS);
        case 'h': usage(argv[0]); exit(EXIT_SUCCESS);
        default: usage(argv[0]); exit(EXIT_FAILURE);
        }
    }
    if (optind != argc) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
}

enum command { COMMAND_NONE, COMMAND_QUIT, COMMAND_RESET, COMMAND_SEND };

static enum command handle_command(struct eol_state *state, char cmd)
{
    switch (cmd) {
    case 'r': state->pwm_active = true; /* fall through */
    case '5': state->duty_cycle = 5; break;
    case 't': state->pwm_active = true; /* fall through */
    case '6': state->duty_cycle = 10; break;
    case 'z': state->pwm_active = true; /* fall through */
    case '9': state->duty_cycle = 100; break;
    case '0': state->duty_cycle = 0; break;
    case '+': if (state->duty_cycle < 100) ++state->duty_cycle; break;
    case '-': if (state->duty_cycle > 0) --state->duty_cycle; break;
    case 'e': state->pwm_active = true; break;
    case 'E': state->pwm_active = false; break;
    case 's': eol_set_auto_send(state, !state->auto_send); break;
    case 'c': return COMMAND_SEND;
    case 'q': case 0x03: return COMMAND_QUIT;
    case 0x12: return COMMAND_RESET;
    case 'h': case '?': case '\r': case '\n': break; /* redraw command list */
    default: error("Unknown command 0x%02x; use h or ? for the command list", (unsigned char)cmd);
    }
    return COMMAND_NONE;
}

static int send_output(struct uart_ctx *uart, struct eol_state *state)
{
    uint64_t data = eol_output(state);
    if (cb_uart_send(uart, COM_DIGITAL_OUTPUT, data))
        return -1;
    eol_record_sent(state, data);
    return 0;
}

int main(int argc, char **argv)
{
    /* Initialization order matches ra-raw. Only firmware identity is mandatory. */
    static const enum cb_uart_com inquiries[] = {
        COM_FW_VERSION, COM_PARTNUMBER_1, COM_PARTNUMBER_2, COM_CHIPINFO, COM_GIT_HASH
    };
    enum { STATE_INIT_FW_VERSION, STATE_INIT_PNM1, STATE_INIT_PNM2,
           STATE_INIT_CHIPINFO, STATE_INIT_GIT_HASH, STATE_RUN_LOOP } state = STATE_INIT_FW_VERSION;
    struct uart_ctx uart = INIT_UART_CTX;
    struct cb_uart_rx_state rx = { 0 };
    struct eol_state controller;
    struct termios original, raw;
    struct pollfd fds[2] = { { .fd = STDIN_FILENO, .events = POLLIN },
                             { .fd = -1, .events = POLLIN } };
    struct sigaction sa = { .sa_handler = handle_signal };
    struct timespec now, next_output, next_display, response_deadline;
    bool terminal_saved = false, dirty = true, inquiry_pending = false;
    int rc = EXIT_FAILURE, rv;

    parse_cli(argc, argv);
    ra_utils_set_error_msg_cb(error_cb);
    ra_utils_set_debug_msg_cb(debug_cb);
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
        error("Installing signal handlers failed: %m");
        return EXIT_FAILURE;
    }
    eol_state_init(&controller);
    if (uart_open(&uart, uart_device, 115200))
        return EXIT_FAILURE;
    fds[1].fd = uart.fd;
    uart_trace(&uart, verbose);
    if (can_mirror_device && uart_can_mirror_enable(&uart, can_mirror_device)) {
        error("Opening CAN interface '%s' failed: %m", can_mirror_device);
        goto close_out;
    }
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &original)) {
            error("Reading terminal settings failed: %m");
            goto close_out;
        }
        raw = original;
        cfmakeraw(&raw);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw)) {
            error("Setting terminal mode failed: %m");
            goto close_out;
        }
        terminal_saved = true;
    }
    if (!no_reset) {
restart_reset:
        if (reset_controller(reset_gpioname, md_gpioname, reset_duration)) {
            error("Resetting controller failed: %m");
            goto close_out;
        }
    }
    /* Ctrl-R retains desired PWM settings but clears all old observations. */
    {
        unsigned int duty = controller.duty_cycle;
        bool active = controller.pwm_active, automatic = controller.auto_send;
        eol_state_init(&controller);
        controller.duty_cycle = duty;
        controller.pwm_active = active;
        controller.auto_send = automatic;
    }
    memset(&rx, 0, sizeof(rx));
    if (uart_flush_input(&uart)) {
        error("Flushing UART failed: %m");
        goto close_out;
    }
    state = STATE_INIT_FW_VERSION;
    inquiry_pending = false;
    dirty = true;
    clock_gettime(CLOCK_MONOTONIC, &now);
    next_output = next_display = now;

    while (!interrupted) {
        long long wait_ms = EOL_INTERVAL_MS;
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (state != STATE_INIT_FW_VERSION && eol_measure_tick(&controller, eol_monotonic_ms()))
            dirty = true;

        if (inquiry_pending && timespec_compare(&now, &response_deadline) >= 0) {
            if (state == STATE_INIT_FW_VERSION) {
                error("No firmware version received within 1500 ms");
                goto close_out;
            }
            fprintf(stderr, "Warning: No response to %s\r\n", cb_uart_com_to_str(inquiries[state]));
            ++state;
            inquiry_pending = false;
            dirty = true;
        }
        if (state != STATE_INIT_FW_VERSION && controller.auto_send &&
            timespec_compare(&now, &next_output) >= 0) {
            if (send_output(&uart, &controller))
                goto send_error;
            /* No burst after missed periods. */
            do { timespec_add_ms(&next_output, EOL_INTERVAL_MS); }
            while (timespec_compare(&next_output, &now) <= 0);
            dirty = true;
        }
        if (state != STATE_RUN_LOOP && !inquiry_pending) {
            if (cb_send_uart_inquiry(&uart, inquiries[state]))
                goto send_error;
            response_deadline = now;
            timespec_add_ms(&response_deadline, state == STATE_INIT_FW_VERSION ? 1500 : 100);
            inquiry_pending = true;
        }
        if (dirty && !no_dump && timespec_compare(&now, &next_display) >= 0) {
            if (!verbose && isatty(STDOUT_FILENO))
                printf("\033[H\033[J");
            eol_dump(stdout, &controller);
            fflush(stdout);
            next_display = now;
            timespec_add_ms(&next_display, EOL_INTERVAL_MS);
            dirty = false;
        }
        if (state != STATE_INIT_FW_VERSION && controller.auto_send)
            wait_ms = timespec_to_ms(timespec_sub(next_output, now));
        if (inquiry_pending) {
            long long remaining = timespec_to_ms(timespec_sub(response_deadline, now));
            if (remaining < wait_ms) wait_ms = remaining;
        }
        if (dirty && !no_dump) {
            long long remaining = timespec_to_ms(timespec_sub(next_display, now));
            if (remaining < wait_ms) wait_ms = remaining;
        }
        rv = poll(fds, 2, wait_ms > 0 ? (int)wait_ms : 0);
        if (rv < 0) {
            if (errno == EINTR) continue;
            error("poll() failed: %m");
            goto close_out;
        }
        if (interrupted) break;
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char cmd;
            ssize_t count = read(fds[0].fd, &cmd, 1);
            if (count < 0 && errno != EINTR) {
                error("Reading stdin failed: %m");
                goto close_out;
            }
            if (!count) fds[0].fd = -1;
            if (count == 1) {
                enum command action = handle_command(&controller, cmd);
                dirty = true;
                if (action == COMMAND_QUIT) break;
                if (action == COMMAND_RESET) goto restart_reset;
                if (action == COMMAND_SEND && state != STATE_INIT_FW_VERSION) {
                    if (send_output(&uart, &controller)) goto send_error;
                }
            }
        }
        if (fds[0].revents & (POLLERR | POLLNVAL)) fds[0].fd = -1;
        if (fds[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
            unsigned int batch;
            for (batch = 0; batch < 32; ++batch) {
                enum cb_uart_com com;
                uint64_t data;
                rv = cb_uart_recv_available(&uart, &rx, &com, &data);
                if (rv < 0) {
                    if (errno == EINTR) break;
                    error("UART receive failed: %m");
                    goto close_out;
                }
                if (!rv) break;
                eol_record(&controller, com, data);
                dirty = true;
                if (com == COM_FW_VERSION &&
                    (cb_proto_fw_get_platform_type(&controller.info) != FW_PLATFORM_TYPE_CHARGESOM ||
                     cb_proto_fw_get_application_type(&controller.info) != FW_APPLICATION_TYPE_EOL)) {
                    error("Controller is not running Charge SOM EOL firmware");
                    goto close_out;
                }
                if (inquiry_pending && com == inquiries[state]) {
                    ++state;
                    inquiry_pending = false;
                }
            }
        }
    }
    rc = EXIT_SUCCESS;
    goto close_out;

send_error:
    error("Sending UART message failed: %m");
close_out:
    if (terminal_saved && tcsetattr(STDIN_FILENO, TCSANOW, &original)) {
        error("Restoring terminal settings failed: %m");
        rc = EXIT_FAILURE;
    }
    uart_can_mirror_disable(&uart);
    if (uart_close(&uart)) {
        error("Closing UART failed: %m");
        rc = EXIT_FAILURE;
    }
    return rc;
}
