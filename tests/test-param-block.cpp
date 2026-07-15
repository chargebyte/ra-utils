// Copyright © 2026 chargebyte GmbH
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

extern "C" {
#include "param_block.h"
}

namespace {

TEST(ParamBlockTest, StrToVersionAcceptsValidValues)
{
    uint16_t version = 0;

    EXPECT_EQ(str_to_version("1", &version), 0);
    EXPECT_EQ(version, 1);

    EXPECT_EQ(str_to_version("65535", &version), 0);
    EXPECT_EQ(version, 65535);
}

TEST(ParamBlockTest, StrToVersionRejectsInvalidValues)
{
    uint16_t version = 99;

    errno = 0;
    EXPECT_EQ(str_to_version("0", &version), -1);
    errno = 0;
    EXPECT_EQ(str_to_version("-1", &version), -1);
    errno = 0;
    EXPECT_EQ(str_to_version("65536", &version), -1);
    errno = 0;
    EXPECT_EQ(str_to_version("2x", &version), -1);
}

TEST(ParamBlockTest, StrToTemperatureHandlesAliasesRoundingAndClamping)
{
    int16_t temperature = 0;
    char buffer[32];

    EXPECT_EQ(str_to_temperature("disabled", &temperature), 0);
    EXPECT_EQ(le16toh(temperature), CHANNEL_DISABLE_VALUE);

    EXPECT_EQ(str_to_temperature("12.34 °C", &temperature), 0);
    EXPECT_EQ(le16toh(temperature), 123);

    EXPECT_EQ(str_to_temperature("250.0 °C", &temperature), 0);
    EXPECT_EQ(le16toh(temperature), 2000);

    EXPECT_EQ(str_to_temperature("-100.0 °C", &temperature), 0);
    EXPECT_STREQ((temperature_to_str(buffer, sizeof(buffer), temperature), buffer), "-80.0 °C");
}

TEST(ParamBlockTest, StrToTemperatureRejectsInvalidSuffix)
{
    int16_t temperature = 0;

    errno = 0;
    EXPECT_EQ(str_to_temperature("12.3C", &temperature), -1);
    errno = 0;
    EXPECT_EQ(str_to_temperature("foo", &temperature), -1);
}

TEST(ParamBlockTest, TemperatureToStrHandlesDisabledAndLegacyDisabled)
{
    char buffer[32];

    EXPECT_STREQ((temperature_to_str(buffer, sizeof(buffer), htole16(CHANNEL_DISABLE_VALUE)), buffer), "disabled");
    EXPECT_STREQ((temperature_to_str(buffer, sizeof(buffer), htole16(OLD_CHANNEL_DISABLE_VALUE)), buffer), "disabled");
    EXPECT_STREQ((temperature_to_str(buffer, sizeof(buffer), htole16(155)), buffer), "15.5 °C");
}

TEST(ParamBlockTest, StrToResistanceOffsetHandlesUnitsRoundingAndClamping)
{
    int16_t offset = 0;
    char buffer[32];

    EXPECT_EQ(str_to_resistance_offset("1.234 Ω", &offset), 0);
    EXPECT_EQ(le16toh(offset), 1234);

    EXPECT_EQ(str_to_resistance_offset("1.234 Ω", &offset), 0);
    EXPECT_EQ(le16toh(offset), 1234);

    EXPECT_EQ(str_to_resistance_offset("40 Ω", &offset), 0);
    EXPECT_EQ(le16toh(offset), 32000);

    EXPECT_EQ(str_to_resistance_offset("-40 Ω", &offset), 0);
    EXPECT_STREQ((resistance_offset_to_str(buffer, sizeof(buffer), offset), buffer), "-32.000 Ω");
}

TEST(ParamBlockTest, StrToResistanceOffsetRejectsInvalidInput)
{
    int16_t offset = 0;

    errno = 0;
    EXPECT_EQ(str_to_resistance_offset("1.2 ohm", &offset), -1);
    errno = 0;
    EXPECT_EQ(str_to_resistance_offset("foo", &offset), -1);
}

TEST(ParamBlockTest, ResistanceOffsetToStrFormatsMilliohmResolution)
{
    char buffer[32];

    EXPECT_STREQ((resistance_offset_to_str(buffer, sizeof(buffer), htole16(1234)), buffer), "1.234 Ω");
}

TEST(ParamBlockTest, ContactorTypeParsingSupportsAliases)
{
    EXPECT_EQ(str_to_contactor_type("without-feedback"), CONTACTOR_WITHOUT_FEEDBACK);
    EXPECT_EQ(str_to_contactor_type("none"), CONTACTOR_NONE);
    EXPECT_EQ(str_to_contactor_type("with-feedback"), CONTACTOR_WITH_FEEDBACK_NC);
    EXPECT_EQ(str_to_contactor_type("invalid"), CONTACTOR_MAX);
    EXPECT_STREQ(contactor_type_to_str(CONTACTOR_WITH_FEEDBACK_NO), "with-feedback-normally-open");
    EXPECT_STREQ(contactor_type_to_str(CONTACTOR_MAX), "invalid");
}

TEST(ParamBlockTest, InletTypeParsingSupportsAliases)
{
    EXPECT_EQ(str_to_inlet_type("none"), INLET_NONE);
    EXPECT_EQ(str_to_inlet_type("disable"), INLET_NONE);
    EXPECT_EQ(str_to_inlet_type("disabled"), INLET_NONE);
    EXPECT_EQ(str_to_inlet_type("without-feedback"), INLET_WITHOUT_FEEDBACK);
    EXPECT_EQ(str_to_inlet_type("with-feedback"), INLET_WITH_FEEDBACK);
    EXPECT_EQ(str_to_inlet_type("invalid"), INLET_MAX);
    EXPECT_STREQ(inlet_type_to_str(INLET_WITH_FEEDBACK), "with-feedback");
    EXPECT_STREQ(inlet_type_to_str(INLET_MAX), "invalid");
}

TEST(ParamBlockTest, TimeParsingConvertsAndClamps)
{
    uint8_t time = 0;
    char buffer[32];

    EXPECT_EQ(str_to_contactor_time("250 ms", &time), 0);
    EXPECT_EQ(time, 25);
    EXPECT_STREQ((contactor_time_to_str(buffer, sizeof(buffer), time), buffer), "250 ms");

    EXPECT_EQ(str_to_contactor_time("999999 ms", &time), 0);
    EXPECT_EQ(time, 255);

    EXPECT_EQ(str_to_rcm_time("260 ms", &time), 0);
    EXPECT_EQ(time, 13);
    EXPECT_STREQ((rcm_time_to_str(buffer, sizeof(buffer), time), buffer), "260 ms");

    EXPECT_EQ(str_to_rcm_time("999999 ms", &time), 0);
    EXPECT_EQ(time, 255);

    EXPECT_EQ(str_to_inlet_time("130 ms", &time), 0);
    EXPECT_EQ(time, 13);
    EXPECT_STREQ((inlet_time_to_str(buffer, sizeof(buffer), time), buffer), "130 ms");
}

TEST(ParamBlockTest, TimeParsingRejectsInvalidSuffix)
{
    uint8_t time = 0;

    errno = 0;
    EXPECT_EQ(str_to_contactor_time("100", &time), -1);
    errno = 0;
    EXPECT_EQ(str_to_inlet_time("100", &time), -1);
    errno = 0;
    EXPECT_EQ(str_to_rcm_time("foo", &time), -1);
}

TEST(ParamBlockTest, MillivoltParsingConvertsAndClamps)
{
    uint16_t mv = 0;
    char buffer[32];

    EXPECT_EQ(str_to_mv("2200 mV", &mv), 0);
    EXPECT_EQ(le16toh(mv), 2200);
    EXPECT_STREQ((mv_to_str(buffer, sizeof(buffer), mv), buffer), "2200 mV");

    EXPECT_EQ(str_to_mv("999999 mV", &mv), 0);
    EXPECT_EQ(le16toh(mv), 3300);
}

TEST(ParamBlockTest, MillivoltParsingRejectsInvalidSuffix)
{
    uint16_t mv = 0;

    errno = 0;
    EXPECT_EQ(str_to_mv("2200", &mv), -1);
    errno = 0;
    EXPECT_EQ(str_to_mv("foo", &mv), -1);
}

TEST(ParamBlockTest, PinPolarityAndDisabledFlagSupportAliases)
{
    bool flag = true;

    EXPECT_EQ(str_to_pin_polarity_type("active-low"), PIN_POLARITY_ACTIVE_LOW);
    EXPECT_EQ(str_to_pin_polarity_type("off"), PIN_POLARITY_NONE);
    EXPECT_EQ(str_to_pin_polarity_type("invalid"), PIN_POLARITY_MAX);
    EXPECT_STREQ(pin_polarity_type_to_str(PIN_POLARITY_ACTIVE_HIGH), "active-high");
    EXPECT_STREQ(pin_polarity_type_to_str(PIN_POLARITY_MAX), "invalid");

    EXPECT_EQ(str_to_disabled_flag("disabled", &flag), 0);
    EXPECT_FALSE(flag);
    EXPECT_EQ(str_to_disabled_flag("none", &flag), 0);
    EXPECT_FALSE(flag);
    EXPECT_EQ(str_to_disabled_flag("enabled", &flag), 1);
}

TEST(ParamBlockTest, InitFunctionsSetMarkersDefaultsAndValidCrc)
{
    struct param_block_v1 pb_v1;
    struct param_block_v2 pb_v2;

    pb_init_v1(&pb_v1);
    pb_init_v2(&pb_v2);

    EXPECT_EQ(le32toh(pb_v1.sob), MARKER);
    EXPECT_EQ(le32toh(pb_v1.eob), MARKER);
    EXPECT_EQ(pb_v1.version, 1);
    EXPECT_TRUE(pb_check_crc_v1(&pb_v1));
    for (size_t i = 0; i < sizeof(pb_v1.temperature) / sizeof(pb_v1.temperature[0]); ++i)
        EXPECT_EQ(le16toh(pb_v1.temperature[i]), CHANNEL_DISABLE_VALUE);

    EXPECT_EQ(le32toh(pb_v2.sob), MARKER);
    EXPECT_EQ(le32toh(pb_v2.eob), MARKER);
    EXPECT_EQ(pb_v2.version, 2);
    EXPECT_TRUE(pb_check_crc_v2(&pb_v2));
    for (size_t i = 0; i < sizeof(pb_v2.temperature) / sizeof(pb_v2.temperature[0]); ++i)
        EXPECT_EQ(le16toh(pb_v2.temperature[i]), CHANNEL_DISABLE_VALUE);
    EXPECT_EQ(pb_v2.inlet_type, INLET_NONE);
    EXPECT_EQ(pb_v2.inlet_open_time, 0);
    EXPECT_EQ(pb_v2.inlet_close_time, 0);
    EXPECT_EQ(le16toh(pb_v2.inlet_feedback_open_valid_min_mv), 0);
    EXPECT_EQ(le16toh(pb_v2.inlet_feedback_open_valid_max_mv), 0);
    EXPECT_EQ(le16toh(pb_v2.inlet_feedback_closed_valid_min_mv), 0);
    EXPECT_EQ(le16toh(pb_v2.inlet_feedback_closed_valid_max_mv), 0);
}

TEST(ParamBlockTest, EnableHelpersReflectStoredValues)
{
    struct param_block_v2 pb = {};

    pb_init_v2(&pb);
    EXPECT_FALSE(pb_is_pt1000_enabled(reinterpret_cast<struct param_block_v1 *>(&pb), 0));
    EXPECT_FALSE(pb_is_contactor_enabled(reinterpret_cast<struct param_block_v1 *>(&pb), 0));
    EXPECT_FALSE(pb_is_rcm_enabled(&pb));

    pb.temperature[0] = htole16(50);
    pb.contactor_type[0] = CONTACTOR_WITHOUT_FEEDBACK;
    pb.rcm_fault_polarity = PIN_POLARITY_ACTIVE_LOW;
    pb.rcm_test_polarity = PIN_POLARITY_ACTIVE_HIGH;

    EXPECT_TRUE(pb_is_pt1000_enabled(reinterpret_cast<struct param_block_v1 *>(&pb), 0));
    EXPECT_TRUE(pb_is_contactor_enabled(reinterpret_cast<struct param_block_v1 *>(&pb), 0));
    EXPECT_TRUE(pb_is_rcm_enabled(&pb));
}

TEST(ParamBlockTest, RefreshCrcRestoresValidityAfterMutation)
{
    struct param_block_v1 pb_v1;
    struct param_block_v2 pb_v2;

    pb_init_v1(&pb_v1);
    pb_init_v2(&pb_v2);

    pb_v1.estop[0] = PIN_POLARITY_ACTIVE_HIGH;
    pb_v2.estop[0] = PIN_POLARITY_ACTIVE_HIGH;

    EXPECT_FALSE(pb_check_crc_v1(&pb_v1));
    EXPECT_FALSE(pb_check_crc_v2(&pb_v2));

    pb_refresh_crc_v1(&pb_v1);
    pb_refresh_crc_v2(&pb_v2);

    EXPECT_TRUE(pb_check_crc_v1(&pb_v1));
    EXPECT_TRUE(pb_check_crc_v2(&pb_v2));
}

TEST(ParamBlockTest, DowngradeWarningsReportEachApplicableCondition)
{
    struct param_block_v2 pb;

    pb_init_v2(&pb);
    EXPECT_EQ(pb_get_downgrade_warnings(&pb, PB_VERSION_V2), 0U);
    EXPECT_EQ(pb_get_downgrade_warnings(&pb, PB_VERSION_V1), 0U);
    EXPECT_EQ(pb_get_downgrade_warnings(&pb, PB_VERSION_UNVERSIONED), 0U);

    pb.temperature_resistance_offset[0] = htole16(1);
    pb.contactor_close_time[0] = 1;
    pb.contactor_type[0] = CONTACTOR_WITH_FEEDBACK_NC;
    pb.rcm_fault_polarity = PIN_POLARITY_ACTIVE_LOW;

    EXPECT_EQ(pb_get_downgrade_warnings(&pb, PB_VERSION_V1), PB_WARN_DROP_RCM);
    EXPECT_EQ(pb_get_downgrade_warnings(&pb, PB_VERSION_UNVERSIONED),
              PB_WARN_DROP_V0_RESISTANCE_OFFSETS | PB_WARN_DROP_V0_CONTACTOR_TIMES |
                  PB_WARN_MAP_V0_CONTACTOR_WITH_FEEDBACK_NC | PB_WARN_DROP_RCM);
}

FILE *MakeTempFile()
{
    FILE *file = tmpfile();
    EXPECT_NE(file, nullptr) << std::strerror(errno);
    return file;
}

void RewindOrFail(FILE *file)
{
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fflush(file), 0) << std::strerror(errno);
    std::rewind(file);
}

