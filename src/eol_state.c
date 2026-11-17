/* Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include "eol_state.h"
#include "tools.h"

#define EOL_SETTLE_MS 120
#define EOL_PT_TIMEOUT_MS 1500
#define ANALOG_COLUMNS { 2, 33, 64, 91 }

static const uint64_t pt_output_masks[] = {
    (UINT64_C(1) << 45) | (UINT64_C(1) << 43), /* channels 1 + 3 */
    (UINT64_C(1) << 44) | (UINT64_C(1) << 50)  /* channels 2 + 4 */
};
static const uint64_t pt_input_masks[] = {
    (UINT64_C(1) << 46) | (UINT64_C(1) << 44),
    (UINT64_C(1) << 45) | (UINT64_C(1) << 50)
};

int64_t eol_monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void eol_set_auto_send(struct eol_state *state, bool enabled)
{
    /* A pending next group may not have been transmitted yet. Pausing must
     * retain the physical selection, including for subsequent manual sends.
     */
    if (!enabled && state->sent) {
        uint64_t mask = pt_output_masks[0] | pt_output_masks[1];
        state->pt_group = (state->last_output & mask) == pt_output_masks[1] ? 1 : 0;
    }
    state->auto_send = enabled;
    state->pt_phase = EOL_PT_WAIT_SEND;
    state->pt_parts = 0;
}

bool eol_measure_tick(struct eol_state *state, int64_t now_ms)
{
    if (!state->auto_send || state->pt_phase == EOL_PT_WAIT_SEND)
        return false;
    if (state->pt_phase != EOL_PT_DONE && now_ms < state->pt_deadline_ms)
        return false;
    if (state->pt_phase != EOL_PT_DONE) {
        state->pt[state->pt_group] = (struct eol_pt_sample){ 0 };
        state->pt[state->pt_group + 2] = (struct eol_pt_sample){ 0 };
    }
    state->pt_group ^= 1;
    state->pt_phase = EOL_PT_WAIT_SEND;
    state->pt_parts = 0;
    return true;
}

static void record_measurement(struct eol_state *state, enum cb_uart_com com, uint64_t data, int64_t now_ms)
{
    if (com == COM_DIGITAL_INPUT) {
        bool iec = (data >> 55) & 1;
        if (!state->pp_known || state->pp_iec != iec) {
            state->pp_known = true;
            state->pp_iec = iec;
            state->pp_settling = true;
            state->pp_changed_ms = now_ms;
        }
        state->pp_cycle_ready = now_ms - state->pp_changed_ms >= EOL_SETTLE_MS;
    } else if (com == COM_ANALOG_INPUT_02 && state->pp_cycle_ready) {
        state->pp_settling = false;
        state->pp_cycle_ready = false;
    }

    if (!state->auto_send || state->pt_phase == EOL_PT_WAIT_SEND || state->pt_phase == EOL_PT_DONE)
        return;
    if (now_ms >= state->pt_deadline_ms)
        return; /* A late reply must not rescue an expired measurement. */
    if (com == COM_DIGITAL_INPUT) {
        uint64_t mask = pt_input_masks[0] | pt_input_masks[1];
        state->pt_parts = 0; /* A new cyclic batch replaces any incomplete one. */
        if ((data & mask) != pt_input_masks[state->pt_group]) {
            state->pt_phase = EOL_PT_WAIT_CONFIRM;
            return;
        }
        if (state->pt_phase == EOL_PT_WAIT_CONFIRM) {
            state->pt_confirmed_ms = now_ms;
            state->pt_phase = EOL_PT_SETTLE;
        } else if (now_ms - state->pt_confirmed_ms >= EOL_SETTLE_MS) {
            state->pt_phase = EOL_PT_COLLECT;
        }
        return;
    }
    if (state->pt_phase == EOL_PT_COLLECT) {
        unsigned int part;
        switch (com) {
        case COM_ANALOG_INPUT_01: part = 0; break;
        case COM_ANALOG_INPUT_03: part = 1; break;
        case COM_ANALOG_INPUT_04: part = 2; break;
        default: return;
        }
        state->pt_cycle[part] = data;
        state->pt_parts |= 1u << part;
        if (state->pt_parts == 7) {
            unsigned int group = state->pt_group;
            state->pt[group] = eol_pt_evaluate(state->pt_cycle[0] >> (48 - group * 16),
                                               state->pt_cycle[1] & 0xffff);
            state->pt[group + 2] = eol_pt_evaluate(state->pt_cycle[0] >> (16 - group * 16),
                                                   state->pt_cycle[2] >> 48);
            state->pt_phase = EOL_PT_DONE;
        }
    }
}

