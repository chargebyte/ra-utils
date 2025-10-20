/*
 * Copyright © 2025 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 *
 * Command line tool to create a binary parameter block file from a YAML file
 *
 * Usage: ra-pb-create [<options>]
 *
 * Options:
 *         -i, --infile            use the given filename as input file (default: stdin)
 *         -o, --outfile           use the given filename for output (default: stdout)
 *         -D, --debug             print debug output to stderr
 *         -V, --version           print version and exit
 *         -h, --help              print this usage and exit
 *
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <endian.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <yaml.h>
#include <version.h>
#include "param_block.h"

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "ra-pb-create (unknown version)"
#endif

/* command line options */
static const struct option long_options[] = {
    { "infile",             required_argument,      0,      'i' },
    { "outfile",            required_argument,      0,      'o' },

    { "debug",              no_argument,            0,      'D' },

    { "version",            no_argument,            0,      'V' },
    { "help",               no_argument,            0,      'h' },
    {} /* stop condition for iterator */
};

static const char *short_options = "i:o:DVh";

/* descriptions for the command line options */
static const char *long_options_descs[] = {
    "use the given filename as input file (default: stdin)",
    "use the given filename for output (default: stdout)",

    "print debug output to stderr",

    "print version and exit",
    "print this usage and exit",
    NULL /* stop condition for iterator */
};

static void usage(char *p, int exitcode)
{
    const char **desc = long_options_descs;
    const struct option *op = long_options;
    int max_cmd_len = 0;
    int i;

    fprintf(stderr,
            "%s (%s) -- Command line tool to create a binary parameter block file from a YAML file\n\n"
            "Usage: %s [<options>]\n\n"
            , p, PACKAGE_STRING, p);

    fprintf(stderr,
            "Options:\n");

    while (op->name && desc) {
        if (op->val > 1) {
            fprintf(stderr, "\t-%c, --%-12s\t%s\n", op->val, op->name, *desc);
        } else {
            fprintf(stderr, "\t    --%-12s\t%s\n", op->name, *desc);
        }
        op++;
        desc++;
    }

    fprintf(stderr, "\n");

    exit(exitcode);
}

/* to keep things easy, we use global variables here */
char *filename_in = "-";
char *filename_out = "-";
FILE *infile, *outfile;
struct param_block param_block;
yaml_parser_t yaml_parser;
yaml_event_t event;
bool debug;

void parse_cli(int argc, char *argv[])
{
    int rc = EXIT_FAILURE;
    int i;

    while (1) {
        int c = getopt_long(argc, argv, short_options, long_options, NULL);

        /* detect the end of the options */
        if (c == -1)
            break;

        switch (c) {
        case 'i':
            filename_in = optarg;
            break;
        case 'o':
            filename_out = optarg;
            break;

        case 'D':
            debug = true;
            break;

        case 'V':
            printf("%s (%s)\n", argv[0], PACKAGE_STRING);
            exit(EXIT_SUCCESS);
        case '?':
        case 'h':
            rc = EXIT_SUCCESS;
            usage(argv[0], rc);
            break;
        case 0:
            /* getopt_long() set a variable by reference */
            break;
        default:
            rc = EXIT_FAILURE;
            fprintf(stderr, "Unknown option '%c'.\n", (char) c);
            usage(argv[0], rc);
        }
    }

    argc -= optind;
    argv += optind;

    /* check whether additional command line arguments were given */
    if (argc > 0)
        usage(program_invocation_short_name, EXIT_FAILURE);

    if (strcmp(filename_in, "-") == 0) {
        infile = stdin;
    } else {
        infile = fopen(filename_in, "r");
        if (!infile) {
            fprintf(stderr, "Error: cannot open '%s' for reading: %m\n", filename_in);
            exit(EXIT_FAILURE);
        }
    }

    if (strcmp(filename_out, "-") == 0) {
        outfile = stdout;
    } else {
        outfile = fopen(filename_out, "wb");
        if (!outfile) {
            fprintf(stderr, "Error: cannot open '%s' for writing: %m\n", filename_out);
            exit(EXIT_FAILURE);
        }
    }
}

