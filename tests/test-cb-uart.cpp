// Copyright © 2026 chargebyte GmbH
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <unistd.h>
#include <vector>

extern "C" {
#include "cb_uart.h"
#include "crc8_j1850.h"
#include "uart.h"
}

namespace {

constexpr std::size_t FrameSize = 12;

std::array<uint8_t, FrameSize> MakeFrame(uint8_t com, uint64_t data)
{
    std::array<uint8_t, FrameSize> frame{};

    frame[0] = 0xa5;
    frame[1] = com;
    for (std::size_t i = 0; i < sizeof(data); ++i)
        frame[2 + i] = data >> (8 * (sizeof(data) - i - 1));
    frame[10] = crc8_j1850(&frame[1], 1 + sizeof(data));
    frame[11] = 0x03;

    return frame;
}

void WriteAll(int fd, const std::vector<uint8_t> &bytes)
{
    std::size_t written = 0;

    while (written < bytes.size()) {
        const ssize_t rc = write(fd, bytes.data() + written, bytes.size() - written);
        ASSERT_GT(rc, 0);
        written += rc;
    }
}

class CbUartReceiveTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_EQ(pipe(fds_), 0);
        uart_.fd = fds_[0];
    }

    void TearDown() override
    {
        close(fds_[0]);
        close(fds_[1]);
    }

    void ReceiveAndExpect(const std::vector<uint8_t> &stream, enum cb_uart_com expected_com,
                          uint64_t expected_data)
    {
        enum cb_uart_com com = COM_MAX;
        uint64_t data = 0;

        WriteAll(fds_[1], stream);
        ASSERT_EQ(cb_uart_recv(&uart_, &com, &data), 0);
        EXPECT_EQ(com, expected_com);
        EXPECT_EQ(data, expected_data);
    }

    int fds_[2] = {-1, -1};
    struct uart_ctx uart_ = INIT_UART_CTX;
};

TEST_F(CbUartReceiveTest, ReceivesAlignedFrame)
{
    constexpr uint64_t data = 0x0123456789abcdefULL;
    const auto frame = MakeFrame(COM_DIGITAL_INPUT, data);

    ReceiveAndExpect({frame.begin(), frame.end()}, COM_DIGITAL_INPUT, data);
}

TEST_F(CbUartReceiveTest, SkipsBytesBeforeStartOfFrame)
{
    constexpr uint64_t data = 0xfedcba9876543210ULL;
    const auto frame = MakeFrame(COM_DIGITAL_OUTPUT, data);
    std::vector<uint8_t> stream{0x00, 0x01, 0x00, 0x00};
    stream.insert(stream.end(), frame.begin(), frame.end());

    ReceiveAndExpect(stream, COM_DIGITAL_OUTPUT, data);
}

TEST_F(CbUartReceiveTest, ContinuesAfterFalseStartOfFrame)
{
    constexpr uint64_t data = 0x1122334455667788ULL;
    const auto frame = MakeFrame(COM_CHARGE_STATE, data);
    std::vector<uint8_t> stream(FrameSize, 0x00);
    stream[0] = 0xa5;
    stream.insert(stream.end(), frame.begin(), frame.end());

    ReceiveAndExpect(stream, COM_CHARGE_STATE, data);
}

}  // namespace