static void timestamp(char *text)
{
    struct timeval tv;
    struct tm tm;
    size_t offset;

    text[0] = '\0';
    if (gettimeofday(&tv, NULL) || !localtime_r(&tv.tv_sec, &tm))
        return;
    offset = strftime(text, TS_STR_RECV_COM_BUFSIZE, "%Y-%m-%d %H:%M:%S", &tm);
    if (offset)
        snprintf(text + offset, TS_STR_RECV_COM_BUFSIZE - offset, ".%03ld", tv.tv_usec / 1000);
}

void eol_state_init(struct eol_state *state)
{
    memset(state, 0, sizeof(*state));
    state->duty_cycle = 5;
    state->pwm_active = true;
    state->auto_send = true;
}

void eol_record(struct eol_state *state, enum cb_uart_com com, uint64_t data)
{
    eol_record_at(state, com, data, eol_monotonic_ms());
}

void eol_record_at(struct eol_state *state, enum cb_uart_com com, uint64_t data, int64_t now_ms)
{
    if ((unsigned int)com > 255)
        return;
    state->frames[com] = data;
    state->seen[com] = true;
    timestamp(state->timestamps[com]);
    record_measurement(state, com, data, now_ms);
    switch (com) {
    case COM_FW_VERSION:
        state->info.fw_version = data;
        cb_proto_set_fw_version_str(&state->info);
        break;
    case COM_GIT_HASH:
        state->info.git_hash = data;
        cb_proto_set_git_hash_str(&state->info);
        break;
    case COM_PARTNUMBER_1:
        state->info.partnumber1 = data;
        break;
    case COM_PARTNUMBER_2:
        state->info.partnumber2 = data;
        break;
    case COM_CHIPINFO:
        state->info.chipinfo = data;
        break;
    case COM_ERROR_MESSAGE:
        state->info.error_message = data;
        break;
    default:
        break;
    }
    if (state->seen[COM_PARTNUMBER_1] && state->seen[COM_PARTNUMBER_2])
        cb_proto_set_partnumber_str(&state->info);
}

uint64_t eol_output(const struct eol_state *state)
{
    /* CP_INVERT follows normal firmware PWM enable. PP uses IEC mode. */
    return pt_output_masks[state->pt_group] | (UINT64_C(1) << 55) |
           (state->pwm_active ? (UINT64_C(1) << 59) | ((uint64_t)state->duty_cycle << 32) : 0);
}

void eol_record_sent(struct eol_state *state, uint64_t data)
{
    eol_record_sent_at(state, data, eol_monotonic_ms());
}

void eol_record_sent_at(struct eol_state *state, uint64_t data, int64_t now_ms)
{
    state->last_output = data;
    state->sent = true;
    timestamp(state->sent_timestamp);
    if (state->auto_send && state->pt_phase == EOL_PT_WAIT_SEND &&
        (data & (pt_output_masks[0] | pt_output_masks[1])) == pt_output_masks[state->pt_group]) {
        state->pt_deadline_ms = now_ms + EOL_PT_TIMEOUT_MS;
        state->pt_phase = EOL_PT_WAIT_CONFIRM;
    }
}