#define PRINT_EVENT(indent, type, start_end) \
        fprintf(stderr, "%*s%s %s\n", (indent) * 2, "", (type), (start_end))

int indent = 0;

void print_event_type(yaml_event_type_t type)
{
    switch (type) {
    case YAML_STREAM_START_EVENT:   PRINT_EVENT(indent++, "Stream", "start"); break;
    case YAML_STREAM_END_EVENT:     PRINT_EVENT(--indent, "Stream", "end"); break;
    case YAML_DOCUMENT_START_EVENT: PRINT_EVENT(indent++, "Document", "start"); break;
    case YAML_DOCUMENT_END_EVENT:   PRINT_EVENT(--indent, "Document", "end"); break;
    case YAML_MAPPING_START_EVENT:  PRINT_EVENT(indent++, "Mapping", "start"); break;
    case YAML_MAPPING_END_EVENT:    PRINT_EVENT(--indent, "Mapping", "end"); break;
    case YAML_SEQUENCE_START_EVENT: PRINT_EVENT(indent++, "Sequence", "start"); break;
    case YAML_SEQUENCE_END_EVENT:   PRINT_EVENT(--indent, "Sequence", "end"); break;
    case YAML_SCALAR_EVENT:         /* Handled below */ break;
    default: fprintf(stderr, "%*sOther event: %d\n", indent * 2, "", type); break;
    }
}

