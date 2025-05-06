/*
 * Copyright © 2024 chargebyte GmbH
 */
#include <endian.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include "uart.h"
#include "logging.h"
#include "tools.h"
#include "ra_protocol.h"

#define STARTUP_DELAY        500 /* in ms */
#define LOW_PULSE_DELAY      100 /* in ms */

#define RESPONSE_TIMEOUT     500 /* in ms */

static uint8_t LOW_PULSE_PATTERN = 0x00;
#define ACK_PATTERN                0x00

static uint8_t GENERIC_CODE_PATTERN = 0x55;
#define BOOT_CODE_PATTERN             0xC3

/* command codes */
#define INQUIRY_CMD           0x00
#define ERASE_CMD             0x12
#define WRITE_CMD             0x13
#define READ_CMD              0x15
#define ID_AUTHENTICATION_CMD 0x30
#define BAUDRATE_SETTING_CMD  0x34
#define SIGNATURE_REQUEST_CMD 0x3A
#define AREA_INFORMATION_CMD  0x3B

/* keep in sync with enum rwe_cmd */
static const char *rwe_cmd_str[] = {
    "ERASE_CMD",
    "WRITE_CMD",
    "READ_CMD",
};

/* response code handling */
#define RES_OK_MASK   0x00
#define RES_ERR_MASK  0x80

static inline bool is_res_error(uint8_t *res)
{
    return *res & RES_ERR_MASK == RES_ERR_MASK;
}

/* status codes */
#define STATUSCODE_OK                               0x00
#define STATUSCODE_UNSUPPORTED_CMD                  0xC0
#define STATUSCODE_PACKET_ERROR                     0xC1
#define STATUSCODE_CHECKSUM_ERROR                   0xC2
#define STATUSCODE_FLOW_ERROR                       0xC3
#define STATUSCODE_ADDRESS_ERROR                    0xD0
#define STATUSCODE_BAUDRATE_MARGIN_ERROR            0xD4
#define STATUSCODE_PROTECTION_ERROR                 0xDA
#define STATUSCODE_ID_MISMATCH_ERROR                0xDB
#define STATUSCODE_SERIAL_PROGRAMMING_DISABLE_ERROR 0xDC
#define STATUSCODE_ERASE_ERROR                      0xE1
#define STATUSCODE_WRITE_ERROR                      0xE2
#define STATUSCODE_SEQUENCER_ERROR                  0xE7

struct code_to_str {
    unsigned int code;
    const char *str;
};

static const struct code_to_str statuscode_status[] = {
    { STATUSCODE_OK,                               "STATUSCODE_OK"                               },
    { STATUSCODE_UNSUPPORTED_CMD,                  "STATUSCODE_UNSUPPORTED_CMD"                  },
    { STATUSCODE_PACKET_ERROR,                     "STATUSCODE_PACKET_ERROR"                     },
    { STATUSCODE_CHECKSUM_ERROR,                   "STATUSCODE_CHECKSUM_ERROR"                   },
    { STATUSCODE_FLOW_ERROR,                       "STATUSCODE_FLOW_ERROR"                       },
    { STATUSCODE_ADDRESS_ERROR,                    "STATUSCODE_ADDRESS_ERROR"                    },
    { STATUSCODE_BAUDRATE_MARGIN_ERROR,            "STATUSCODE_BAUDRATE_MARGIN_ERROR"            },
    { STATUSCODE_PROTECTION_ERROR,                 "STATUSCODE_PROTECTION_ERROR"                 },
    { STATUSCODE_ID_MISMATCH_ERROR,                "STATUSCODE_ID_MISMATCH_ERROR"                },
    { STATUSCODE_SERIAL_PROGRAMMING_DISABLE_ERROR, "STATUSCODE_SERIAL_PROGRAMMING_DISABLE_ERROR" },
    { STATUSCODE_ERASE_ERROR,                      "STATUSCODE_ERASE_ERROR"                      },
    { STATUSCODE_WRITE_ERROR,                      "STATUSCODE_WRITE_ERROR"                      },
    { STATUSCODE_SEQUENCER_ERROR,                  "STATUSCODE_SEQUENCER_ERROR"                  },
};

static const char *statuscode_str(unsigned int statuscode)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(statuscode_status); i++)
        if (statuscode_status[i].code == statuscode)
            return statuscode_status[i].str;

    return "UNKNOWN";
}