/* Preserve the normal firmware's integer arithmetic, including intermediate truncation. */
int eol_adc_mv(uint16_t raw)
{
    return raw <= 4095 ? (int)((unsigned int)raw * 3300 / 4095) : -1;
}

int eol_cp_pos_mv(uint16_t raw)
{
    int mv = eol_adc_mv(raw);
    return mv < 0 ? -1 : mv * 197 / 47;
}

int eol_cp_neg_mv(uint16_t raw)
{
    int mv = eol_adc_mv(raw);
    return mv < 0 ? -1 : mv * 150 / 36; /* magnitude, as in firmware */
}

static const char *cp_positive_range(int mv)
{
    if (mv > 10750 && mv < 13250) return "A";
    if (mv > 7750 && mv < 10250) return "B";
    if (mv > 4750 && mv < 7250) return "C";
    if (mv > 1750 && mv < 4250) return "D";
    if (mv >= 0 && mv < 1250) return "E range";
    return "invalid";
}

const char *eol_cp_interpret(uint16_t neg, uint16_t pos, bool active, unsigned int duty)
{
    int n = eol_cp_neg_mv(neg), p = eol_cp_pos_mv(pos);
    if (n < 0 || p < 0) return "invalid ADC";
    if (!active)
        return p < 1250 && n < 1250 ? "E" : "invalid (malfunction)";
    if (p < 1750 && n < 1750) return "invalid (short circuit)";
    if (!duty)
        return n > 10750 && n < 13000 ? "F" : "invalid (malfunction)";
    if (duty < 100 && n < 10750) return "invalid (diode fault)";
    if (p > 10750 && p < 13250) return "A";
    if (p > 7750 && p < 10250) return "B";
    if (p > 4750 && p < 7250) return "C";
    if (p > 1750 && p < 4250) return "D";
    return "invalid (malfunction)";
}

const char *eol_pp_interpret(uint16_t raw)
{
    int mv = eol_adc_mv(raw);
    if (mv < 0) return "invalid ADC";
    if (mv > 2950) return "no cable";
    if (mv > 2430 && mv < 2790) return "13 A";
    if (mv > 1740 && mv < 2340) return "20 A";
    if (mv > 1050 && mv < 1510) return "32 A";
    if (mv > 620 && mv < 940) return "63 A";
    return "invalid";
}