int main(int argc, char *argv[])
{
    int rv = EXIT_FAILURE;
    enum {
        PBS_NONE,
        PBS_VERSION,
        PBS_PT1000S, /* in the array */
        PBS_PT1000,  /* in a specific array item */
        PBS_PT1000_TEMP,
        PBS_PT1000_OFFSET,
        PBS_CONTACTORS, /* in the array */
        PBS_CONTACTOR, /* in a specific array item */
        PBS_CONTACTOR_TYPE,
        PBS_CONTACTOR_CLOSE_TIME,
        PBS_CONTACTOR_OPEN_TIME,
        PBS_ESTOPS,
    } param_block_state;
    int current_temperature_idx = -1;
    int current_contactor_idx = -1;
    int current_estop_idx = -1;
    bool parsing_done = false;
    uint16_t tmp_u16;
    int16_t tmp_i16;

    /* handle command line options */
    parse_cli(argc, argv);

    if (!yaml_parser_initialize(&yaml_parser)) {
        fprintf(stderr, "Error while initializing YAML parser.\n");
        goto err_out;
    }

    yaml_parser_set_input_file(&yaml_parser, infile);

    pb_init(&param_block);

    while (!parsing_done) {
        if (!yaml_parser_parse(&yaml_parser, &event)) {
            fprintf(stderr, "YAML parse error: %d\n", yaml_parser.error);
            break;
        }

        if (debug) {
            print_event_type(event.type);

            if (event.type == YAML_SCALAR_EVENT) {
                PRINT_EVENT(indent, "Scalar:", event.data.scalar.value);
            }
        }

        switch (event.type) {
        case YAML_SEQUENCE_END_EVENT:
            switch (param_block_state) {
            case PBS_PT1000S:
            case PBS_CONTACTORS:
            case PBS_ESTOPS:
                param_block_state = PBS_NONE;
                break;
            default:
                /* nothing */;
            }
            break;

        case YAML_MAPPING_START_EVENT:
            switch (param_block_state) {
            case PBS_PT1000S:
                param_block_state = PBS_PT1000;
                current_temperature_idx++;
                if (current_temperature_idx > CB_PROTO_MAX_PT1000S - 1) {
                    /* we only warn when more values than allowed are given */
                    fprintf(stderr, "Warning: ignoring surplus temperature value (#%u)\n",
                            current_temperature_idx + 1);
                }
                break;
            case PBS_CONTACTORS:
                param_block_state = PBS_CONTACTOR;
                current_contactor_idx++;
                if (current_contactor_idx > CB_PROTO_MAX_CONTACTORS - 1) {
                    /* we only warn when more values than allowed are given */
                    fprintf(stderr, "Warning: ignoring surplus contactor configuration (#%u)\n",
                            current_contactor_idx + 1);
                }
                break;

            default:
                /* nothing */;
            }
            break;

        case YAML_MAPPING_END_EVENT:
            switch (param_block_state) {
            case PBS_PT1000:
                param_block_state = PBS_PT1000S;
                break;
            case PBS_CONTACTOR:
                param_block_state = PBS_CONTACTORS;
                break;
            default:
                /* nothing */;
            }
            break;

        case YAML_SCALAR_EVENT:
            switch (param_block_state) {
            case PBS_NONE:
                if (strcasecmp(event.data.scalar.value, "version") == 0)
                    param_block_state = PBS_VERSION;
                else if (strcasecmp(event.data.scalar.value, "pt1000s") == 0)
                    param_block_state = PBS_PT1000S;
                else if (strcasecmp(event.data.scalar.value, "contactors") == 0)
                    param_block_state = PBS_CONTACTORS;
                else if (strcasecmp(event.data.scalar.value, "estops") == 0)
                    param_block_state = PBS_ESTOPS;
                break;
            case PBS_VERSION:
                param_block_state = PBS_NONE;
                if (str_to_version(event.data.scalar.value, &tmp_u16)) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a version value (allowed range: 1-%" PRIu16 ")\n",
                            event.data.scalar.value, UINT16_MAX);
                    goto err_out;
                }
                param_block.version = tmp_u16;
                break;
            case PBS_PT1000:
                if (strcasecmp(event.data.scalar.value, "abort-temperature") == 0)
                    param_block_state = PBS_PT1000_TEMP;
                else if (strcasecmp(event.data.scalar.value, "resistance-offset") == 0)
                    param_block_state = PBS_PT1000_OFFSET;
                break;
            case PBS_PT1000S:
                current_temperature_idx++;
                if (current_temperature_idx > CB_PROTO_MAX_PT1000S - 1) {
                    /* we only warn when more values than allowed are given */
                    fprintf(stderr, "Warning: ignoring surplus temperature value (#%u): %s\n",
                            current_temperature_idx + 1, event.data.scalar.value);
                    break;
                }
                if (str_to_temperature(event.data.scalar.value, &tmp_i16)) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a temperature value. Unit (°C) missing or wrong whitespace?\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                param_block.temperature[current_temperature_idx] = tmp_i16;
                break;
            case PBS_PT1000_TEMP:
                param_block_state = PBS_PT1000;
                if (current_temperature_idx > CB_PROTO_MAX_PT1000S - 1)
                    break;
                if (str_to_temperature(event.data.scalar.value, &tmp_i16)) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a temperature value. Unit (°C) missing or wrong whitespace?\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                param_block.temperature[current_temperature_idx] = tmp_i16;
                break;
            case PBS_PT1000_OFFSET:
                param_block_state = PBS_PT1000;
                if (current_temperature_idx > CB_PROTO_MAX_PT1000S - 1)
                    break;
                if (str_to_resistance_offset(event.data.scalar.value, &tmp_i16)) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a temperature resistance offset. Unit (Ω) missing or wrong whitespace?\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                param_block.temperature_resistance_offset[current_temperature_idx] = tmp_i16;
                break;
            case PBS_CONTACTOR:
                if (strcasecmp(event.data.scalar.value, "type") == 0)
                    param_block_state = PBS_CONTACTOR_TYPE;
                else if (strcasecmp(event.data.scalar.value, "close-time") == 0)
                    param_block_state = PBS_CONTACTOR_CLOSE_TIME;
                else if (strcasecmp(event.data.scalar.value, "open-time") == 0)
                    param_block_state = PBS_CONTACTOR_OPEN_TIME;
                break;
            case PBS_CONTACTORS:
                current_contactor_idx++;
                if (current_contactor_idx > CB_PROTO_MAX_CONTACTORS - 1) {
                    /* we only warn when more values than allowed are given */
                    fprintf(stderr, "Warning: ignoring surplus contactor configuration (#%u): %s\n",
                            current_contactor_idx + 1, event.data.scalar.value);
                    break;
                }
                param_block.contactor_type[current_contactor_idx] =
                    str_to_contactor_type(event.data.scalar.value);
                if (param_block.contactor_type[current_contactor_idx] == CONTACTOR_MAX) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a contactor configuration.\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                break;
            case PBS_CONTACTOR_TYPE:
                param_block_state = PBS_CONTACTOR;
                if (current_contactor_idx > CB_PROTO_MAX_CONTACTORS - 1)
                    break;
                param_block.contactor_type[current_contactor_idx] =
                    str_to_contactor_type(event.data.scalar.value);
                if (param_block.contactor_type[current_contactor_idx] == CONTACTOR_MAX) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a contactor type configuration.\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                break;
            case PBS_CONTACTOR_CLOSE_TIME:
                param_block_state = PBS_CONTACTOR;
                if (current_contactor_idx > CB_PROTO_MAX_CONTACTORS - 1)
                    break;
                if (str_to_contactor_time(event.data.scalar.value, &param_block.contactor_close_time[current_contactor_idx])) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a valid contactor close time. Unit (ms) missing or wrong whitespace?\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                break;
            case PBS_CONTACTOR_OPEN_TIME:
                param_block_state = PBS_CONTACTOR;
                if (current_contactor_idx > CB_PROTO_MAX_CONTACTORS - 1)
                    break;
                if (str_to_contactor_time(event.data.scalar.value, &param_block.contactor_open_time[current_contactor_idx])) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a valid contactor open time. Unit (ms) missing or wrong whitespace?\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                break;
            case PBS_ESTOPS:
                current_estop_idx++;
                if (current_estop_idx > CB_PROTO_MAX_ESTOPS - 1) {
                    /* we only warn when more values than allowed are given */
                    fprintf(stderr, "Warning: ignoring surplus estop configuration (#%u): %s\n",
                            current_estop_idx + 1, event.data.scalar.value);
                    break;
                }
                param_block.estop[current_estop_idx] =
                    str_to_emergeny_stop_type(event.data.scalar.value);
                if (param_block.estop[current_estop_idx] == EMERGENY_STOP_MAX) {
                    fprintf(stderr, "Error: Cannot convert '%s' to a estop configuration.\n",
                            event.data.scalar.value);
                    goto err_out;
                }
                break;
            }
            break;

        case YAML_STREAM_END_EVENT:
            parsing_done = true;
            break;

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&yaml_parser);

    /* special case: no properties found at all, e.g. libyaml could not parse, e.g. due to wrong YAML file encoding */
    if (current_temperature_idx == -1 &&
        current_contactor_idx == -1 &&
        current_estop_idx == 0) {
        fprintf(stderr, "Error: no or wrong input data - YAML file is probably not UTF-8 encoded.\n");
        goto err_out;
    }
    /* check that we saw at least the expected count of parameters, warn otherwise */
    if (current_temperature_idx < CB_PROTO_MAX_PT1000S - 1)
        fprintf(stderr, "Warning: only %d temperature value(s) set instead of expected %d.\n", current_temperature_idx + 1, CB_PROTO_MAX_PT1000S);
    if (current_contactor_idx < CB_PROTO_MAX_CONTACTORS - 1)
        fprintf(stderr, "Warning: only %d contactor configuration(s) set instead of expected %d.\n", current_contactor_idx + 1, CB_PROTO_MAX_CONTACTORS);
    if (current_estop_idx < CB_PROTO_MAX_ESTOPS - 1)
        fprintf(stderr, "Warning: only %d estop configuration(s) set instead of expected %d.\n", current_estop_idx + 1, CB_PROTO_MAX_ESTOPS);

    if (pb_write(&param_block, outfile)) {
        fprintf(stderr, "Error while writing to '%s': %m\n", filename_out);
        goto err_out;
    } else {
        if (fclose(outfile) == EOF) {
            fprintf(stderr, "Error while closing '%s': %m\n", filename_out);
            goto err_out;
        }
        outfile = NULL;
        rv = EXIT_SUCCESS;
    }

err_out:
    /* close (if not yet done) but do not look at result */
    if (infile)
        fclose(infile);
    if (outfile)
        fclose(outfile);

    return rv;
}

