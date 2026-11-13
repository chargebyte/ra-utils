// Copyright © 2026 chargebyte GmbH
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include "eol_state.h"

namespace {
TEST(EolState, FirmwareIntegerVoltageConversion)
{
    EXPECT_EQ(eol_adc_mv(0), 0);
    EXPECT_EQ(eol_adc_mv(1), 0); // Firmware truncates before applying CP divider.
    EXPECT_EQ(eol_adc_mv(2048), 1650);
    EXPECT_EQ(eol_cp_pos_mv(2048), 6915);
    EXPECT_EQ(eol_cp_neg_mv(2048), 6875);
    EXPECT_EQ(eol_adc_mv(4095), 3300);
    EXPECT_EQ(eol_cp_pos_mv(4095), 13831);
    EXPECT_EQ(eol_cp_neg_mv(4095), 13750);
    for (uint16_t raw : {4096, 65534, 65535}) {
        EXPECT_EQ(eol_adc_mv(raw), -1);
        EXPECT_EQ(eol_cp_pos_mv(raw), -1);
        EXPECT_EQ(eol_cp_neg_mv(raw), -1);
        EXPECT_STREQ(eol_pp_interpret(raw), "invalid ADC");
        EXPECT_STREQ(eol_cp_interpret(raw, 3574, true, 5), "invalid ADC");
    }
}

TEST(EolState, CpStateAndFaultOrdering)
{
    EXPECT_STREQ(eol_cp_interpret(3574, 3574, true, 5), "A");
    EXPECT_STREQ(eol_cp_interpret(3574, 2680, true, 5), "B");
    EXPECT_STREQ(eol_cp_interpret(3574, 1787, true, 5), "C");
    EXPECT_STREQ(eol_cp_interpret(3574, 894, true, 5), "D");
    EXPECT_STREQ(eol_cp_interpret(0, 0, false, 5), "E");
    EXPECT_STREQ(eol_cp_interpret(0, 3574, false, 5), "invalid (malfunction)");
    EXPECT_STREQ(eol_cp_interpret(3574, 0, true, 0), "F");
    EXPECT_STREQ(eol_cp_interpret(0, 0, true, 0), "invalid (short circuit)");
    EXPECT_STREQ(eol_cp_interpret(0, 2680, true, 5), "invalid (diode fault)");
    EXPECT_STREQ(eol_cp_interpret(0, 2680, true, 100), "B");
    EXPECT_STREQ(eol_cp_interpret(3202, 3574, true, 5), "A"); // Diode threshold is inclusive.
    EXPECT_STREQ(eol_cp_interpret(3201, 3574, true, 5), "invalid (diode fault)");
    EXPECT_STREQ(eol_cp_interpret(3202, 0, true, 0), "invalid (malfunction)"); // F threshold is strict.
    EXPECT_STREQ(eol_cp_interpret(3203, 0, true, 0), "F");
    EXPECT_STREQ(eol_cp_interpret(3871, 0, true, 0), "F");
    EXPECT_STREQ(eol_cp_interpret(3872, 0, true, 0), "invalid (malfunction)");
    EXPECT_STREQ(eol_cp_interpret(372, 371, false, 0), "E");
    EXPECT_STREQ(eol_cp_interpret(373, 371, false, 0), "invalid (malfunction)");
}

TEST(EolState, CpVoltageBandBoundaries)
{
    struct Case { uint16_t raw; const char *state; };
    for (const Case &c : {Case{518, "invalid (malfunction)"}, {519, "D"},
             {1258, "D"}, {1259, "invalid (malfunction)"},
             {1407, "invalid (malfunction)"}, {1408, "C"},
             {2146, "C"}, {2147, "invalid (malfunction)"},
             {2295, "invalid (malfunction)"}, {2296, "B"},
             {3035, "B"}, {3036, "invalid (malfunction)"},
             {3182, "invalid (malfunction)"}, {3183, "A"},
             {3923, "A"}, {3924, "invalid (malfunction)"}})
        EXPECT_STREQ(eol_cp_interpret(3574, c.raw, true, 100), c.state) << c.raw;
}

TEST(EolState, PpStrictBoundariesAndGaps)
{
    struct Case { uint16_t raw; const char *state; };
    for (const Case &c : {Case{0, "invalid"}, {770, "invalid"}, {771, "63 A"},
             {1166, "63 A"}, {1167, "invalid"}, {1304, "invalid"}, {1305, "32 A"},
             {1873, "32 A"}, {1874, "invalid"}, {2160, "invalid"}, {2161, "20 A"},
             {2903, "20 A"}, {2904, "invalid"}, {3016, "invalid"}, {3017, "13 A"},
             {3462, "13 A"}, {3463, "invalid"}, {3661, "invalid"}, {3662, "no cable"}})
        EXPECT_STREQ(eol_pp_interpret(c.raw), c.state) << c.raw;
}

TEST(EolState, OutputEnableAndRememberedSetpoint)
{
    eol_state state;
    eol_state_init(&state);
    EXPECT_EQ(eol_output(&state), UINT64_C(0x0880280500000000));
    state.duty_cycle = 23;
    state.pwm_active = false;
    EXPECT_EQ(eol_output(&state), UINT64_C(0x0080280000000000));
    state.pwm_active = true;
    EXPECT_EQ(eol_output(&state), UINT64_C(0x0880281700000000));
}

std::string Dump(const eol_state &state)
{
    FILE *file = tmpfile();
    if (!file) return {};
    eol_dump(file, &state);
    rewind(file);
    std::string result;
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file))) result.append(buffer, count);
    fclose(file);
    return result;
}