/* Same resistance/temperature points as safety firmware temp_app.c, -60 .. 200 C. */
static const uint16_t pt_resistances[] =
{
     763u,  767u,  771u,  775u,  779u,  783u,  787u,  791u,  795u,  799u,  // -60 ... -51
     803u,  807u,  811u,  815u,  819u,  823u,  827u,  831u,  835u,  839u,  // -50 ... -41
     843u,  847u,  851u,  855u,  858u,  862u,  866u,  870u,  874u,  878u,  // -40 ... -31
     882u,  886u,  890u,  894u,  898u,  902u,  906u,  910u,  914u,  918u,  // -30 ... -21
     922u,  925u,  929u,  933u,  937u,  941u,  945u,  949u,  953u,  957u,  // -20 ... -11
     961u,  965u,  969u,  973u,  977u,  980u,  984u,  988u,  992u,  996u,  // -10 ...  -1
    1000u, 1004u, 1008u, 1012u, 1016u, 1020u, 1023u, 1027u, 1031u, 1035u,  //   0 ...   9
    1039u, 1043u, 1047u, 1051u, 1055u, 1059u, 1062u, 1066u, 1070u, 1074u,  //  10 ...  19
    1078u, 1082u, 1086u, 1090u, 1094u, 1097u, 1101u, 1105u, 1109u, 1113u,  //  20 ...  29
    1117u, 1121u, 1125u, 1128u, 1132u, 1136u, 1140u, 1144u, 1148u, 1152u,  //  30 ...  39
    1155u, 1159u, 1163u, 1167u, 1171u, 1175u, 1179u, 1182u, 1186u, 1190u,  //  40 ...  49
    1194u, 1198u, 1202u, 1206u, 1209u, 1213u, 1217u, 1221u, 1225u, 1229u,  //  50 ...  59
    1232u, 1236u, 1240u, 1244u, 1248u, 1252u, 1255u, 1259u, 1263u, 1267u,  //  60 ...  69
    1271u, 1275u, 1278u, 1282u, 1286u, 1290u, 1294u, 1298u, 1301u, 1305u,  //  70 ...  79
    1309u, 1313u, 1317u, 1320u, 1324u, 1328u, 1332u, 1336u, 1340u, 1343u,  //  80 ...  89
    1347u, 1351u, 1355u, 1359u, 1362u, 1366u, 1370u, 1374u, 1378u, 1381u,  //  90 ...  99
    1385u, 1389u, 1393u, 1396u, 1400u, 1404u, 1408u, 1412u, 1415u, 1419u,  // 100 ... 109
    1423u, 1427u, 1431u, 1434u, 1438u, 1442u, 1446u, 1449u, 1453u, 1457u,  // 110 ... 119
    1461u, 1465u, 1468u, 1472u, 1476u, 1480u, 1483u, 1487u, 1491u, 1495u,  // 120 ... 129
    1498u, 1502u, 1506u, 1510u, 1513u, 1517u, 1521u, 1525u, 1528u, 1532u,  // 130 ... 139
    1536u, 1540u, 1543u, 1547u, 1551u, 1555u, 1558u, 1562u, 1566u, 1570u,  // 140 ... 149
    1573u, 1577u, 1581u, 1584u, 1588u, 1592u, 1596u, 1599u, 1603u, 1607u,  // 150 ... 159
    1611u, 1614u, 1618u, 1622u, 1625u, 1629u, 1633u, 1637u, 1640u, 1644u,  // 160 ... 169
    1648u, 1651u, 1655u, 1659u, 1663u, 1666u, 1670u, 1674u, 1677u, 1681u,  // 170 ... 179
    1685u, 1688u, 1692u, 1696u, 1700u, 1703u, 1707u, 1711u, 1714u, 1718u,  // 180 ... 189
    1722u, 1725u, 1729u, 1733u, 1736u, 1740u, 1744u, 1747u, 1751u, 1755u,  // 190 ... 199
    1758u,                                                                 // 200
};

struct eol_pt_sample eol_pt_evaluate(uint16_t voltage, uint16_t current)
{
    struct eol_pt_sample sample = { 0 };
    size_t i, count = ARRAY_SIZE(pt_resistances);

    if (voltage > 4095 || current <= 300 || current > 1000)
        return sample;
    sample.resistance = 1000.0 * voltage / current - 1100.0;
    if (sample.resistance <= 0)
        return sample;
    sample.resistance_valid = true;
    if (sample.resistance < pt_resistances[0] || sample.resistance > pt_resistances[count - 1])
        return sample;
    for (i = 1; i < count; ++i) {
        if (sample.resistance <= pt_resistances[i]) {
            sample.temperature = -60.0 + (i - 1) +
                (sample.resistance - pt_resistances[i - 1]) / (pt_resistances[i] - pt_resistances[i - 1]);
            sample.temperature_valid = true;
            break;
        }
    }
    return sample;
}

bool eol_pp_resistance(uint16_t raw, double *resistance)
{
    double voltage, denominator;
    if (raw > 4095)
        return false;
    /* Exactly one internal OR external nominal 5 V / 330 ohm pull-up.
     * Include the load of the 27k + 47k divider; the OPV is a follower.
     */
    voltage = raw * 3.3 / 4095 * 74000 / 47000;
    denominator = (5 - voltage) * 74000 - 330 * voltage;
    if (denominator <= 0)
        return false;
    *resistance = voltage * 330 * 74000 / denominator;
    return isfinite(*resistance);
}

