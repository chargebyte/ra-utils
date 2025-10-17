/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/time.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "uart.h"
#include "cb_uart.h"
#include "cb_protocol.h"
#include "logging.h"

#define BITMASK(len) \
    ((1 << (len)) - 1)

#define DATA_SET_BITS(dst, bit, len, datasource) \
    (dst) &= ~((uint64_t)BITMASK(len) << (bit)); \
    (dst) |= ((uint64_t)(datasource) & BITMASK(len)) << (bit)

#define DATA_GET_BITS(src, bit, len) \
    (((src) >> (bit)) & BITMASK(len))

int cb_send_uart_inquiry(struct uart_ctx *uart, uint8_t com)
{
    uint64_t data = 0;

    DATA_SET_BITS(data, 56, 8, com);

    return cb_uart_send(uart, COM_INQUIRY, data);
}

bool cb_proto_get_actual_pwm_active(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 63, 1);
}

bool cb_proto_get_target_pwm_active(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_control, 63, 1);
}

void cb_proto_set_pwm_active(struct safety_controller *ctx, bool active)
{
    DATA_SET_BITS(ctx->charge_control, 63, 1, active);
}

unsigned int cb_proto_get_actual_duty_cycle(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 48, 10);
}

unsigned int cb_proto_get_target_duty_cycle(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_control, 48, 10);
}

void cb_proto_set_duty_cycle(struct safety_controller *ctx, unsigned int duty_cycle)
{
    if (duty_cycle > 1000)
        duty_cycle = 1000;
    DATA_SET_BITS(ctx->charge_control, 48, 10, duty_cycle);
}

enum contactor_state cb_proto_contactorN_get_actual_state(struct safety_controller *ctx, unsigned int contactor)
{
    return DATA_GET_BITS(ctx->charge_state, 24 + (2 * contactor), 2);
}

bool cb_proto_contactorN_get_target_state(struct safety_controller *ctx, unsigned int contactor)
{
    return DATA_GET_BITS(ctx->charge_control, 40 + contactor, 1);
}

void cb_proto_contactorN_set_state(struct safety_controller *ctx, unsigned int contactor, bool active)
{
    DATA_SET_BITS(ctx->charge_control, 40 + contactor, 1, active);
}

bool cb_proto_contactorN_is_enabled(struct safety_controller *ctx, unsigned int contactor)
{
    return cb_proto_contactorN_get_actual_state(ctx, contactor) != CONTACTOR_STATE_UNUSED;
}

bool cb_proto_contactorN_is_closed(struct safety_controller *ctx, unsigned int contactor)
{
    return cb_proto_contactorN_get_actual_state(ctx, contactor) == CONTACTOR_STATE_CLOSED;
}

bool cb_proto_contactorN_has_error(struct safety_controller *ctx, unsigned int contactor)
{
    /* FIXME: currently returning global HW switch error flag */
    (void)contactor;
    return cb_proto_get_safestate_reason(ctx) == CS1_SAFESTATE_REASON_HV_SWITCH_MALFUNCTION;
}

bool cb_proto_contactors_have_errors(struct safety_controller *ctx)
{
    unsigned int i;

    for (i = 0; i < CB_PROTO_MAX_CONTACTORS; ++i) {
        if (cb_proto_contactorN_is_enabled(ctx, i) &&
            cb_proto_contactorN_has_error(ctx, i))
            return true;
    }

    return false;
}

bool cb_proto_get_hv_ready(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 30, 1);
}

enum cp_state cb_proto_get_cp_state(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 40, 3);
}

unsigned int cb_proto_get_cp_errors(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 43, 2);
}

bool cb_proto_is_cp_short_circuit(struct safety_controller *ctx)
{
    return cb_proto_get_cp_errors(ctx) & CP_SHORT_CIRCUIT;
}

bool cb_proto_is_diode_fault(struct safety_controller *ctx)
{
    return cb_proto_get_cp_errors(ctx) & CP_DIODE_FAULT;
}

enum pp_state cb_proto_get_pp_state(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 32, 3);
}

