/*
 * Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * Command line tool to dump UART frames mirrored onto a CAN interface.
 *
 * Usage: ra-candump [<options>] <can-interface>
 *
 *  Options:
 *          -C, --compact          only print changed payloads per CAN ID
 *          -V, --version          print version and exit
 *          -h, --help             print this usage and exit
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <endian.h>
#include <errno.h>
#include <getopt.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <cb_protocol.h>
#include <cb_uart.h>
#include <version.h>

/* fallback if not set by build system */
#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-utils (unknown version)"
#endif

static const struct option long_options[] = {
    { "compact", no_argument, 0, 'C' },
    { "version", no_argument, 0, 'V' },
    { "help", no_argument, 0, 'h' },
    {}
};

static const char *short_options = "CVh";

static bool compact = false;
static volatile sig_atomic_t stopped = 0;

struct last_frame {
    bool valid;
    canid_t can_id;
    uint8_t can_dlc;
    uint8_t data[CAN_MAX_DLEN];
};

static void usage(const char *progname, int exitcode)
{
    fprintf(stderr,
            "%s (%s) -- Protocol-aware dump of UART frames mirrored to CAN\n\n"
            "Usage: %s [<options>] <can-interface>\n\n"
            "Options:\n"
            "\t-C, --compact    only print changed payloads per CAN ID\n"
            "\t-V, --version    print version and exit\n"
            "\t-h, --help       print this usage and exit\n\n",
            progname, PACKAGE_STRING, progname);

    exit(exitcode);
}

static void handle_signal(int signum)
{
    (void)signum;
    stopped = 1;
}

static int setup_socket(const char *ifname)
{
    struct ifreq ifr;
    struct sockaddr_can addr;
    int enable = 1;
    int fd;

    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        fprintf(stderr, "Error: could not create CAN socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "Error: could not resolve interface '%s': %s\n", ifname, strerror(errno));
        close(fd);
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, &enable, sizeof(enable)) < 0) {
        fprintf(stderr, "Error: could not enable receive timestamps: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error: could not bind CAN socket to '%s': %s\n", ifname, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void format_timestamp(char *buffer, size_t size, const struct timeval *tv)
{
    struct tm tm_local;

    if (!tv) {
        snprintf(buffer, size, "1970-01-01 00:00:00.000000");
        return;
    }

    localtime_r(&tv->tv_sec, &tm_local);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm_local);
    snprintf(buffer + strlen(buffer), size > strlen(buffer) ? size - strlen(buffer) : 0,
             ".%06ld", (long)tv->tv_usec);
}

static bool compact_skip(struct last_frame last_frames[256], const struct can_frame *frame)
{
    unsigned int id = frame->can_id & CAN_EFF_MASK;
    struct last_frame *last;

    if (!compact)
        return false;

    if (id >= 256)
        return false;

    last = &last_frames[id];
    if (last->valid &&
        last->can_id == frame->can_id &&
        last->can_dlc == frame->can_dlc &&
        memcmp(last->data, frame->data, frame->can_dlc) == 0)
        return true;

    last->valid = true;
    last->can_id = frame->can_id;
    last->can_dlc = frame->can_dlc;
    memcpy(last->data, frame->data, frame->can_dlc);

    return false;
}

static void print_frame(const char *ifname, const struct can_frame *frame, const struct timeval *tv)
{
    char timestamp[64];
    char decoded[512];
    canid_t id = frame->can_id & CAN_EFF_MASK;
    uint64_t payload = 0;
    unsigned int i;
    int rv;

    format_timestamp(timestamp, sizeof(timestamp), tv);

    printf("(%s)  %s  %08X   [%u]", timestamp, ifname, id, frame->can_dlc);
    for (i = 0; i < frame->can_dlc; ++i)
        printf("  %02X", frame->data[i]);

    if ((frame->can_id & CAN_EFF_FLAG) && frame->can_dlc == 8 && id <= 0xff) {
        memcpy(&payload, frame->data, sizeof(payload));
        rv = cb_proto_frame_to_str(decoded, sizeof(decoded), id, be64toh(payload));
    } else {
        rv = snprintf(decoded, sizeof(decoded), "CAN: not a mirrored UART data frame");
    }

    if (rv >= 0)
        printf("  | %s", decoded);

    printf("\n");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    struct last_frame last_frames[256] = { 0 };
    struct sigaction sa;
    const char *ifname;
    struct pollfd pfd;
    int fd;

    while (1) {
        int c = getopt_long(argc, argv, short_options, long_options, NULL);

        if (c == -1)
            break;

        switch (c) {
        case 'C':
            compact = true;
            break;
        case 'V':
            printf("%s (%s)\n", argv[0], PACKAGE_STRING);
            return EXIT_SUCCESS;
        case 'h':
            usage(argv[0], EXIT_SUCCESS);
            break;
        case '?':
        default:
            usage(argv[0], EXIT_FAILURE);
        }
    }

    argc -= optind;
    argv += optind;

    if (argc != 1)
        usage(program_invocation_short_name, EXIT_FAILURE);

    ifname = argv[0];
    fd = setup_socket(ifname);
    if (fd < 0)
        return EXIT_FAILURE;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!stopped) {
        int rv = poll(&pfd, 1, 1000);

        if (rv < 0) {
            if (errno == EINTR)
                continue;

            fprintf(stderr, "Error: poll failed on '%s': %s\n", ifname, strerror(errno));
            close(fd);
            return EXIT_FAILURE;
        }

        if (rv == 0)
            continue;

        if ((pfd.revents & POLLIN) != 0) {
            struct can_frame frame;
            struct iovec iov = {
                .iov_base = &frame,
                .iov_len = sizeof(frame),
            };
            char control[CMSG_SPACE(sizeof(struct timeval))];
            struct msghdr msg;
            struct timeval tv_now;
            struct timeval *tv = NULL;
            struct cmsghdr *cmsg;
            ssize_t len;

            memset(&frame, 0, sizeof(frame));
            memset(&msg, 0, sizeof(msg));
            memset(control, 0, sizeof(control));
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;
            msg.msg_control = control;
            msg.msg_controllen = sizeof(control);

            len = recvmsg(fd, &msg, 0);
            if (len < 0) {
                if (errno == EINTR)
                    continue;

                fprintf(stderr, "Error: recvmsg failed on '%s': %s\n", ifname, strerror(errno));
                close(fd);
                return EXIT_FAILURE;
            }

            if ((size_t)len < sizeof(struct can_frame)) {
                fprintf(stderr, "Error: short CAN frame received on '%s' (%zd bytes)\n", ifname, len);
                close(fd);
                return EXIT_FAILURE;
            }

            for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMP) {
                    tv = (struct timeval *)CMSG_DATA(cmsg);
                    break;
                }
            }

            if (!tv) {
                gettimeofday(&tv_now, NULL);
                tv = &tv_now;
            }

            if (compact_skip(last_frames, &frame))
                continue;

            print_frame(ifname, &frame, tv);
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