#define line(...) do { fprintf(out, __VA_ARGS__); fputs("\r\n", out); } while (0)

static void dump_cp_voltage(FILE *out, const char *name, int mv, bool negative,
                            const char *range, const char *suffix)
{
    char label[64];
    /* Longest possible range, brackets, and two spaces: keep the connector fixed. */
    const int width = sizeof("outside F/E ranges") - 1 + 4;
    fprintf(out, "  %-18s ", name);
    if (mv < 0) {
        line("%-*s%s", 11 + width, "invalid ADC", suffix);
        return;
    }
    snprintf(label, sizeof(label), "[%s]", range);
    line("%7.3f V  %-*s%s", (negative ? -mv : mv) / 1000.0, width, label, suffix);
}

static void dump_cp_pp(FILE *out, const struct eol_state *state)
{
    uint64_t data;
    uint16_t neg, pos, pp;
    int n, p, mv;
    double resistance;
    char interpretation[128], resistance_text[32];

    if (!state->seen[COM_ANALOG_INPUT_02]) {
        line("  CP Negative Side: not received");
        line("  CP Positive Side: not received");
        line("  PP: not received");
        return;
    }
    data = state->frames[COM_ANALOG_INPUT_02];
    neg = data >> 48;
    pos = data >> 32;
    pp = data >> 16;
    n = eol_cp_neg_mv(neg);
    p = eol_cp_pos_mv(pos);
    snprintf(interpretation, sizeof(interpretation), "/`-> CP Interpretation: %s", state->sent ?
             eol_cp_interpret(neg, pos, (state->last_output >> 59) & 1,
                              (state->last_output >> 32) & 255) : "not sent yet");
    dump_cp_voltage(out, "CP Negative Side:", n, true,
                    n > 10750 && n < 13000 ? "F range" : n >= 0 && n < 1250 ? "E range" : "outside F/E ranges", "\\");
    dump_cp_voltage(out, "CP Positive Side:", p, false, cp_positive_range(p), interpretation);
    mv = eol_adc_mv(pp);
    if (mv < 0) {
        line("  PP: invalid ADC   R≈   -- Ω   [invalid ADC]");
        return;
    }
    strcpy(resistance_text, "--");
    if (!state->pp_settling && eol_pp_resistance(pp, &resistance))
        snprintf(resistance_text, sizeof(resistance_text), "%.0f", resistance);
    line("  PP: %d.%03d V   R≈ %4s Ω   [%s]", mv / 1000, mv % 1000, resistance_text,
         state->pp_settling ? "settling" : eol_pp_interpret(pp));
}

static void dump_pt(FILE *out, const struct eol_state *state)
{
    const unsigned int columns[] = ANALOG_COLUMNS;
    unsigned int channel, position = 0;

    for (channel = 0; channel < 4; ++channel) {
        char resistance[16] = "--", temperature[16] = "--";
        if (state->pt[channel].resistance_valid && state->pt[channel].resistance < 9999.5)
            snprintf(resistance, sizeof(resistance), "%.0f", state->pt[channel].resistance);
        if (state->pt[channel].temperature_valid)
            snprintf(temperature, sizeof(temperature), "%.1f", state->pt[channel].temperature);
        fprintf(out, "%*sR=%4s Ω    T=%5s °C", (int)(columns[channel] - position), "", resistance, temperature);
        position = columns[channel] + 22; /* Terminal cells, not UTF-8 bytes. */
    }
    fputs("\r\n", out);
}

/* Fixed signal groups and column starts match the engineering overview. */
struct signal_layout {
    enum cb_uart_com com;
    unsigned int columns[5];
    const char *rows[5][5];
};

