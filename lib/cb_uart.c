/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "uart.h"
#include "tools.h"
#include "logging.h"
#include "crc8_j1850.h"
#include "cb_uart.h"
#include "cb_can_mirror.h"

/* frame start/end markers */
#define CB_SOF 0xA5
#define CB_EOF 0x03

/* UART frame */
struct uart_frame {
    uint8_t sof;
    uint8_t com;
    uint64_t data;
    uint8_t crc;
    uint8_t eof;
} __attribute__((packed));

static int remaining_timeout_ms(const struct timespec *timeout)
{
    struct timespec now;
    long long remaining;

    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return -1;

    remaining = timespec_to_ms(timespec_sub(*timeout, now));
    if (remaining <= 0) {
        errno = ETIMEDOUT;
        return -1;
    }

    return remaining;
}

/* Shared stream parser for blocking and incremental receive. */
static bool extract_frame(struct cb_uart_rx_state *state, struct uart_frame *frame)
{
    size_t sof = 0;

    while (sof < state->buffered && state->bytes[sof] != CB_SOF)
        ++sof;
    state->discarded += sof;
    state->buffered -= sof;
    memmove(state->bytes, state->bytes + sof, state->buffered);
    if (state->buffered < sizeof(*frame))
        return false;

    memcpy(frame, state->bytes, sizeof(*frame));
    if (frame->eof == CB_EOF &&
        frame->crc == crc8_j1850(&frame->com, sizeof(frame->com) + sizeof(frame->data))) {
        state->buffered = 0;
        if (state->discarded)
            debug("UART receive stream resynchronized after discarding %zu byte(s)", state->discarded);
        state->discarded = 0;
        return true;
    }
    /* Keep bytes after a false SOF, including any following real SOF. */
    ++state->discarded;
    --state->buffered;
    memmove(state->bytes, state->bytes + 1, state->buffered);
    return false;
}

static int cb_uart_read_frame(struct uart_ctx *uart, struct uart_frame *frame, int timeout_ms)
{
    struct cb_uart_rx_state state = { 0 };
    struct timespec timeout;

    if (clock_gettime(CLOCK_MONOTONIC, &timeout))
        return -1;
    timespec_add_ms(&timeout, timeout_ms);
    for (;;) {
        int remaining;
        ssize_t count;

        if (extract_frame(&state, frame))
            return 0;
        remaining = remaining_timeout_ms(&timeout);
        if (remaining < 0)
            return -1;
        count = uart_read_with_timeout(uart, state.bytes + state.buffered,
                                       sizeof(state.bytes) - state.buffered, remaining);
        if (count < 0)
            return -1;
        state.buffered += count;
    }
}

const char *cb_uart_com_to_str(enum cb_uart_com com)
{
    switch (com) {
    case COM_INQUIRY:
        return "COM_INQUIRY";
    case COM_ACTION:
        return "COM_ACTION";
    case COM_DIGITAL_OUTPUT:
        return "COM_DIGITAL_OUTPUT";
    case COM_DIGITAL_INPUT:
        return "COM_DIGITAL_INPUT";
    case COM_ANALOG_INPUT_01:
        return "COM_ANALOG_INPUT_01";
    case COM_ANALOG_INPUT_02:
        return "COM_ANALOG_INPUT_02";
    case COM_ANALOG_INPUT_03:
        return "COM_ANALOG_INPUT_03";
    case COM_ANALOG_INPUT_04:
        return "COM_ANALOG_INPUT_04";
    case COM_CHARGE_CONTROL:
        return "COM_CHARGE_CONTROL";
    case COM_CHARGE_STATE:
        return "COM_CHARGE_STATE";
    case COM_PT1000_STATE:
        return "COM_PT1000_STATE";
    case COM_DIAGNOSTIC_MEASUREMENTS:
        return "COM_DIAGNOSTIC_MEASUREMENTS";
    case COM_FW_VERSION:
        return "COM_FW_VERSION";
    case COM_GIT_HASH:
        return "COM_GIT_HASH";
    case COM_ERROR_MESSAGE:
        return "COM_ERROR_MESSAGE";
    case COM_CHARGE_STATE_2:
        return "COM_CHARGE_STATE_2";
    case COM_CHARGE_CONTROL_2:
        return "COM_CHARGE_CONTROL_2";
    case COM_DIAGNOSTIC_MEASUREMENTS_2:
        return "COM_DIAGNOSTIC_MEASUREMENTS_2";
    case COM_ANALOG_INPUT_05:
        return "COM_ANALOG_INPUT_05";
    case COM_PARTNUMBER_1:
        return "COM_PARTNUMBER_1";
    case COM_PARTNUMBER_2:
        return "COM_PARTNUMBER_2";
    case COM_CHIPINFO:
        return "COM_CHIPINFO";
    default:
        return "UNKNOWN";
    }
}