TEST(EolState, SnapshotIncludesZeroMessagesMetadataAndSeparateTimestamps)
{
    eol_state state;
    eol_state_init(&state);
    EXPECT_NE(Dump(state).find("CP Negative Side: not received"), std::string::npos);
    eol_record(&state, COM_ANALOG_INPUT_02, UINT64_C(0x0df60df60bd00000));
    eol_record(&state, COM_DIGITAL_INPUT, 0);
    eol_record(&state, COM_GIT_HASH, 0);
    eol_record(&state, COM_CHIPINFO, 0);
    eol_record(&state, COM_FW_VERSION, UINT64_C(0x0005018104000300));
    eol_record(&state, COM_PARTNUMBER_1, UINT64_C(0x5237464132453141));
    eol_record(&state, COM_PARTNUMBER_2, UINT64_C(0x3930303030303030));
    eol_record(&state, COM_ERROR_MESSAGE, 0);
    eol_record(&state, COM_INQUIRY, 0); // COM=255 must not overflow timestamp storage.
    eol_record_sent(&state, eol_output(&state));
    const auto text = Dump(state);
    for (const char *value : {"-12.000 V", "12.071 V", "CP Interpretation: A",
            "DI_PT1_EN=0", "Git Hash: 0000000000000000", "MCU Version: 0",
            "Firmware Version: 0.5.1", "R7FA2E1A90000000", "Parameter Version: 3",
            "== Latest Error Message ==", "== Timestamps ==", "COM_DIGITAL_OUTPUT",
            "COM_INQUIRY", "== Available Commands =="})
        EXPECT_NE(text.find(value), std::string::npos) << value;
    EXPECT_TRUE(state.seen[COM_DIGITAL_INPUT]);
    EXPECT_EQ(state.frames[COM_DIGITAL_INPUT], 0);
    EXPECT_EQ(std::string(state.timestamps[COM_INQUIRY]).size(), 23u);
    state.duty_cycle = 0; // Interpretation continues to use last transmitted output.
    EXPECT_NE(Dump(state).find("CP Interpretation: A"), std::string::npos);
}
TEST(EolMeasurements, PtResistanceAndTemperatureWithoutCorrectionOrFiltering)
{
    for (const auto &pair : {std::pair<uint16_t, double>{2100, 0.0}, {2197, 25.0},
                             {1863, -60.0}, {2858, 200.0}}) {
        auto sample = eol_pt_evaluate(pair.first, 1000);
        ASSERT_TRUE(sample.resistance_valid);
        ASSERT_TRUE(sample.temperature_valid);
        EXPECT_NEAR(sample.resistance, pair.first - 1100.0, 1e-9);
        EXPECT_NEAR(sample.temperature, pair.second, 1e-9);
    }
    auto fractional = eol_pt_evaluate(1290, 600); // 1050 ohm, between 12 and 13 C.
    EXPECT_NEAR(fractional.temperature, 12.75, 1e-9);
    for (const auto &pair : {std::pair<uint16_t, uint16_t>{4096, 600}, {2100, 0},
                             {2100, 300}, {2100, 1001}, {1000, 1000}, {65535, 600}})
        EXPECT_FALSE(eol_pt_evaluate(pair.first, pair.second).resistance_valid);
    for (uint16_t voltage : {1862, 2859}) {
        auto sample = eol_pt_evaluate(voltage, 1000);
        EXPECT_TRUE(sample.resistance_valid);
        EXPECT_FALSE(sample.temperature_valid);
    }
}

uint64_t PtInput(unsigned group)
{
    return group == 0 ? (UINT64_C(1) << 46) | (UINT64_C(1) << 44) :
                        (UINT64_C(1) << 45) | (UINT64_C(1) << 50);
}

void PtBatch(eol_state &state, unsigned group, int64_t now)
{
    // Every channel's voltage represents the same 1000-ohm test resistance.
    eol_record_at(&state, COM_DIGITAL_INPUT, PtInput(group), now);
    eol_record_at(&state, COM_ANALOG_INPUT_01, UINT64_C(0x04ec04ec04ec04ec), now + 1);
    eol_record_at(&state, COM_ANALOG_INPUT_03, 600, now + 2);
    eol_record_at(&state, COM_ANALOG_INPUT_04, UINT64_C(600) << 48, now + 3);
}