static const struct signal_layout signal_layouts[] = {
    { COM_DIGITAL_OUTPUT, { 2, 27, 49, 74 }, {
        { "DO_CP_INVERT", "DO_PT1000_CTRLEN1", "DO_SAFETY_MOTOR_OUT1", "DO_HV_SW1_Dutycycle" },
        { "DO_CP_Dutycycle", "DO_PT1000_CTRLEN2", "DO_SAFETY_MOTOR_OUT2", "DO_HV_SW2_Dutycycle" },
        { "DO_PP_SAE_IEC", "DO_PT1000_CTRLEN3", NULL, "DO_HV_SW3_Dutycycle" },
        { "DO_SAFETY_CP_STATE_C", "DO_PT1000_CTRLEN4", "DO_SAFETY_IMD_OD_EN" },
        { "DO_SAFETY_GPIO_EXP" }
    } },
    { COM_DIGITAL_INPUT, { 2, 27, 41, 65, 91 }, {
        { "DI_CP_INVERT", "DI_PT1_EN", "DI_SAFETY_K1_IN", "DI_SAFETY_MOTOR_OUT1", "DI_ESTOP1" },
        { "DI_PP_SAE_IEC", "DI_PT2_EN", "DI_SAFETY_K2_IN", "DI_SAFETY_MOTOR_OUT2", "DI_ESTOP2" },
        { "DI_EV_CP_EDGE", "DI_PT3_EN", "DI_SAFETY_IMD_OD_EN", "DI_SAFETY_MOTOR_FAULT", "DI_ESTOP3" },
        { "DI_SAFETY_CP_STATE_C", "DI_PT4_EN" },
        { "DI_SAFETY_GPIO_EXP" }
    } },
    { COM_ANALOG_INPUT_01, ANALOG_COLUMNS, {
        { "AI_PT1000_LEAD1_1", "AI_PT1000_LEAD1_2", "AI_PT1000_LEAD1_3", "AI_PT1000_LEAD1_4" }
    } },
    { COM_ANALOG_INPUT_02, ANALOG_COLUMNS, {
        { "AI_CP_BUFFERED_NEG_ADC", "AI_CP_BUFFERED_POS_ADC", "AI_PP_VALUE_ADC", "AI_SAFETY_MOTOR_CAPS_SENSE" }
    } },
    { COM_ANALOG_INPUT_03, ANALOG_COLUMNS, {
        { "AI_SAFETY_DEV1_ADC", "AI_SAFETY_DEV2_ADC", "AI_PT1000_CFB1_2", "AI_SAFETY_INLET_FB" }
    } },
    { COM_ANALOG_INPUT_04, { 2 }, {
        { "AI_PT1000_CFB3_4" }
    } }
};

static void dump_signals(FILE *out, const struct signal_layout *layout, bool available, uint64_t data)
{
    char decoded[2048], *fields[32], *field, *save;
    size_t count = 0, row, col, i;

    line("%s:", cb_uart_com_to_str(layout->com));
    if (!available) {
        line("  %s", layout->com == COM_DIGITAL_OUTPUT ? "not sent" : "not received");
        fputs("\r\n", out);
        return;
    }
    if (cb_proto_frame_to_str(decoded, sizeof(decoded), layout->com, data) < 0)
        return;
    field = strchr(decoded, ':');
    if (!field)
        return;
    field = strtok_r(field + 1, ";", &save);
    while (field && count < ARRAY_SIZE(fields)) {
        while (*field == ' ') ++field;
        fields[count++] = field;
        field = strtok_r(NULL, ";", &save);
    }
    for (row = 0; row < 5 && layout->rows[row][0]; ++row) {
        unsigned int position = 0;
        for (col = 0; col < 5; ++col) {
            const char *name = layout->rows[row][col];
            if (!name)
                continue;
            for (i = 0; i < count; ++i) {
                size_t length = strlen(name);
                if (strncmp(fields[i], name, length) || fields[i][length] != '=')
                    continue;
                int gap = (int)layout->columns[col] - (int)position;
                if (col && gap < 3)
                    gap = 3;
                fprintf(out, "%*s%s", gap, "", fields[i]);
                position += gap + strlen(fields[i]);
                break;
            }
        }
        fputs("\r\n", out);
    }
    fputs("\r\n", out);
}