enum cs1_safestate_reason cb_proto_get_safestate_reason(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 8, 8);
}

enum estop_state cb_proto_estopN_get_state(struct safety_controller *ctx, unsigned int estop)
{
    return DATA_GET_BITS(ctx->charge_state, 16 + (2 * estop), 2);
}

bool cb_proto_estopN_is_enabled(struct safety_controller *ctx, unsigned int estop)
{
    return (cb_proto_estopN_get_state(ctx, estop) == ESTOP_STATE_NOT_TRIPPED) ||
           (cb_proto_estopN_get_state(ctx, estop) == ESTOP_STATE_TRIPPED);
}

bool cb_proto_estopN_is_tripped(struct safety_controller *ctx, unsigned int estop)
{
    return cb_proto_estopN_get_state(ctx, estop) == ESTOP_STATE_TRIPPED;
}

bool cb_proto_estop_has_any_tripped(struct safety_controller *ctx)
{
    unsigned int i;

    for (i = 0; i < CB_PROTO_MAX_ESTOPS; ++i) {
        if (cb_proto_estopN_is_enabled(ctx, i) &&
            cb_proto_estopN_is_tripped(ctx, i))
            return true;
    }

    return false;
}

bool cb_proto_pt1000_is_active(struct safety_controller *ctx, unsigned int channel)
{
    return DATA_GET_BITS(ctx->pt1000, 16 * (CB_PROTO_MAX_PT1000S - 1 - channel) + 2, 14) != PT1000_TEMPERATURE_UNUSED;
}

double cb_proto_pt1000_get_temp(struct safety_controller *ctx, unsigned int channel)
{
    /* we access the whole 16 bit so that we shift correctly */
    int16_t d = DATA_GET_BITS(ctx->pt1000, 16 * (CB_PROTO_MAX_PT1000S - 1 - channel), 16);

    return (double)(d >> 2) / 10.0;
}

unsigned int cb_proto_pt1000_get_errors(struct safety_controller *ctx, unsigned int channel)
{
    return DATA_GET_BITS(ctx->pt1000, 16 * (CB_PROTO_MAX_PT1000S - 1 - channel), 2);
}

bool cb_proto_pt1000_have_errors(struct safety_controller *ctx)
{
    uint64_t mask =    (uint64_t)BITMASK(2) <<  0
                     | (uint64_t)BITMASK(2) << 16
                     | (uint64_t)BITMASK(2) << 32
                     | (uint64_t)BITMASK(2) << 48;
    return ctx->pt1000 & mask;
}

enum cs2_id_state cb_proto_get_id_state(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 56, 4);
}

enum cs2_ce_state cb_proto_get_ce_state(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 60, 4);
}

enum cs2_estop_reason cb_proto_get_estop_reason(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 48, 8);
}

enum cs_safestate_active cb_proto_cs1_get_safe_state_active(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 58, 2);
}

enum cs_safestate_active cb_proto_cs2_get_safe_state_active(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_state, 46, 2);
}

enum cs_safestate_active cb_proto_get_safe_state_active(struct safety_controller *ctx)
{
    if (ctx->mcs)
        return cb_proto_cs2_get_safe_state_active(ctx);
    else
        return cb_proto_cs1_get_safe_state_active(ctx);
}

enum cc2_ccs_ready cb_proto_get_target_ccs_ready(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->charge_control, 60, 4);
}

void cb_proto_set_ccs_ready(struct safety_controller *ctx, bool ready)
{
    DATA_SET_BITS(ctx->charge_control, 60, 4, ready ? CC2_CCS_READY : CC2_CCS_NOT_READY);
}

void cb_proto_set_estop(struct safety_controller *ctx, bool estop)
{
    DATA_SET_BITS(ctx->charge_control, 60, 4, estop ? CC2_CCS_EMERGENCY_STOP : CC2_CCS_NOT_READY);
}

bool cb_proto_errmsg_is_active(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->error_message, 63, 1);
}

enum errmsg_module cb_proto_errmsg_get_module(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->error_message, 48, 15);
}

unsigned int cb_proto_errmsg_get_reason(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->error_message, 32, 16);
}

