/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cb_uart.h"

/* the MCU is responsive to UART messages after releasing reset line after this time, in ms */
#define CB_PROTO_STARTUP_DELAY 300

/* the MCU is expected to answer requests via inquiry messages within this time, in ms */
#define CB_PROTO_RESPONSE_TIMEOUT_MS 30

/* the MCU expects Charge Control messages with this periodicity, in ms */
#define CB_PROTO_CHARGE_CONTROL_INTERVAL 100

/* the MCU sends Charge State messages with this periodicity, in ms */
#define CB_PROTO_CHARGE_STATE_INTERVAL 100

/* maximum supported contactors */
#define CB_PROTO_MAX_CONTACTORS 2

/* maximum count of PT1000 channels */
#define CB_PROTO_MAX_PT1000S 4

/* maximum emergency stop channels */
#define CB_PROTO_MAX_ESTOPS 3

/* possible CP states */
enum cp_state {
    CP_STATE_UNKNOWN = 0x0,
    CP_STATE_A,
    CP_STATE_B,
    CP_STATE_C,
    CP_STATE_D,
    CP_STATE_E,
    CP_STATE_F,
    CP_STATE_INVALID,
    CP_STATE_MAX,
};

/* CP related bit flags */
#define CP_SHORT_CIRCUIT 0x1
#define CP_DIODE_FAULT 0x2

/* possible PP states */
enum pp_state {
    PP_STATE_NO_CABLE,
    PP_STATE_13A,
    PP_STATE_20A,
    PP_STATE_32A,
    PP_STATE_63_70A,
    PP_STATE_TYPE1_CONNECTED,
    PP_STATE_TYPE1_CONNECTED_BUTTON_PRESSED,
    PP_STATE_INVALID,
    PP_STATE_MAX,
};

/* possible contactor states */
enum contactor_state {
    CONTACTOR_STATE_OPEN = 0x0,
    CONTACTOR_STATE_CLOSED = 0x1,
    CONTACTOR_STATE_RESERVED = 0x2,
    CONTACTOR_STATE_UNUSED = 0x3,
    CONTACTOR_STATE_MAX,
};

/* possible estop states */
enum estop_state {
    ESTOP_STATE_NOT_TRIPPED = 0x0,
    ESTOP_STATE_TRIPPED = 0x1,
    ESTOP_STATE_RESERVED = 0x2,
    ESTOP_STATE_UNUSED = 0x3,
    ESTOP_STATE_MAX,
};

/* possible SafeStateActive states in Charge State 1/2 frames */
enum cs_safestate_active {
    CS_SAFESTATE_ACTIVE_NORMAL = 0x0,
    CS_SAFESTATE_ACTIVE_SAFESTATE,
    CS_SAFESTATE_ACTIVE_SNA = 0x3,
    CS_SAFESTATE_ACTIVE_MAX,
};

/* possible safe state reasons in Charge State 1 frame */
enum cs1_safestate_reason {
    CS1_SAFESTATE_REASON_NO_STOP = 0x0,
    CS1_SAFESTATE_REASON_INTERNAL_ERROR,
    CS1_SAFESTATE_REASON_COM_TIMEOUT,
    CS1_SAFESTATE_REASON_TEMP1_MALFUNCTION,
    CS1_SAFESTATE_REASON_TEMP2_MALFUNCTION,
    CS1_SAFESTATE_REASON_TEMP3_MALFUNCTION,
    CS1_SAFESTATE_REASON_TEMP4_MALFUNCTION,
    CS1_SAFESTATE_REASON_TEMP1_OVERTEMP,
    CS1_SAFESTATE_REASON_TEMP2_OVERTEMP,
    CS1_SAFESTATE_REASON_TEMP3_OVERTEMP,
    CS1_SAFESTATE_REASON_TEMP4_OVERTEMP,
    CS1_SAFESTATE_REASON_PP_MALFUNCTION,
    CS1_SAFESTATE_REASON_CP_MALFUNCTION,
    CS1_SAFESTATE_REASON_CP_SHORT_CIRCUIT,
    CS1_SAFESTATE_REASON_CP_DIODE_FAULT,
    CS1_SAFESTATE_REASON_HV_SWITCH_MALFUNCTION,
    CS1_SAFESTATE_REASON_EMERGENCY_INPUT_1,
    CS1_SAFESTATE_REASON_EMERGENCY_INPUT_2,
    CS1_SAFESTATE_REASON_EMERGENCY_INPUT_3,
    CS1_SAFESTATE_REASON_MAX,
};