TEST(EolMeasurements, AlternatesOnlyAfterConfirmedSettledCompleteBatch)
{
    eol_state state;
    eol_state_init(&state);
    eol_record_sent_at(&state, eol_output(&state), 0);
    PtBatch(state, 1, 5); // Wrong group must not confirm selection.
    EXPECT_EQ(state.pt_phase, EOL_PT_WAIT_CONFIRM);
    PtBatch(state, 0, 10);
    EXPECT_EQ(state.pt_phase, EOL_PT_SETTLE);
    PtBatch(state, 0, 129); // Only 119 ms since confirmation.
    EXPECT_FALSE(state.pt[0].resistance_valid);
    EXPECT_FALSE(eol_measure_tick(&state, 129));
    // Periodic output repeats do not restart the confirmation/timeout clocks.
    eol_record_sent_at(&state, eol_output(&state), 129);
    PtBatch(state, 0, 130);
    ASSERT_EQ(state.pt_phase, EOL_PT_DONE);
    EXPECT_TRUE(state.pt[0].temperature_valid);
    EXPECT_TRUE(state.pt[2].temperature_valid);
    EXPECT_FALSE(state.pt[1].resistance_valid);
    EXPECT_FALSE(state.pt[3].resistance_valid);
    EXPECT_TRUE(eol_measure_tick(&state, 134));
    EXPECT_EQ(state.pt_group, 1u);
    EXPECT_EQ(eol_output(&state), UINT64_C(0x0884100500000000));
    eol_record_sent_at(&state, eol_output(&state), 200);
    PtBatch(state, 1, 210);
    PtBatch(state, 1, 330);
    ASSERT_TRUE(state.pt[1].temperature_valid);
    ASSERT_TRUE(state.pt[3].temperature_valid);
    for (const auto &sample : state.pt) {
        EXPECT_DOUBLE_EQ(sample.resistance, 1000.0);
        EXPECT_DOUBLE_EQ(sample.temperature, 0.0);
    }
}

TEST(EolMeasurements, IncompleteOrMismatchedBatchesCannotMixSamples)
{
    eol_state state;
    eol_state_init(&state);
    eol_record_sent_at(&state, eol_output(&state), 0);
    PtBatch(state, 0, 10);
    eol_record_at(&state, COM_DIGITAL_INPUT, PtInput(0), 130);
    eol_record_at(&state, COM_ANALOG_INPUT_01, UINT64_C(0x04ec04ec04ec04ec), 131);
    // New DigitalInput means that the preceding partial batch must be discarded.
    eol_record_at(&state, COM_DIGITAL_INPUT, PtInput(0), 230);
    eol_record_at(&state, COM_ANALOG_INPUT_03, 600, 231);
    eol_record_at(&state, COM_ANALOG_INPUT_04, UINT64_C(600) << 48, 232);
    EXPECT_FALSE(state.pt[0].resistance_valid);
    // A mismatched readback requires a new confirmation and settling interval.
    PtBatch(state, 1, 240);
    PtBatch(state, 0, 250);
    PtBatch(state, 0, 369);
    EXPECT_FALSE(state.pt[0].resistance_valid);
    PtBatch(state, 0, 370);
    EXPECT_TRUE(state.pt[0].resistance_valid);
}

TEST(EolMeasurements, TimeoutsPauseAndResumeKeepGroupsAndClearPartialData)
{
    eol_state state;
    eol_state_init(&state);
    state.pt[0] = eol_pt_evaluate(2100, 1000);
    eol_record_sent_at(&state, eol_output(&state), 0);
    EXPECT_FALSE(eol_measure_tick(&state, 1499));
    EXPECT_TRUE(eol_measure_tick(&state, 1500));
    EXPECT_FALSE(state.pt[0].resistance_valid);
    EXPECT_EQ(state.pt_group, 1u);
    eol_record_sent_at(&state, eol_output(&state), 1600);
    PtBatch(state, 1, 1610);
    eol_set_auto_send(&state, false);
    uint64_t paused_output = eol_output(&state);
    PtBatch(state, 1, 1800);
    EXPECT_FALSE(eol_measure_tick(&state, 10000));
    eol_record_sent_at(&state, paused_output, 10000); // Manual c does not advance measurement.
    EXPECT_EQ(eol_output(&state), paused_output);
    eol_set_auto_send(&state, true);
    PtBatch(state, 1, 10010); // No new selection sent yet.
    EXPECT_EQ(state.pt_phase, EOL_PT_WAIT_SEND);
    eol_record_sent_at(&state, eol_output(&state), 10100);
    PtBatch(state, 1, 10110);
    PtBatch(state, 1, 10229);
    EXPECT_FALSE(state.pt[1].resistance_valid);
    PtBatch(state, 1, 10230);
    EXPECT_TRUE(state.pt[1].resistance_valid);
    eol_state_init(&state); // Ctrl-R initializes the measurement state too.
    EXPECT_EQ(state.pt_group, 0u);
    for (const auto &sample : state.pt) EXPECT_FALSE(sample.resistance_valid);
}