unsigned int cb_proto_errmsg_get_additional_data_1(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->error_message, 16, 16);
}

unsigned int cb_proto_errmsg_get_additional_data_2(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->error_message, 0, 16);
}

unsigned int cb_proto_fw_get_major(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->fw_version, 56, 8);
}

unsigned int cb_proto_fw_get_minor(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->fw_version, 48, 8);
}

unsigned int cb_proto_fw_get_build(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->fw_version, 40, 8);
}

enum fw_platform_type cb_proto_fw_get_platform_type(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->fw_version, 32, 8);
}

enum fw_application_type cb_proto_fw_get_application_type(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->fw_version, 24, 8);
}

unsigned int cb_proto_fw_get_param_version(struct safety_controller *ctx)
{
    return DATA_GET_BITS(ctx->fw_version, 8, 16);
}

void cb_proto_set_fw_version_str(struct safety_controller *ctx)
{
    snprintf(ctx->fw_version_str, sizeof(ctx->fw_version_str), "%u.%u.%u",
             cb_proto_fw_get_major(ctx),
             cb_proto_fw_get_minor(ctx),
             cb_proto_fw_get_build(ctx));
}

void cb_proto_set_git_hash_str(struct safety_controller *ctx)
{
    uint8_t *p = (uint8_t *)&ctx->git_hash + sizeof(ctx->git_hash) - 1;
    char *s = &ctx->git_hash_str[0];
    unsigned int i;

    for (i = 0; i < sizeof(ctx->git_hash); ++i) {
        sprintf(s, "%02" PRIx8, *p);
        p--;
        s += 2;
    }

    *s = '\0';
}

const char *cb_proto_cp_state_to_str(enum cp_state state)
{
    switch (state) {
    case CP_STATE_UNKNOWN:
        return "unknown";
    case CP_STATE_A:
        return "A";
    case CP_STATE_B:
        return "B";
    case CP_STATE_C:
        return "C";
    case CP_STATE_D:
        return "D";
    case CP_STATE_E:
        return "E";
    case CP_STATE_F:
        return "F";
    case CP_STATE_INVALID:
        return "invalid";
    default:
        return "undefined";
    }
}

const char *cb_proto_pp_state_to_str(enum pp_state state)
{
    switch (state) {
    case PP_STATE_NO_CABLE:
        return "no cable detected";
    case PP_STATE_13A:
        return "13 A";
    case PP_STATE_20A:
        return "20 A";
    case PP_STATE_32A:
        return "32 A";
    case PP_STATE_63_70A:
        return "63/70 A";
    case PP_STATE_TYPE1_CONNECTED:
        return "connected";
    case PP_STATE_TYPE1_CONNECTED_BUTTON_PRESSED:
        return "connected, button pressed";
    case PP_STATE_INVALID:
        return "invalid";
    default:
        return "undefined";
    }
}

const char *cb_proto_contactor_state_to_str(enum contactor_state state)
{
    switch (state) {
    case CONTACTOR_STATE_UNDEFINED:
        return "undefined";
    case CONTACTOR_STATE_OPEN:
        return "open";
    case CONTACTOR_STATE_CLOSED:
        return "CLOSED";
    case CONTACTOR_STATE_UNUSED:
        return "unused";
    default:
        return "invalid";
    }
}

const char *cb_proto_estop_state_to_str(enum estop_state state)
{
    switch (state) {
    case ESTOP_STATE_NOT_TRIPPED:
        return "not tripped";
    case ESTOP_STATE_TRIPPED:
        return "TRIPPED";
    case ESTOP_STATE_RESERVED:
        return "reserved";
    case ESTOP_STATE_UNUSED:
        return "unused";
    default:
        return "undefined";
    }
}

void cb_proto_set_mcs_mode(struct safety_controller *ctx, bool mcs)
{
    ctx->mcs = mcs;
}

bool cb_proto_is_mcs_mode(struct safety_controller *ctx)
{
    return ctx->mcs;
}