TEST(ParamBlockTest, ReadWriteRoundTripSupportsUnversionedV1AndV2)
{
    struct param_block_v2 source;
    pb_init_v2(&source);
    source.temperature[0] = htole16(321);
    source.temperature_resistance_offset[0] = htole16(123);
    source.contactor_type[0] = CONTACTOR_WITH_FEEDBACK_NC;
    source.contactor_close_time[0] = 11;
    source.contactor_open_time[0] = 7;
    source.estop[0] = PIN_POLARITY_ACTIVE_HIGH;
    source.inlet_type = INLET_WITH_FEEDBACK;
    source.inlet_open_time = 9;
    source.inlet_close_time = 12;
    source.inlet_feedback_open_valid_min_mv = htole16(2200);
    source.inlet_feedback_open_valid_max_mv = htole16(2800);
    source.inlet_feedback_closed_valid_min_mv = htole16(1700);
    source.inlet_feedback_closed_valid_max_mv = htole16(2000);
    source.rcm_fault_polarity = PIN_POLARITY_ACTIVE_LOW;
    source.rcm_test_polarity = PIN_POLARITY_ACTIVE_HIGH;
    source.rcm_test_trigger_time = 3;
    source.rcm_test_check_tripped_time = 4;
    source.rcm_test_check_normal_time = 5;

    for (const auto version : {PB_VERSION_UNVERSIONED, PB_VERSION_V1, PB_VERSION_V2}) {
        SCOPED_TRACE(version);
        FILE *file = MakeTempFile();
        ASSERT_NE(file, nullptr);

        ASSERT_EQ(pb_write(&source, version, file), 0);
        RewindOrFail(file);

        struct param_block read_back = {};
        ASSERT_EQ(pb_read(file, &read_back), 0);

        EXPECT_EQ(read_back.version, version);
        switch (version) {
        case PB_VERSION_UNVERSIONED:
            EXPECT_EQ(le16toh(read_back.data.v0.temperature[0]), 321);
            EXPECT_EQ(read_back.data.v0.contactor[0], CONTACTOR_WITH_FEEDBACK_NO);
            EXPECT_EQ(read_back.data.v0.estop[0], PIN_POLARITY_ACTIVE_HIGH);
            break;
        case PB_VERSION_V1:
            EXPECT_EQ(le16toh(read_back.data.v1.temperature[0]), 321);
            EXPECT_EQ(le16toh(read_back.data.v1.temperature_resistance_offset[0]), 123);
            EXPECT_EQ(read_back.data.v1.contactor_type[0], CONTACTOR_WITH_FEEDBACK_NC);
            EXPECT_EQ(read_back.data.v1.contactor_close_time[0], 11);
            EXPECT_EQ(read_back.data.v1.contactor_open_time[0], 7);
            EXPECT_EQ(read_back.data.v1.estop[0], PIN_POLARITY_ACTIVE_HIGH);
            break;
        case PB_VERSION_V2:
            EXPECT_EQ(le16toh(read_back.data.v2.temperature[0]), 321);
            EXPECT_EQ(le16toh(read_back.data.v2.temperature_resistance_offset[0]), 123);
            EXPECT_EQ(read_back.data.v2.contactor_type[0], CONTACTOR_WITH_FEEDBACK_NC);
            EXPECT_EQ(read_back.data.v2.contactor_close_time[0], 11);
            EXPECT_EQ(read_back.data.v2.contactor_open_time[0], 7);
            EXPECT_EQ(read_back.data.v2.estop[0], PIN_POLARITY_ACTIVE_HIGH);
            EXPECT_EQ(read_back.data.v2.inlet_type, INLET_WITH_FEEDBACK);
            EXPECT_EQ(read_back.data.v2.inlet_open_time, 9);
            EXPECT_EQ(read_back.data.v2.inlet_close_time, 12);
            EXPECT_EQ(le16toh(read_back.data.v2.inlet_feedback_open_valid_min_mv), 2200);
            EXPECT_EQ(le16toh(read_back.data.v2.inlet_feedback_open_valid_max_mv), 2800);
            EXPECT_EQ(le16toh(read_back.data.v2.inlet_feedback_closed_valid_min_mv), 1700);
            EXPECT_EQ(le16toh(read_back.data.v2.inlet_feedback_closed_valid_max_mv), 2000);
            EXPECT_EQ(read_back.data.v2.rcm_fault_polarity, PIN_POLARITY_ACTIVE_LOW);
            EXPECT_EQ(read_back.data.v2.rcm_test_polarity, PIN_POLARITY_ACTIVE_HIGH);
            EXPECT_EQ(read_back.data.v2.rcm_test_trigger_time, 3);
            break;
        default:
            FAIL() << "Unexpected version";
        }

        EXPECT_EQ(std::fclose(file), 0);
    }
}

TEST(ParamBlockTest, ReadRejectsBadMagic)
{
    struct param_block_v2 pb;
    pb_init_v2(&pb);
    pb.sob = htole32(0x12345678);
    pb_refresh_crc_v2(&pb);

    FILE *file = MakeTempFile();
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(&pb, sizeof(pb), 1, file), 1U);
    RewindOrFail(file);

    struct param_block read_back = {};
    EXPECT_EQ(pb_read(file, &read_back), PB_READ_ERROR_MAGIC);

    EXPECT_EQ(std::fclose(file), 0);
}

TEST(ParamBlockTest, ReadRejectsBadCrc)
{
    struct param_block_v2 pb;
    pb_init_v2(&pb);
    pb.temperature[0] = htole16(123);
    pb_refresh_crc_v2(&pb);
    pb.crc ^= 0xff;

    FILE *file = MakeTempFile();
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(&pb, sizeof(pb), 1, file), 1U);
    RewindOrFail(file);

    struct param_block read_back = {};
    EXPECT_EQ(pb_read(file, &read_back), PB_READ_ERROR_CRC);

    EXPECT_EQ(std::fclose(file), 0);
}

}  // namespace