TEST(EolMeasurements, PauseRetainsPhysicalGroupWhenNextSelectionIsNotSentYet)
{
    eol_state state;
    eol_state_init(&state);
    eol_record_sent_at(&state, eol_output(&state), 0);
    PtBatch(state, 0, 10);
    PtBatch(state, 0, 130);
    ASSERT_TRUE(eol_measure_tick(&state, 134)); // Next group prepared, not transmitted.
    ASSERT_EQ(state.pt_group, 1u);
    eol_set_auto_send(&state, false);
    EXPECT_EQ(state.pt_group, 0u);
    EXPECT_EQ(eol_output(&state), state.last_output);
}

TEST(EolMeasurements, PpResistanceAndReadbackChanges)
{
    double resistance;
    ASSERT_TRUE(eol_pp_resistance(2660, &resistance));
    EXPECT_NEAR(resistance, 691.801, 0.001);
    ASSERT_TRUE(eol_pp_resistance(0, &resistance));
    EXPECT_EQ(resistance, 0);
    EXPECT_FALSE(eol_pp_resistance(4095, &resistance)); // Beyond the model's open-circuit voltage.
    EXPECT_FALSE(eol_pp_resistance(65535, &resistance));
    // Independently calculated ADC values for nominal PP test resistors.
    for (const auto &pair : {std::pair<uint16_t, double>{3218, 1500}, {2645, 680}, {1573, 220}, {915, 100}}) {
        ASSERT_TRUE(eol_pp_resistance(pair.first, &resistance));
        EXPECT_NEAR(resistance, pair.second, 2.0);
    }
    eol_state state;
    eol_state_init(&state);
    uint64_t analog = (UINT64_C(3574) << 48) | (UINT64_C(2680) << 32) | (UINT64_C(2660) << 16);
    for (unsigned iec = 0; iec < 2; ++iec) {
        int64_t start = iec * 500;
        uint64_t digital = (uint64_t)iec << 55;
        eol_record_at(&state, COM_DIGITAL_INPUT, digital, start);
        eol_record_at(&state, COM_ANALOG_INPUT_02, analog, start + 121);
        EXPECT_TRUE(state.pp_settling); // A fresh DigitalInput cycle is still required.
        EXPECT_NE(Dump(state).find("[settling]"), std::string::npos);
        eol_record_at(&state, COM_DIGITAL_INPUT, digital, start + 122);
        eol_record_at(&state, COM_ANALOG_INPUT_02, analog, start + 123);
        EXPECT_FALSE(state.pp_settling);
        const auto text = Dump(state);
        EXPECT_NE(text.find("692 Ω   [20 A]"), std::string::npos) << text;
    }
}

TEST(EolMeasurements, LayoutAlignsRawValuesUnitsAndConnectorAcrossRanges)
{
    eol_state state;
    eol_state_init(&state);
    eol_record_sent_at(&state, eol_output(&state), 0);
    eol_record_at(&state, COM_ANALOG_INPUT_01, UINT64_MAX, 1);
    auto sample = eol_pt_evaluate(2197, 1000);
    for (auto &pt : state.pt) pt = sample;
    state.pt[1] = eol_pt_evaluate(1900, 1000); // Three-digit resistance, negative temperature.
    auto text = Dump(state);
    EXPECT_EQ(text.find("== CP / PP =="), std::string::npos);
    EXPECT_NE(text.find("R=1097 Ω    T= 25.0 °C"), std::string::npos);
    EXPECT_NE(text.find("R= 800 Ω    T=-50.8 °C"), std::string::npos);
    // Even 16-bit diagnostic sentinel values leave >=3 spaces between raw fields.
    EXPECT_NE(text.find("AI_PT1000_LEAD1_1=65535"), std::string::npos);
    size_t old_connector = 0;
    for (uint16_t negative : {3574, 1600}) {
        eol_record_at(&state, COM_ANALOG_INPUT_02,
                      ((uint64_t)negative << 48) | (UINT64_C(2680) << 32), 2);
        text = Dump(state);
        size_t line = text.rfind('\n', text.find("/`-> CP Interpretation:")) + 1;
        size_t column = text.find("/`-> CP Interpretation:") - line;
        if (old_connector) EXPECT_EQ(column, old_connector);
        old_connector = column;
    }
}

} // namespace