int cb_uart_send(struct uart_ctx *uart, enum cb_uart_com com, uint64_t data)
{
    struct uart_frame frame;
    ssize_t c;

    /* prepare packet */
    memset(&frame, 0, sizeof(frame));
    frame.sof = CB_SOF;
    frame.com = com;
    frame.data = htobe64(data);
    frame.crc = crc8_j1850(&frame.com, sizeof(frame.com) + sizeof(frame.data));
    frame.eof = CB_EOF;

    if (com == COM_INQUIRY) {
        uint8_t *c = &frame.com + 1;
        debug("sending frame: %s (%s)", cb_uart_com_to_str(com), cb_uart_com_to_str(*c));
    } else {
        debug("sending frame: %s", cb_uart_com_to_str(com));
    }

    if (uart->trace)
        uart_dump_frame(true, true, (uint8_t *)&frame, sizeof(frame));

    c = uart_write_drain(uart, (const uint8_t *)&frame, sizeof(frame));
    if (c < 0)
        return c;

    if (uart_can_mirror_enabled(uart))
        cb_can_mirror_write(uart->fd_can_mirror, com, htobe64(data));

    return 0;
}

static void deliver_frame(struct uart_ctx *uart, struct uart_frame *frame,
                          enum cb_uart_com *com, uint64_t *data)
{
    if (uart->trace)
        uart_dump_frame(true, false, (uint8_t *)frame, sizeof(*frame));
    debug("received frame: %s", cb_uart_com_to_str(frame->com));
    if (uart_can_mirror_enabled(uart))
        cb_can_mirror_write(uart->fd_can_mirror, frame->com, frame->data);
    if (com)
        *com = frame->com;
    if (data)
        *data = be64toh(frame->data);
}

int cb_uart_recv(struct uart_ctx *uart, enum cb_uart_com *com, uint64_t *data)
{
    struct uart_frame frame;

    if (cb_uart_read_frame(uart, &frame, CB_UART_RECV_INTERVAL + CB_UART_RECV_INTERVAL / 2))
        return -1;
    deliver_frame(uart, &frame, com, data);
    return 0;
}

int cb_uart_recv_available(struct uart_ctx *uart, struct cb_uart_rx_state *state,
                           enum cb_uart_com *com, uint64_t *data)
{
    struct uart_frame frame;
    unsigned int reads;

    for (reads = 0; reads < 32; ++reads) {
        struct pollfd pfd = { .fd = uart->fd, .events = POLLIN };
        ssize_t count;
        int rv;

        if (extract_frame(state, &frame)) {
            deliver_frame(uart, &frame, com, data);
            return 1;
        }
        rv = poll(&pfd, 1, 0);
        if (rv < 0)
            return -1;
        if (!rv)
            return 0;
        if (!(pfd.revents & POLLIN)) {
            errno = EIO;
            return -1;
        }
        count = read(uart->fd, state->bytes + state->buffered,
                     sizeof(state->bytes) - state->buffered);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
        if (!count) {
            errno = EPIPE;
            return -1;
        }
        state->buffered += count;
    }
    if (extract_frame(state, &frame)) {
        deliver_frame(uart, &frame, com, data);
        return 1;
    }
    return 0;
}

int cb_uart_recv_and_sync(struct uart_ctx *uart, enum cb_uart_com *com, uint64_t *data)
{
    unsigned int trial = CB_UART_MAX_SYNC_TRIALS;
    int rv;

retry:
    trial--;

    rv = cb_uart_recv(uart, com, data);
    if (rv < 0) {
        /* in case we are async to the safety controller, we might have looked at
         * the wrong positions; then discard all data in receive buffers, wait for next
         * frame and try again, but at max CB_UART_MAX_SYNC_TRIALS times.
         */
        if (errno == EBADMSG) {
            rv = uart_flush_input(uart);
            if (rv < 0)
                return rv;

            if (trial)
                goto retry;
        }

        return rv;
    }

    return 0;
}
