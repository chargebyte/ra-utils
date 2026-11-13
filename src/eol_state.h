/* Copyright © 2026 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <cb_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

struct eol_pt_sample {
    double resistance;
    double temperature;
    bool resistance_valid;
    bool temperature_valid;
};

enum eol_pt_phase {
    EOL_PT_WAIT_SEND,
    EOL_PT_WAIT_CONFIRM,
    EOL_PT_SETTLE,
    EOL_PT_COLLECT,
    EOL_PT_DONE
};

struct eol_state {
    struct safety_controller info;
    uint64_t frames[256];
    bool seen[256];
    char timestamps[256][TS_STR_RECV_COM_BUFSIZE];
    char sent_timestamp[TS_STR_RECV_COM_BUFSIZE];
    unsigned int duty_cycle;
    bool pwm_active;
    bool auto_send;
    bool sent;
    uint64_t last_output;

    struct eol_pt_sample pt[4];
    unsigned int pt_group;
    enum eol_pt_phase pt_phase;
    int64_t pt_deadline_ms;
    int64_t pt_confirmed_ms;
    unsigned int pt_parts;
    uint64_t pt_cycle[3];

    bool pp_known;
    bool pp_iec;
    bool pp_settling;
    bool pp_cycle_ready;
    int64_t pp_changed_ms;
};

void eol_state_init(struct eol_state *state);
void eol_record(struct eol_state *state, enum cb_uart_com com, uint64_t data);
uint64_t eol_output(const struct eol_state *state);
void eol_record_sent(struct eol_state *state, uint64_t data);
int eol_adc_mv(uint16_t raw);
int eol_cp_pos_mv(uint16_t raw);
int eol_cp_neg_mv(uint16_t raw);
const char *eol_cp_interpret(uint16_t neg, uint16_t pos, bool active, unsigned int duty);
const char *eol_pp_interpret(uint16_t raw);
/* Explicit monotonic timestamps also allow deterministic measurement tests. */
void eol_record_at(struct eol_state *state, enum cb_uart_com com, uint64_t data, int64_t now_ms);
void eol_record_sent_at(struct eol_state *state, uint64_t data, int64_t now_ms);
bool eol_measure_tick(struct eol_state *state, int64_t now_ms);
int64_t eol_monotonic_ms(void);
void eol_set_auto_send(struct eol_state *state, bool enabled);
struct eol_pt_sample eol_pt_evaluate(uint16_t voltage, uint16_t current);
bool eol_pp_resistance(uint16_t raw, double *resistance);
void eol_dump(FILE *out, const struct eol_state *state);

#ifdef __cplusplus
}
#endif
