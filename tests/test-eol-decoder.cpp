// Copyright © 2026 chargebyte GmbH
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <cstdint>
#include <string>
extern "C" {
#include "cb_protocol.h"
}

namespace {
std::string Decode(enum cb_uart_com com, uint64_t data)
{
    char buffer[2048];
    EXPECT_GE(cb_proto_frame_to_str(buffer, sizeof(buffer), com, data), 0);
    return buffer;
}

TEST(EolDecoder, DigitalInputsMatchFirmwarePinPacking)
{
    // Byte index and bit within the UART payload, independent of host byte order.
    struct Pin { const char *name; unsigned byte, bit; };
    const Pin pins[] = {
        {"DI_EV_CP_EDGE", 0, 2}, {"DI_CP_INVERT", 0, 3},
        {"DI_SAFETY_MOTOR_FAULT", 0, 4}, {"DI_SAFETY_MOTOR_OUT2", 0, 5},
        {"DI_SAFETY_MOTOR_OUT1", 0, 6}, {"DI_SAFETY_K1_IN", 0, 7},
        {"DI_SAFETY_K2_IN", 1, 0}, {"DI_PT4_EN", 1, 2},
        {"DI_SAFETY_IMD_OD_EN", 1, 3}, {"DI_SAFETY_GPIO_EXP", 1, 4},
        {"DI_PP_SAE_IEC", 1, 7}, {"DI_ESTOP3", 2, 0},
        {"DI_ESTOP2", 2, 1}, {"DI_ESTOP1", 2, 2},
        {"DI_SAFETY_CP_STATE_C", 2, 3}, {"DI_PT3_EN", 2, 4},
        {"DI_PT2_EN", 2, 5}, {"DI_PT1_EN", 2, 6},
    };
    for (const auto &pin : pins) {
        const auto text = Decode(COM_DIGITAL_INPUT, UINT64_C(1) << ((7 - pin.byte) * 8 + pin.bit));
        for (const auto &expected : pins)
            EXPECT_NE(text.find(std::string(expected.name) + (expected.name == pin.name ? "=1" : "=0")),
                      std::string::npos) << pin.name << ": " << text;
    }
}

TEST(EolDecoder, DigitalOutputsMatchFirmwareUnpacking)
{
    const char *names[] = {
        "DO_CP_INVERT", "DO_SAFETY_MOTOR_OUT2", "DO_SAFETY_MOTOR_OUT1",
        "DO_PT1000_CTRLEN4", "DO_SAFETY_IMD_OD_EN", "DO_SAFETY_GPIO_EXP",
        "DO_PP_SAE_IEC", "DO_SAFETY_CP_STATE_C", "DO_PT1000_CTRLEN3",
        "DO_PT1000_CTRLEN2", "DO_PT1000_CTRLEN1"
    };
    const unsigned positions[] = {3, 5, 6, 10, 11, 12, 15, 18, 19, 20, 21};
    for (unsigned i = 0; i < 11; ++i) {
        const unsigned pos = positions[i];
        const auto text = Decode(COM_DIGITAL_OUTPUT, UINT64_C(1) << ((7 - pos / 8) * 8 + pos % 8));
        for (unsigned j = 0; j < 11; ++j)
            EXPECT_NE(text.find(std::string(names[j]) + (i == j ? "=1" : "=0")), std::string::npos);
    }
    const auto text = Decode(COM_DIGITAL_OUTPUT, UINT64_C(0x0000003201026400));
    for (const char *value : {"DO_CP_Dutycycle=50%", "DO_HV_SW1_Dutycycle=1%",
                              "DO_HV_SW2_Dutycycle=2%", "DO_HV_SW3_Dutycycle=100%"})
        EXPECT_NE(text.find(value), std::string::npos);
}

TEST(EolDecoder, AnalogValuesStayUnsignedAndInWireOrder)
{
    const char *names[][4] = {
        {"AI_PT1000_LEAD1_1", "AI_PT1000_LEAD1_2", "AI_PT1000_LEAD1_3", "AI_PT1000_LEAD1_4"},
        {"AI_CP_BUFFERED_NEG_ADC", "AI_CP_BUFFERED_POS_ADC", "AI_PP_VALUE_ADC", "AI_SAFETY_MOTOR_CAPS_SENSE"},
        {"AI_SAFETY_DEV2_ADC", "AI_SAFETY_DEV1_ADC", "AI_SAFETY_INLET_FB", "AI_PT1000_CFB1_2"}
    };
    const char *values[] = {"=1", "=4660", "=32768", "=65535"};
    for (unsigned frame = 0; frame < 3; ++frame) {
        const auto text = Decode(static_cast<cb_uart_com>(COM_ANALOG_INPUT_01 + frame), UINT64_C(0x000112348000ffff));
        for (unsigned field = 0; field < 4; ++field)
            EXPECT_NE(text.find(std::string(names[frame][field]) + values[field]), std::string::npos);
    }
    EXPECT_EQ(Decode(COM_ANALOG_INPUT_04, UINT64_C(0xffff0123456789ab)),
              "COM_ANALOG_INPUT_04: AI_PT1000_CFB3_4=65535");
}

TEST(EolDecoder, CompleteDigitalLineAndSmallBuffers)
{
    const auto full = Decode(COM_DIGITAL_OUTPUT, UINT64_MAX);
    EXPECT_GT(full.size(), 300u);
    EXPECT_NE(full.find("DO_PT1000_CTRLEN1=1"), std::string::npos);
    char small[16] = {};
    cb_proto_frame_to_str(small, sizeof(small), COM_DIGITAL_INPUT, UINT64_MAX);
    EXPECT_EQ(small[sizeof(small) - 1], '\0');
    EXPECT_EQ(cb_proto_frame_to_str(nullptr, 0, COM_DIGITAL_INPUT, 0), 0);
}
} // namespace