/* packet markers */
#define SOH 0x01
#define SOD 0x81
#define ETX 0x03

struct inquiry_cmd {
    uint8_t soh;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t com;
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

struct baudrate_cmd {
    uint8_t soh;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t com;
    uint32_t brt;
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

struct common_data_header {
    uint8_t sod;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t res;
} __attribute__((packed));

struct common_data_trailer {
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

struct status_rsp {
    uint8_t sod;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t res;
    uint8_t sts;
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

#define STATUS_RSP_LENGTH 0x0002

/* rwe means read-write-erase - all share a common packet layout */
struct rwe_cmd {
    uint8_t soh;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t com;
    uint32_t sad;
    uint32_t ead;
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

#define RWE_CMD_LENGTH 0x0009

#define MAX_DATA_PACKET_PAYLOAD 1024

/* we define the structure with maximum payload since we want to transmit/receive
 * as much data as possible in a single packet */
struct data_pkt {
    uint8_t sod;
    union {
        struct {
            uint8_t lnh;
            uint8_t lnl;
        } __attribute__((packed));
        uint16_t length;
    } __attribute__((packed));
    uint8_t res;
    uint8_t data[MAX_DATA_PACKET_PAYLOAD];
    uint8_t sum;
    uint8_t etx;
} __attribute__((packed));

int ra_comm_setup(struct uart_ctx *uart)
{
    uint8_t response_byte;
    ssize_t c;
    int rv;

    /* give CPU some time to startup */
    rv = usleep(STARTUP_DELAY * 1000);
    if (rv)
        return rv;

    /* drop possible accumulated noise and ensure that input queue is empty */
    rv = uart_flush_input(uart);
    if (rv)
        return rv;

    debug("sending 0x00 to setup communication");

    /* send out 2 times 0x00 to start communication */
    c = uart_write_drain(uart, &LOW_PULSE_PATTERN, sizeof(LOW_PULSE_PATTERN));
    if (c < 0)
        return rv;

    rv = usleep(LOW_PULSE_DELAY * 1000);
    if (rv)
        return rv;

    c = uart_write_drain(uart, &LOW_PULSE_PATTERN, sizeof(LOW_PULSE_PATTERN));
    if (c < 0)
        return rv;

    debug("receiving ACK pattern");

    /* now we should receive an ACK from the MCU */
    c = uart_read_with_timeout(uart, &response_byte, sizeof(response_byte), RESPONSE_TIMEOUT);
    if (c < 0)
        return c;

    if (response_byte != ACK_PATTERN) {
        error("ACK pattern mismatch: expected 0x%02" PRIx8 ", got 0x%02" PRIx8, ACK_PATTERN, response_byte);
        return -1;
    }

    debug("sending GENERIC_CODE_PATTERN");

    /* send the Generic Code */
    c = uart_write_drain(uart, &GENERIC_CODE_PATTERN, sizeof(GENERIC_CODE_PATTERN));
    if (c < 0)
        return rv;

    c = uart_read_with_timeout(uart, &response_byte, sizeof(response_byte), RESPONSE_TIMEOUT);
    if (c < 0)
        return c;

    if (response_byte != BOOT_CODE_PATTERN) {
        error("Boot code pattern mismatch: expected 0x%02" PRIx8 ", got 0x%02" PRIx8, BOOT_CODE_PATTERN, response_byte);
        return -1;
    }

    debug("MCU is now accepting commands");

    return 0;
}

static void ra_update_checksum(const uint8_t *buf, size_t len, uint8_t *sum)
{
    uint8_t chksum = 0;

    /* sum with overflow */
    while (len--)
        chksum += *buf++;

    /* calculate 2s complement */
    *sum = 0x00 - chksum;
}

/* returns 1 if checksum is invalid */
static bool ra_is_checksum_invalid(const uint8_t *buf, size_t len, const uint8_t sum)
{
    uint8_t chksum;

    ra_update_checksum(buf, len, &chksum);

    return chksum != sum;
}

static bool ra_is_invalid_status_pkt(struct status_rsp *status_rsp, const uint8_t cmd)
{
    return // invalid SOD
           status_rsp->sod != SOD
           // invalid ETX
           || status_rsp->etx != ETX
           // invalid length
           || be16toh(status_rsp->length) != STATUS_RSP_LENGTH
           // res is neither cmd nor ERR(cmd)
           || (status_rsp->res != cmd && status_rsp->res != (cmd | RES_ERR_MASK))
           // invalid checksum
           || ra_is_checksum_invalid(&status_rsp->lnh, sizeof(struct status_rsp) - 3, status_rsp->sum);
}

int ra_inquiry(struct uart_ctx *uart)
{
    static struct inquiry_cmd inquiry_cmd = { SOH, 0x00, 0x01, INQUIRY_CMD, 0xff, ETX };
    struct status_rsp status_rsp;
    ssize_t c;

    debug("sending INQUIRY_CMD");

    c = uart_write_drain(uart, (const uint8_t *)&inquiry_cmd, sizeof(inquiry_cmd));
    if (c < 0)
        return c;

    debug("waiting for INQUIRY_CMD response");

    c = uart_read_with_timeout(uart, (uint8_t *)&status_rsp, sizeof(status_rsp), RESPONSE_TIMEOUT);
    if (c < 0)
        return c;

    if (ra_is_invalid_status_pkt(&status_rsp, INQUIRY_CMD)) {
        error("unexpected response for INQUIRY_CMD");
        uart_dump_frame(false, (uint8_t *)&status_rsp, sizeof(status_rsp));
        return -1;
    }

    if (status_rsp.res != INQUIRY_CMD || status_rsp.sts != STATUSCODE_OK) {
        error("INQUIRY_CMD failed: RES=0x%02" PRIx8 ", STS=0x%02" PRIx8 " (%s)",
              status_rsp.res, status_rsp.sts, statuscode_str(status_rsp.sts));
        return -1;
    }

    debug("INQUIRY_CMD succeeded");
    return 0;
}

int ra_set_baudrate(struct uart_ctx *uart, int baudrate)
{
    struct baudrate_cmd baudrate_cmd = { SOH, 0x00, 0x05, BAUDRATE_SETTING_CMD, 0x00000000, 0x00, ETX };
    struct status_rsp status_rsp;
    ssize_t c;

    baudrate_cmd.brt = htobe32(baudrate);

    /* checksum without SOH, SUM itself and without ETX */
    ra_update_checksum(&baudrate_cmd.lnh, sizeof(baudrate_cmd) - 3, &baudrate_cmd.sum);

    debug("sending BAUDRATE_SETTING_CMD");

    c = uart_write_drain(uart, (const uint8_t *)&baudrate_cmd, sizeof(baudrate_cmd));
    if (c < 0)
        return c;

    debug("waiting for BAUDRATE_SETTING_CMD response");

    c = uart_read_with_timeout(uart, (uint8_t *)&status_rsp, sizeof(status_rsp), RESPONSE_TIMEOUT);
    if (c < 0)
        return c;

    if (ra_is_invalid_status_pkt(&status_rsp, BAUDRATE_SETTING_CMD)) {
        error("unexpected response while trying to change baudrate");
        uart_dump_frame(false, (uint8_t *)&status_rsp, sizeof(status_rsp));
        return -1;
    }

    if (status_rsp.res != BAUDRATE_SETTING_CMD || status_rsp.sts != STATUSCODE_OK) {
        error("BAUDRATE_SETTING_CMD failed: RES=0x%02" PRIx8 ", STS=0x%02" PRIx8 " (%s)",
              status_rsp.res, status_rsp.sts, statuscode_str(status_rsp.sts));
        return -1;
    }

    debug("BAUDRATE_SETTING_CMD succeeded");
    return 0;
}

int ra_rwe_cmd(struct uart_ctx *uart, enum rwe_command rwe, uint32_t start_addr, uint32_t end_addr)
{
    struct rwe_cmd rwe_cmd;
    struct status_rsp status_rsp;
    ssize_t c;

    rwe_cmd.soh = SOH;
    rwe_cmd.length = htobe16(RWE_CMD_LENGTH);

    switch (rwe) {
    case RWE_ERASE:
        rwe_cmd.com = ERASE_CMD;
        break;
    case RWE_WRITE:
        rwe_cmd.com = WRITE_CMD;
        break;
    case RWE_READ:
        rwe_cmd.com = READ_CMD;
        break;
    }

    rwe_cmd.sad = htobe32(start_addr);
    rwe_cmd.ead = htobe32(end_addr);
    rwe_cmd.etx = ETX;

    /* checksum without SOH, SUM itself and without ETX */
    ra_update_checksum(&rwe_cmd.lnh, sizeof(rwe_cmd) - 3, &rwe_cmd.sum);

    debug("sending %s [0x%08" PRIx32 "-0x%08" PRIx32 "]", rwe_cmd_str[rwe], start_addr, end_addr);

    c = uart_write_drain(uart, (const uint8_t *)&rwe_cmd, sizeof(rwe_cmd));
    if (c < 0)
        return c;

    /* the read command does not send a status response - data follows directly */
    if (rwe != RWE_READ) {
        debug("waiting for %s response", rwe_cmd_str[rwe]);

        c = uart_read_with_timeout(uart, (uint8_t *)&status_rsp, sizeof(status_rsp), RESPONSE_TIMEOUT);
        if (c < 0)
            return c;

        if (ra_is_invalid_status_pkt(&status_rsp, rwe_cmd.com)) {
            error("unexpected response for %s", rwe_cmd_str[rwe]);
            uart_dump_frame(false, (uint8_t *)&status_rsp, sizeof(status_rsp));
            return -1;
        }

        if (status_rsp.res != rwe_cmd.com || status_rsp.sts != STATUSCODE_OK) {
            error("%s failed: RES=0x%02" PRIx8 ", rwe_cmd_str[rwe], STS=0x%02" PRIx8 " (%s)",
                  rwe_cmd_str[rwe], status_rsp.res, status_rsp.sts, statuscode_str(status_rsp.sts));
            return -1;
        }
    }

    debug("%s succeeded", rwe_cmd_str[rwe]);
    return 0;
}

int ra_write_data(struct uart_ctx *uart, const uint8_t *payload, size_t len)
{
    struct data_pkt data_pkt;
    struct status_rsp status_rsp;
    uint8_t *p;
    ssize_t c;

    /* safety check */
    if (len > sizeof(data_pkt.data)) {
        errno = EFBIG;
        return -1;
    }

    data_pkt.sod = SOD;
    data_pkt.length = htobe16(len + 1);
    data_pkt.res = WRITE_CMD;
    memcpy(&data_pkt.data, payload, len);

    /* the data to write might be shorter than our full packet length,
     * so we must determine where to place SUM and ETX;
     * first let's point it to SUM */
    p = (uint8_t *)&data_pkt;
    p += sizeof(struct common_data_header) + len;

    /* checksum without SOH, SUM itself and without ETX */
    ra_update_checksum(&data_pkt.lnh, len + 3, p);

    /* let's add ETX finally */
    p++;
    *p = ETX;

    debug("sending data packet");

    c = uart_write_drain(uart, (const uint8_t *)&data_pkt,
                         sizeof(struct common_data_header) + len + sizeof(struct common_data_trailer));
    if (c < 0)
        return c;

    debug("waiting for data packet status response");

    c = uart_read_with_timeout(uart, (uint8_t *)&status_rsp, sizeof(status_rsp), RESPONSE_TIMEOUT);
    if (c < 0)
        return c;

    if (ra_is_invalid_status_pkt(&status_rsp, WRITE_CMD)) {
        error("unexpected response for data packet status");
        uart_dump_frame(false, (uint8_t *)&status_rsp, sizeof(status_rsp));
        return -1;
    }

    if (status_rsp.res != WRITE_CMD || status_rsp.sts != STATUSCODE_OK) {
        error("data packet failed: RES=0x%02" PRIx8 ", STS=0x%02" PRIx8 " (%s)",
              status_rsp.res, status_rsp.sts, statuscode_str(status_rsp.sts));
        return -1;
    }

    debug("data packet succeeded");
    return 0;
}

static bool ra_is_invalid_data_pkt(struct data_pkt *data_pkt, const uint8_t cmd)
{
    size_t len = be16toh(data_pkt->length);
    uint8_t *p;

    /* check header first */
    if (// invalid SOD
        data_pkt->sod != SOD
        // invalid length
        || (len == 0 || len > MAX_DATA_PACKET_PAYLOAD + 1)
        // res is neither cmd nor ERR(cmd)
        || (data_pkt->res != cmd && data_pkt->res != (cmd | RES_ERR_MASK))) {
        debug("header looks ugly");
        return true;
    }

    /* point to packet start first */
    p = (uint8_t *)data_pkt;
    p += sizeof(struct common_data_header) + len;

    /* now p should point to the ETX byte, so let's check it */
    if (*p != ETX) {
        debug("wrong byte at calculated ETX position, seeing 0x%02" PRIx8 " there instead of 0x%02x", *p, ETX);
        return true;
    }

    /* re-use this pointer to point to SUM byte */
    p--;

    /* finally check checksum */
    if (ra_is_checksum_invalid(&data_pkt->lnh, len + 2 /* length field itself */, *p)) {
        debug("checksum mismatch");
        return true;
    }

    return false;
}

/* we assume that caller knows how much data will be available */
int ra_read_data(struct uart_ctx *uart, uint8_t *buffer, size_t bufsize, bool ack)
{
    struct data_pkt data_pkt;
    ssize_t c;

    /* safety check */
    if (bufsize > sizeof(data_pkt.data)) {
        errno = EFBIG;
        return -1;
    }

    memset(&data_pkt, 0, sizeof(data_pkt));

    debug("waiting for data packet");

    c = uart_read_with_timeout(uart, (uint8_t *)&data_pkt,
                               bufsize + sizeof(struct common_data_header) + sizeof(struct common_data_trailer),
                               RESPONSE_TIMEOUT);
    if (c < 0) {
        if (errno == ETIMEDOUT) {
            error("timeout while receiving data packet, what we got so far follows (dump of full buffer):");
            uart_dump_frame(false, (uint8_t *)&data_pkt, sizeof(data_pkt));
        }
        return c;
    }

    if (ra_is_invalid_data_pkt(&data_pkt, READ_CMD)) {
        error("unexpected response for data packet");
        uart_dump_frame(false, (uint8_t *)&data_pkt, sizeof(data_pkt));
        return -1;
    }

    if (data_pkt.res != READ_CMD) {
        /* in this case this should be an status error packet, thus we can cast */
        struct status_rsp *status_rsp = (struct status_rsp *)&data_pkt;
        error("received status error instead of data packet: RES=0x%02" PRIx8 ", STS=0x%02" PRIx8 " (%s)",
              status_rsp->res, status_rsp->sts, statuscode_str(status_rsp->sts));
        return -1;
    }

    /* copy data */
    memcpy(buffer, &data_pkt.data, bufsize);

    /* respond with status packet if desired */
    if (ack) {
        struct status_rsp status_rsp = { SOD, 0x00, 0x02, READ_CMD, STATUSCODE_OK, 0xe9, ETX };

        debug("sending data packet status (confirmation)");

        c = uart_write_drain(uart, (const uint8_t *)&status_rsp, sizeof(status_rsp));
        if (c < 0)
            return c;
    }

    debug("successfully received a data packet");

    return 0;
}

/* FIXME: assumes that only one single data packet is received -> TBD: looping */
int ra_read(struct uart_ctx *uart, uint8_t *buffer, uint32_t start_addr, size_t len)
{
    uint32_t end_addr = start_addr + len - 1;
    int rv;

    /* safety check - remove when implementation is complete */
    if (len > MAX_DATA_PACKET_PAYLOAD) {
        error("requested size for reading to big - not implemented yet");
        errno = EFBIG;
        return -1;
    }

    rv = ra_rwe_cmd(uart, RWE_READ, start_addr, end_addr);
    if (rv)
        return rv;

    rv = ra_read_data(uart, buffer, len, false);
    if (rv)
        return rv;

    return 0;
}

int ra_write(struct uart_ctx *uart, uint32_t start_addr, uint8_t *buffer, size_t len)
{
    uint32_t end_addr = start_addr + len - 1;
    size_t already_written = 0;
    int rv;

    rv = ra_rwe_cmd(uart, RWE_WRITE, start_addr, end_addr);
    if (rv)
        return rv;

    while (already_written < len) {
        size_t len_for_this_round = min(len - already_written, MAX_DATA_PACKET_PAYLOAD);
        uint32_t cur_addr = start_addr + already_written;

        debug("writing  0x%08" PRIx32 "-0x%08" PRIx32, cur_addr, (uint32_t)(cur_addr + len_for_this_round - 1));

        rv = ra_write_data(uart, &buffer[already_written], len_for_this_round);
        if (rv)
            return rv;

        already_written += len_for_this_round;
    }

    return 0;
}