const char *cb_proto_id_state_to_str(enum cs2_id_state state)
{
    switch (state) {
    case CS2_ID_STATE_UNKNOWN:
        return "unknown";
    case CS2_ID_STATE_NOT_CONNECTED:
        return "not connected";
    case CS2_ID_STATE_CONNECTED:
        return "connected";
    case CS2_ID_STATE_INVALID:
        return "invalid";
    default:
        return "undefined";
    }
}

const char *cb_proto_ce_state_to_str(enum cs2_ce_state state)
{
    switch (state) {
    case CS2_CE_STATE_UNKNOWN:
        return "unknown";
    case CS2_CE_STATE_A:
        return "A";
    case CS2_CE_STATE_B0:
        return "B0";
    case CS2_CE_STATE_B:
        return "B";
    case CS2_CE_STATE_C:
        return "C";
    case CS2_CE_STATE_E:
        return "E";
    case CS2_CE_STATE_EC:
        return "EC";
    case CS2_CE_STATE_INVALID:
        return "invalid";
    default:
        return "undefined";
    }
}

const char *cb_proto_estop_reason_to_str(enum cs2_estop_reason reason)
{
    switch (reason) {
    case CS2_ESTOP_REASON_NO_STOP:
        return "no estop reason";
    case CS2_ESTOP_REASON_INTERNAL_ERROR:
        return "internal error";
    case CS2_ESTOP_REASON_COM_TIMEOUT:
        return "communication timeout";
    case CS2_ESTOP_REASON_TEMP1_MALFUNCTION:
        return "temperature 1 malfunction";
    case CS2_ESTOP_REASON_TEMP2_MALFUNCTION:
        return "temperature 2 malfunction";
    case CS2_ESTOP_REASON_TEMP3_MALFUNCTION:
        return "temperature 3 malfunction";
    case CS2_ESTOP_REASON_TEMP4_MALFUNCTION:
        return "temperature 4 malfunction";
    case CS2_ESTOP_REASON_TEMP1_OVERTEMP:
        return "temperature 1 over-temperature";
    case CS2_ESTOP_REASON_TEMP2_OVERTEMP:
        return "temperature 2 over-temperature";
    case CS2_ESTOP_REASON_TEMP3_OVERTEMP:
        return "temperature 3 over-temperature";
    case CS2_ESTOP_REASON_TEMP4_OVERTEMP:
        return "temperature 4 over-temperature";
    case CS2_ESTOP_REASON_ID_MALFUNCTION:
        return "ID malfunction";
    case CS2_ESTOP_REASON_CE_MALFUNCTION:
        return "CE malfunction";
    case CS2_ESTOP_REASON_HVREADY_MALFUNCTION:
        return "HV ready malfunction";
    case CS2_ESTOP_REASON_EMERGENCY_INPUT:
        return "emergency input";
    default:
        return "unknown";
    }
}

const char *cb_proto_safestate_reason_to_str(enum cs1_safestate_reason reason)
{
    switch (reason) {
    case CS1_SAFESTATE_REASON_NO_STOP:
        return "no safe state";
    case CS1_SAFESTATE_REASON_INTERNAL_ERROR:
        return "internal error";
    case CS1_SAFESTATE_REASON_COM_TIMEOUT:
        return "communication timeout";
    case CS1_SAFESTATE_REASON_TEMP1_MALFUNCTION:
        return "temperature 1 malfunction";
    case CS1_SAFESTATE_REASON_TEMP2_MALFUNCTION:
        return "temperature 2 malfunction";
    case CS1_SAFESTATE_REASON_TEMP3_MALFUNCTION:
        return "temperature 3 malfunction";
    case CS1_SAFESTATE_REASON_TEMP4_MALFUNCTION:
        return "temperature 4 malfunction";
    case CS1_SAFESTATE_REASON_TEMP1_OVERTEMP:
        return "temperature 1 over-temperature";
    case CS1_SAFESTATE_REASON_TEMP2_OVERTEMP:
        return "temperature 2 over-temperature";
    case CS1_SAFESTATE_REASON_TEMP3_OVERTEMP:
        return "temperature 3 over-temperature";
    case CS1_SAFESTATE_REASON_TEMP4_OVERTEMP:
        return "temperature 4 over-temperature";
    case CS1_SAFESTATE_REASON_PP_MALFUNCTION:
        return "Proximity Pilot error";
    case CS1_SAFESTATE_REASON_CP_MALFUNCTION:
        return "Control Pilot error";
    case CS1_SAFESTATE_REASON_CP_SHORT_CIRCUIT:
        return "Control Pilot short-circuit";
    case CS1_SAFESTATE_REASON_CP_DIODE_FAULT:
        return "Control Pilot diode not detected";
    case CS1_SAFESTATE_REASON_HV_SWITCH_MALFUNCTION:
        return "high-voltage switch malfunction";
    case CS1_SAFESTATE_REASON_EMERGENCY_INPUT_1:
        return "emergency input 1";
    case CS1_SAFESTATE_REASON_EMERGENCY_INPUT_2:
        return "emergency input 2";
    case CS1_SAFESTATE_REASON_EMERGENCY_INPUT_3:
        return "emergency input 3";
    default:
        return "unknown";
    }
}

