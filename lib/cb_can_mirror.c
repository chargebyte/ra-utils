/*
 * Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include "cb_can_mirror.h"

int cb_can_mirror_open(const char *device)
{
    struct sockaddr_can addr;
    struct ifreq ifr;
    int fd;

    /* open a CAN RAW socket */
    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0)
        return -1;

    /* find out interface index */
    strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
    if (ioctl(fd, SIOCGIFINDEX, &ifr))
        goto err_out;

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    /* bind socket to this interface */
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)))
        goto err_out;

    return fd;

err_out:
    if (fd != -1)
        close(fd);

    return -1;
}

int cb_can_mirror_close(int fd)
{
    if (fd == -1)
        return 0;

    return close(fd);
}

int cb_can_mirror_write(int fd, uint8_t com, uint64_t data)
{
    struct can_frame can_frame;

    can_frame.can_id = CAN_EFF_FLAG | com;
    can_frame.len = CAN_MAX_DLEN;
    memcpy(&can_frame.data, &data, CAN_MAX_DLEN);

    return write(fd, &can_frame, sizeof(can_frame));
}