/* possible CCS ready values in Charge Control 2 frame */
enum cc2_ccs_ready {
    CC2_CCS_NOT_READY = 0x0,
    CC2_CCS_READY,
    CC2_CCS_EMERGENCY_STOP,
    CC2_CCS_MAX,
};

/* possible CE states in Charge State 2 frame */
enum cs2_ce_state {
    CS2_CE_STATE_UNKNOWN = 0x0,
    CS2_CE_STATE_A,
    CS2_CE_STATE_B0,
    CS2_CE_STATE_B,
    CS2_CE_STATE_C,
    CS2_CE_STATE_E,
    CS2_CE_STATE_EC,
    CS2_CE_STATE_INVALID,
    CS2_CE_STATE_MAX,
};

/* possible ID states in Charge State 2 frame */
enum cs2_id_state {
    CS2_ID_STATE_UNKNOWN = 0x0,
    CS2_ID_STATE_NOT_CONNECTED,
    CS2_ID_STATE_CONNECTED,
    CS2_ID_STATE_INVALID,
    CS2_ID_STATE_MAX,
};

/* possible ESTOP reasons in Charge State 2 frame */
enum cs2_estop_reason {
    CS2_ESTOP_REASON_NO_STOP = 0x0,
    CS2_ESTOP_REASON_INTERNAL_ERROR,
    CS2_ESTOP_REASON_COM_TIMEOUT,
    CS2_ESTOP_REASON_TEMP1_MALFUNCTION,
    CS2_ESTOP_REASON_TEMP2_MALFUNCTION,
    CS2_ESTOP_REASON_TEMP3_MALFUNCTION,
    CS2_ESTOP_REASON_TEMP4_MALFUNCTION,
    CS2_ESTOP_REASON_TEMP1_OVERTEMP,
    CS2_ESTOP_REASON_TEMP2_OVERTEMP,
    CS2_ESTOP_REASON_TEMP3_OVERTEMP,
    CS2_ESTOP_REASON_TEMP4_OVERTEMP,
    CS2_ESTOP_REASON_ID_MALFUNCTION,
    CS2_ESTOP_REASON_CE_MALFUNCTION,
    CS2_ESTOP_REASON_HVREADY_MALFUNCTION,
    CS2_ESTOP_REASON_EMERGENCY_INPUT,
    CS2_ESTOP_REASON_MAX,
};

/* PT1000 related bit flags */
#define PT1000_CHARGING_STOPPED 0x1
#define PT1000_SELFTEST_FAILED 0x2

/* magic value to indicate that this channel is not used */
#define PT1000_TEMPERATURE_UNUSED 0x1fff

/* error message frame related stuff */
enum errmsg_module {
    ERRMSG_MODULE_DEFAULT = 0,
    ERRMSG_MODULE_APP_TASK,
    ERRMSG_MODULE_APP_COMM,
    ERRMSG_MODULE_APP_SAFETY,
    ERRMSG_MODULE_APP_CP_PP,
    ERRMSG_MODULE_APP_TEMP,
    ERRMSG_MODULE_APP_SYSTEM,
    ERRMSG_MODULE_MW_ADC,
    ERRMSG_MODULE_MW_I2C,
    ERRMSG_MODULE_MW_PIN,
    ERRMSG_MODULE_MW_PWM,
    ERRMSG_MODULE_MW_UART,
    ERRMSG_MODULE_MW_PARAM,
    ERRMSG_MODULE_MAX,
};