const char *cb_proto_safe_state_active_to_str(enum cs_safestate_active state)
{
    switch (state) {
    case CS_SAFESTATE_ACTIVE_NORMAL:
        return "normal";
    case CS_SAFESTATE_ACTIVE_SAFESTATE:
        return "safe state";
    case CS_SAFESTATE_ACTIVE_SNA:
        return "SNA";
    default:
        return "undefined";
    }
}

const char *cb_proto_ccs_ready_to_str(enum cc2_ccs_ready state)
{
    switch (state) {
    case CC2_CCS_NOT_READY:
        return "not ready";
    case CC2_CCS_READY:
        return "ready";
    case CC2_CCS_EMERGENCY_STOP:
        return "emergency stop";
    default:
        return "undefined";
    }
}

static const char *errmsg_module_strings[ERRMSG_MODULE_MAX] = {
    "DEFAULT",
    "APP_TASK",
    "APP_COMM",
    "APP_SAFETY",
    "APP_CP_PP",
    "APP_TEMP",
    "APP_SYSTEM",
    "MW_ADC",
    "MW_I2C",
    "MW_PIN",
    "MW_PWM",
    "MW_UART",
    "MW_PARAM",
};

const char *cb_proto_errmsg_module_to_str(enum errmsg_module module)
{
    if (module >= ERRMSG_MODULE_MAX)
        return "unknown";

    return errmsg_module_strings[module];
}

#define DEFINE_REASON_STRINGS(module, ...) \
    static const char * const errmsg_reason_strings_##module[] = { __VA_ARGS__ NULL }

DEFINE_REASON_STRINGS(ERRMSG_MODULE_DEFAULT,
    "default",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_APP_TASK,
    "default",
    "task was not executed in time [task id, -]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_APP_COMM,
    "default",
    "safety message timeouted [message id, last timestamp]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_APP_SAFETY,
    "default",
    "safety state mismatch [active safety fault, inverted safety fault]",
    "CP safety fault [CP pos voltage, CP neg voltage]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_APP_CP_PP,
    "default",
    "[CP pos voltage, CP neg voltage]",
    "[PP voltage, -]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_APP_TEMP,
    "default",
    "short to battery [raw current, index]",
    "short to ground [raw current, index:4 | raw voltage:12]",
    "open load [raw current, index:4 | raw voltage:12]",
    "temperature over limit [raw temp, index]",
    "temperature under limit [raw temp, index]",
    "resistance too high [resistance/10000, index]",
    "resistance negative [abs(resistance), index]",
    "invalid evaluation state [state, -]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_APP_SYSTEM,
    "default",
    "watchdog error [watchdog state, -]",
    "application initial selftests failed [-, -]",
    "application CRC mismatch [calculated CRC, stored CRC]",
    "application initial ADC test error [-, -]",
    "CPU test error [-, -]",
    "RAM test error [-, -]",
    "clock test error [-, -]",
    "clock stop error [-, -]",
    "ROM test error [-, -]",
    "ADC test error [-, -]",
    "voltage test error [-, -]",
    "temperature error [-, -]",
    "other test failed [-, -]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_MW_ADC,
    "default",
    "ELC initialization failed [FSP error code, -]",
    "ADC initialization failed [FSP error code, -]",
    "ADC scan configuration failed [FSP error code, -]",
    "ELC enable failed [FSP error code, -]",
    "ADC scan start failed [FSP error code, -]",
    "GPT initialization failed [FSP error code, -]",
    "GPT start failed [FSP error code, -]",
    "ADC read failed [group, FSP error code]",
    "invalid parameter for adcif_get_value [value, average_size]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_MW_I2C,
    "default",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_MW_PIN,
    "default",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_MW_PWM,
    "default",
    "GPT initialization failed [FSP error code, -]",
    "GPT start failed [FSP error code, -]",
    "setting duty cycle failed [dutycycle, FSP error code]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_MW_UART,
    "default",
    "UART initialization failed [FSP error code, -]",
    "UART RX buffer overflow [packet type, buffer index]",
    "UART TX buffer overflow [packet type, buffer index]",
    "UART TX failed [packet type, FSP error code]",
    "no TX packet set [ -, -]",
);