void eol_dump(FILE *out, const struct eol_state *state)
{
    /* Existing metadata helpers take a mutable context but only inspect these fields. */
    struct safety_controller info = state->info;
    unsigned int com;
    char decoded[2048];

    line("== PWM ==");
    line("Requested: %3u%%, enabled=%-3s  automatic sending=%s", state->duty_cycle,
         state->pwm_active ? "yes" : "no", state->auto_send ? "on" : "off");
    if (state->sent)
        line("Last sent: %3u%%, CP_INVERT=%u, PP_SAE_IEC=%u", (unsigned int)(state->last_output >> 32) & 255,
             (unsigned int)(state->last_output >> 59) & 1, (unsigned int)(state->last_output >> 55) & 1);
    else
        line("Last sent: none");
    line("\r\n== Digital Signals / Analog Raw Values ==");
    for (com = 0; com < ARRAY_SIZE(signal_layouts); ++com) {
        const struct signal_layout *layout = &signal_layouts[com];
        bool output = layout->com == COM_DIGITAL_OUTPUT;
        dump_signals(out, layout, output ? state->sent : state->seen[layout->com],
                     output ? state->last_output : state->frames[layout->com]);
        if (layout->com == COM_ANALOG_INPUT_01) {
            dump_pt(out, state);
            fputs("\r\n", out);
        } else if (layout->com == COM_ANALOG_INPUT_02) {
            dump_cp_pp(out, state);
            fputs("\r\n", out);
        }
    }
    line("== Controller Info / Firmware Info ==");
    if (state->seen[COM_CHIPINFO]) line("MCU Version: %u", cb_proto_get_mcu_version(&info));
    else line("MCU Version: not received");
    if (state->seen[COM_PARTNUMBER_1] && state->seen[COM_PARTNUMBER_2])
        line("Part Number: %s %s", info.partnumber_str, info.partnumber_details);
    else line("Part Number: not received completely");
    if (state->seen[COM_FW_VERSION])
        line("Firmware Version: %s (%s, %s, Parameter Version: %u)", info.fw_version_str,
             cb_proto_fw_platform_type_to_str(cb_proto_fw_get_platform_type(&info)),
             cb_proto_fw_application_type_to_str(cb_proto_fw_get_application_type(&info)),
             cb_proto_fw_get_param_version(&info));
    else line("Firmware Version: not received");
    line("Git Hash: %s", state->seen[COM_GIT_HASH] ? info.git_hash_str : "not received");
    line("\r\n== Latest Error Message ==");
    if (state->seen[COM_ERROR_MESSAGE] &&
        cb_proto_frame_to_str(decoded, sizeof(decoded), COM_ERROR_MESSAGE, state->frames[COM_ERROR_MESSAGE]) >= 0)
        line("%s", decoded);
    else line("none");
    line("\r\n== Timestamps ==");
    for (com = 0; com < 256; ++com) {
        if (com == COM_DIGITAL_OUTPUT && state->sent)
            line("%-29s [0x%02x]: %s", cb_uart_com_to_str(com), com, state->sent_timestamp);
        else if (state->seen[com])
            line("%-29s [0x%02x]: %s", cb_uart_com_to_str(com), com, state->timestamps[com]);
    }
    line("\r\n== Available Commands ==");
    line("r/t/z: enable PWM at 5/10/100%%   0/5/6/9: set duty cycle to 0/5/10/100%%   +/-: duty +/-1%%");
    line("e/E: enable/disable PWM   s: toggle automatic sending   c: send Digital Output");
    line("Ctrl-R: reset and reinitialize   q / Ctrl-C: quit (leave last outputs unchanged)");
}