/* error message frame */
struct error_message {
    uint16_t active:1;
    uint16_t module:15;
    uint16_t reason;
    uint16_t additional_data_1;
    uint16_t additional_data_2;
} __attribute__((packed));

/* buffer size for timestamp */
#define TS_STR_RECV_COM_BUFSIZE 32

/* holds the current MCU state */
struct safety_controller {
    /* latest message to send */
    uint64_t charge_control;

    /* the latest received messages */
    uint64_t charge_state;
    uint64_t pt1000;
    uint64_t fw_version;
    uint64_t error_message;

    /* MCS mode */
    bool mcs;

    /* Git hash is special: usually handled as byte stream, so here stored
     * in wrong (host) byte order -> this is handled in `cb_proto_set_git_hash_str`
     */
    uint64_t git_hash;

    /* parsed fw version as string: major.minor.build;
     * maximum length: 3 * 3 chars + 2 dots + NUL = 12 byte; padded to 64bit => 16 byte
     */
    char fw_version_str[16];

    /* string representation of git hash;
     * 2 char a 8 byte + NUL = 17 byte; padded to 64 bit => 24 byte
     */
    char git_hash_str[24];

    /* plain text receive timestampes for each packet type */
    char ts_str_recv_com[COM_MAX][TS_STR_RECV_COM_BUFSIZE];
};

bool cb_proto_get_actual_pwm_active(struct safety_controller *ctx);
bool cb_proto_get_target_pwm_active(struct safety_controller *ctx);
void cb_proto_set_pwm_active(struct safety_controller *ctx, bool active);

unsigned int cb_proto_get_actual_duty_cycle(struct safety_controller *ctx);
unsigned int cb_proto_get_target_duty_cycle(struct safety_controller *ctx);
void cb_proto_set_duty_cycle(struct safety_controller *ctx, unsigned int duty_cycle);

enum contactor_state cb_proto_contactorN_get_actual_state(struct safety_controller *ctx, unsigned int contactor);
bool cb_proto_contactorN_get_target_state(struct safety_controller *ctx, unsigned int contactor);
void cb_proto_contactorN_set_state(struct safety_controller *ctx, unsigned int contactor, bool active);

bool cb_proto_contactorN_is_enabled(struct safety_controller *ctx, unsigned int contactor);
bool cb_proto_contactorN_is_closed(struct safety_controller *ctx, unsigned int contactor);
bool cb_proto_contactorN_has_error(struct safety_controller *ctx, unsigned int contactor);

bool cb_proto_contactors_have_errors(struct safety_controller *ctx);

bool cb_proto_get_hv_ready(struct safety_controller *ctx);

enum cp_state cb_proto_get_cp_state(struct safety_controller *ctx);
unsigned int cb_proto_get_cp_errors(struct safety_controller *ctx);
bool cb_proto_is_cp_short_circuit(struct safety_controller *ctx);
bool cb_proto_is_diode_fault(struct safety_controller *ctx);

enum pp_state cb_proto_get_pp_state(struct safety_controller *ctx);

enum cs1_safestate_reason cb_proto_get_safestate_reason(struct safety_controller *ctx);

enum estop_state cb_proto_estopN_get_state(struct safety_controller *ctx, unsigned int estop);
bool cb_proto_estopN_is_enabled(struct safety_controller *ctx, unsigned int estop);
bool cb_proto_estopN_is_tripped(struct safety_controller *ctx, unsigned int estop);
bool cb_proto_estop_has_any_tripped(struct safety_controller *ctx);

bool cb_proto_pt1000_is_active(struct safety_controller *ctx, unsigned int channel);
double cb_proto_pt1000_get_temp(struct safety_controller *ctx, unsigned int channel);
unsigned int cb_proto_pt1000_get_errors(struct safety_controller *ctx, unsigned int channel);
bool cb_proto_pt1000_have_errors(struct safety_controller *ctx);