DEFINE_REASON_STRINGS(ERRMSG_MODULE_MW_PARAM,
    "default",
    "parameter not found in memory, defaults will be used",
    "CRC mismatch, defaults will be used ",
    "index out of bounds [index, [1= temp, 2=hv connector, 3=emergency in]]",
);

static const char * const * const errmsg_reason_strings[ERRMSG_MODULE_MAX] = {
    [ERRMSG_MODULE_DEFAULT]     = errmsg_reason_strings_ERRMSG_MODULE_DEFAULT,
    [ERRMSG_MODULE_APP_TASK]    = errmsg_reason_strings_ERRMSG_MODULE_APP_TASK,
    [ERRMSG_MODULE_APP_COMM]    = errmsg_reason_strings_ERRMSG_MODULE_APP_COMM,
    [ERRMSG_MODULE_APP_SAFETY]  = errmsg_reason_strings_ERRMSG_MODULE_APP_SAFETY,
    [ERRMSG_MODULE_APP_CP_PP]   = errmsg_reason_strings_ERRMSG_MODULE_APP_CP_PP,
    [ERRMSG_MODULE_APP_TEMP]    = errmsg_reason_strings_ERRMSG_MODULE_APP_TEMP,
    [ERRMSG_MODULE_APP_SYSTEM]  = errmsg_reason_strings_ERRMSG_MODULE_APP_SYSTEM,
    [ERRMSG_MODULE_MW_ADC]      = errmsg_reason_strings_ERRMSG_MODULE_MW_ADC,
    [ERRMSG_MODULE_MW_I2C]      = errmsg_reason_strings_ERRMSG_MODULE_MW_I2C,
    [ERRMSG_MODULE_MW_PIN]      = errmsg_reason_strings_ERRMSG_MODULE_MW_PIN,
    [ERRMSG_MODULE_MW_PWM]      = errmsg_reason_strings_ERRMSG_MODULE_MW_PWM,
    [ERRMSG_MODULE_MW_UART]     = errmsg_reason_strings_ERRMSG_MODULE_MW_UART,
    [ERRMSG_MODULE_MW_PARAM]    = errmsg_reason_strings_ERRMSG_MODULE_MW_PARAM,
};

const char *cb_proto_errmsg_reason_to_str(enum errmsg_module module, unsigned int reason)
{
    if (module < ERRMSG_MODULE_MAX) {
        const char * const *module_reasons = errmsg_reason_strings[module];

        while (*module_reasons != NULL) {
            if (reason == 0)
                return *module_reasons;

            module_reasons++;
            reason--;
        }
    }

    return "unknown";
}

const char *cb_proto_fw_platform_type_to_str(enum fw_platform_type type)
{
    switch (type) {
    case FW_PLATFORM_TYPE_UNSPECIFIED:
        return "unspecified";
    case FW_PLATFORM_TYPE_UNKNOWN:
        return "unknown";
    case FW_PLATFORM_TYPE_CHARGESOM:
        return "Charge SOM";
    case FW_PLATFORM_TYPE_CCY:
        return "Charge Control Y";
    default:
        return "unknown value";
    }
}

const char *cb_proto_fw_application_type_to_str(enum fw_application_type type)
{
    switch (type) {
    case FW_APPLICATION_TYPE_FW:
        return "firmware";
    case FW_APPLICATION_TYPE_EOL:
        return "eol";
    case FW_APPLICATION_TYPE_QUALIFICATION:
        return "qualification";
    default:
        return "unknown";
    }
}

int cb_proto_set_ts_str(struct safety_controller *ctx, uint8_t com)
{
    char *buffer = ctx->ts_str_recv_com[com];
    struct timeval tv;
    struct tm tm;
    size_t offset;
    int rv;

    if (gettimeofday(&tv, NULL) < 0) {
         error("gettimeofday() failed: %m");
         return -1;
    }

    if (localtime_r(&tv.tv_sec, &tm) == NULL) {
        error("localtime_r() failed: %m");
        return -1;
    }

    offset = strftime(buffer, TS_STR_RECV_COM_BUFSIZE, "%Y-%m-%d %H:%M:%S", &tm);
    if (offset < 1) {
        error("strftime() failed");
        return -1;
    }

    rv = snprintf(&buffer[offset], TS_STR_RECV_COM_BUFSIZE - offset, ".%03ld", tv.tv_usec / 1000);
    if (rv < 0) {
        error("strftime() failed");
        return -1;
    }

    return rv;
}