enum cs_safestate_active cb_proto_get_safe_state_active(struct safety_controller *ctx);

/* MCS related */
void cb_proto_set_mcs_mode(struct safety_controller *ctx, bool mcs);
bool cb_proto_is_mcs_mode(struct safety_controller *ctx);

enum cs2_id_state cb_proto_get_id_state(struct safety_controller *ctx);
enum cs2_ce_state cb_proto_get_ce_state(struct safety_controller *ctx);
enum cs2_estop_reason cb_proto_get_estop_reason(struct safety_controller *ctx);

enum cc2_ccs_ready cb_proto_get_target_ccs_ready(struct safety_controller *ctx);
void cb_proto_set_ccs_ready(struct safety_controller *ctx, bool ready);
void cb_proto_set_estop(struct safety_controller *ctx, bool estop);

/* Error Message related (accesses the last received error message) */
bool cb_proto_errmsg_is_active(struct safety_controller *ctx);
enum errmsg_module cb_proto_errmsg_get_module(struct safety_controller *ctx);
unsigned int cb_proto_errmsg_get_reason(struct safety_controller *ctx);
unsigned int cb_proto_errmsg_get_additional_data_1(struct safety_controller *ctx);
unsigned int cb_proto_errmsg_get_additional_data_2(struct safety_controller *ctx);
const char *cb_proto_errmsg_module_to_str(enum errmsg_module module);
const char *cb_proto_errmsg_reason_to_str(enum errmsg_module module, unsigned int reason);

/* possible firmware platform types */
enum fw_platform_type {
    FW_PLATFORM_TYPE_UNSPECIFIED = 0xff,
    FW_PLATFORM_TYPE_UNKNOWN = 0x00,
    FW_PLATFORM_TYPE_CHARGESOM = 0x81,
    FW_PLATFORM_TYPE_CCY = 0x82,
};

enum fw_application_type {
    FW_APPLICATION_TYPE_FW = 0x3,
    FW_APPLICATION_TYPE_EOL = 0x4,
    FW_APPLICATION_TYPE_QUALIFICATION = 0x5,
};

unsigned int cb_proto_fw_get_major(struct safety_controller *ctx);
unsigned int cb_proto_fw_get_minor(struct safety_controller *ctx);
unsigned int cb_proto_fw_get_build(struct safety_controller *ctx);
enum fw_platform_type cb_proto_fw_get_platform_type(struct safety_controller *ctx);
enum fw_application_type cb_proto_fw_get_application_type(struct safety_controller *ctx);

void cb_proto_set_fw_version_str(struct safety_controller *ctx);
void cb_proto_set_git_hash_str(struct safety_controller *ctx);

/* helpers */
const char *cb_proto_cp_state_to_str(enum cp_state state);
const char *cb_proto_pp_state_to_str(enum pp_state state);
const char *cb_proto_contactor_state_to_str(enum contactor_state state);
const char *cb_proto_estop_state_to_str(enum estop_state state);

const char *cb_proto_safestate_reason_to_str(enum cs1_safestate_reason reason);

const char *cb_proto_safe_state_active_to_str(enum cs_safestate_active state);

const char *cb_proto_id_state_to_str(enum cs2_id_state state);
const char *cb_proto_ce_state_to_str(enum cs2_ce_state state);
const char *cb_proto_estop_reason_to_str(enum cs2_estop_reason reason);

const char *cb_proto_ccs_ready_to_str(enum cc2_ccs_ready state);

const char *cb_proto_fw_platform_type_to_str(enum fw_platform_type type);
const char *cb_proto_fw_application_type_to_str(enum fw_application_type type);

int cb_proto_set_ts_str(struct safety_controller *ctx, uint8_t com);

void cb_proto_dump(struct safety_controller *ctx);

/* low-level helpers */

/* forward declaration so that it is not necessary to include uart.h completely */
struct uart_ctx;

int cb_send_uart_inquiry(struct uart_ctx *uart, uint8_t com);

#ifdef __cplusplus
}
#endif