#define printfnl(fmt, ...) \
        do { \
            printf(fmt "\r\n", ##__VA_ARGS__); \
        } while (0)

#define THIS_BIT_AND_ANY_OF_THE_LOWER(src, bit) \
    (((src) & (bit)) && ((src) & ((bit) - 1)))

void cb_proto_dump(struct safety_controller *ctx)
{
    unsigned int i;

    if (!ctx->mcs) {
        printfnl("== Various ==");
        printfnl("Control Pilot:   %s (%s%s%s%s)", cb_proto_cp_state_to_str(cb_proto_get_cp_state(ctx)),
                 cb_proto_get_cp_errors(ctx) ? "" : "-no flags set-",
                 (cb_proto_get_cp_errors(ctx) & CP_DIODE_FAULT) ? "diode fault" : "",
                 THIS_BIT_AND_ANY_OF_THE_LOWER(cb_proto_get_cp_errors(ctx), CP_DIODE_FAULT) ? "," : "",
                 (cb_proto_get_cp_errors(ctx) & CP_SHORT_CIRCUIT) ? "short circuit" : "");

        printfnl("Proximity Pilot: %s", cb_proto_pp_state_to_str(cb_proto_get_pp_state(ctx)));

        printf("Emergency Stop Tripped:");
        for (i = 0; i < CB_PROTO_MAX_ESTOPS; ++i) {
            printf(" ESTOP%d=%-11s ", i + 1, cb_proto_estop_state_to_str(cb_proto_estopN_get_state(ctx, i)));
        }
        printfnl("");

        printfnl("HV Ready: %u", cb_proto_get_hv_ready(ctx));
        printfnl("Safe State Active: %-11s Reason: %s",
                 cb_proto_safe_state_active_to_str(cb_proto_get_safe_state_active(ctx)),
                 cb_proto_safestate_reason_to_str(cb_proto_get_safestate_reason(ctx)));

        printfnl("");
        printfnl("== PWM ==");
        printfnl("Enable:               %-3s      Is Enabled:         %-3s",
                 cb_proto_get_target_pwm_active(ctx) ? "yes" : "no",
                 cb_proto_get_actual_pwm_active(ctx) ? "yes" : "no");
        printfnl("Requested Duty Cycle: %5.1f%%   Current Duty Cycle: %5.1f%%",
                 cb_proto_get_target_duty_cycle(ctx) / 10.0,
                 cb_proto_get_actual_duty_cycle(ctx) / 10.0);

        printfnl("");
        printfnl("== Contactor ==");
        for (i = 0; i < CB_PROTO_MAX_CONTACTORS; ++i) {
            printfnl("Contactor %d: requested=%-5s   actual=%-9s   %s", i + 1,
                     cb_proto_contactorN_get_target_state(ctx, i) ? "CLOSE" : "open",
                     cb_proto_contactor_state_to_str(cb_proto_contactorN_get_actual_state(ctx, i)),
                     cb_proto_contactorN_has_error(ctx, i) ? "ERROR" : "no error");
        }
    } else {
        printfnl("");
        printfnl("== MCS ==");
        printfnl("ID State: %s", cb_proto_id_state_to_str(cb_proto_get_id_state(ctx)));
        printfnl("CE State: %s", cb_proto_ce_state_to_str(cb_proto_get_ce_state(ctx)));
        printfnl("Safe State Active: %-11s Reason: %s",
                 cb_proto_safe_state_active_to_str(cb_proto_get_safe_state_active(ctx)),
                 cb_proto_estop_reason_to_str(cb_proto_get_estop_reason(ctx)));

        printfnl("");
        printfnl("CCS Ready: %-3s", cb_proto_ccs_ready_to_str(cb_proto_get_target_ccs_ready(ctx)));
    }

    printfnl("");
    printfnl("== Temperatures ==");
    for (i = 0; i < CB_PROTO_MAX_PT1000S; ++i) {
        bool is_enabled = cb_proto_pt1000_is_active(ctx, i);

        printf("Channel %d: enabled=%-3s temperature=", i + 1, is_enabled ? "yes" : "no");
        if (is_enabled)
            printf("%5.1f °C", cb_proto_pt1000_get_temp(ctx, i));
        else
            printf("-n/a- °C");
        printfnl(" (%s%s%s%s)",
                 cb_proto_pt1000_get_errors(ctx, i) ? "" : "-no flags set-",
                 (cb_proto_pt1000_get_errors(ctx, i) & PT1000_SELFTEST_FAILED) ? "selftest failed" : "",
                 THIS_BIT_AND_ANY_OF_THE_LOWER(cb_proto_pt1000_get_errors(ctx, i), PT1000_SELFTEST_FAILED) ? "," : "",
                 (cb_proto_pt1000_get_errors(ctx, i) & PT1000_CHARGING_STOPPED) ? "charging stop cause" : "");
    }

    printfnl("");
    printfnl("== Firmware Info ==");
    printfnl("Version: %s (%s, %s, Parameter Version: %u)",
             ctx->fw_version ? ctx->fw_version_str : "unknown",
             cb_proto_fw_platform_type_to_str(cb_proto_fw_get_platform_type(ctx)),
             cb_proto_fw_application_type_to_str(cb_proto_fw_get_application_type(ctx)),
             cb_proto_fw_get_param_version(ctx));
    printfnl("Git Hash: %s", ctx->git_hash ? ctx->git_hash_str : "unknown");

    printfnl("");
    printfnl("== Latest Error Message ==");
    if (ctx->error_message) {
        enum errmsg_module module = cb_proto_errmsg_get_module(ctx);
        unsigned int reason = cb_proto_errmsg_get_reason(ctx);

        printfnl("Active: %-8s Module: %-15s Reason: %s",
                 cb_proto_errmsg_is_active(ctx) ? "yes" : "no",
                 cb_proto_errmsg_module_to_str(module),
                 cb_proto_errmsg_reason_to_str(module, reason));
        printfnl("Additional Data: 0x%04x 0x%04x",
                 cb_proto_errmsg_get_additional_data_1(ctx),
                 cb_proto_errmsg_get_additional_data_2(ctx));
    } else {
        printfnl("None");
    }

    printfnl("");
    printfnl("== Timestamps ==");
    for (i = 0; i < COM_MAX; ++i) {
        if (strlen(ctx->ts_str_recv_com[i]))
            printfnl("%-29s: %s", cb_uart_com_to_str(i), ctx->ts_str_recv_com[i]);
    }
}
