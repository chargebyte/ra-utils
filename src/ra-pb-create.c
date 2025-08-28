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

    { "version",            no_argument,            0,      'V' },
    { "help",               no_argument,            0,      'h' },
    {} /* stop condition for iterator */
};

static const char *short_options = "i:o:Vh";

/* descriptions for the command line options */
static const char *long_options_descs[] = {
    "use the given filename as input file (default: stdin)",
    "use the given filename for output (default: stdout)",

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
yaml_token_t yaml_token;

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

int main(int argc, char *argv[])
{
    int rv = EXIT_FAILURE;
    enum {
       YPS_NONE,
       YPS_IN_KEY,
       YPS_IN_VALUE_SEQUENCE
    } yaml_parser_state = YPS_NONE;
    enum {
        PBS_NONE,
        PBS_PT1000S,
        PBS_CONTACTORS,
        PBS_ESTOPS,
    } param_block_state;
    unsigned int current_temperature_idx = 0;
    unsigned int current_contactor_idx = 0;
    unsigned int current_estop_idx = 0;
    bool parsing_done = false;

    /* handle command line options */
    parse_cli(argc, argv);

    if (!yaml_parser_initialize(&yaml_parser)) {
        fprintf(stderr, "Error while initializing YAML parser.\n");
        goto err_out;
    }

    yaml_parser_set_input_file(&yaml_parser, infile);

    pb_init(&param_block);

    while (!parsing_done) {
        if (!yaml_parser_scan(&yaml_parser, &yaml_token))
            break;

        switch (yaml_token.type) {
            case YAML_KEY_TOKEN:
                yaml_parser_state = YPS_IN_KEY;
                break;

            case YAML_VALUE_TOKEN:
                yaml_parser_state = YPS_IN_VALUE_SEQUENCE;
                break;

            case YAML_SCALAR_TOKEN:
                if (yaml_parser_state == YPS_IN_KEY) {
                    if (strcasecmp(yaml_token.data.scalar.value, "pt1000s") == 0)
                        param_block_state = PBS_PT1000S;
                    else if (strcasecmp(yaml_token.data.scalar.value, "contactors") == 0)
                        param_block_state = PBS_CONTACTORS;
                    else if (strcasecmp(yaml_token.data.scalar.value, "estops") == 0)
                        param_block_state = PBS_ESTOPS;
                    else
                        param_block_state = PBS_NONE;
                } else if (yaml_parser_state == YPS_IN_VALUE_SEQUENCE) {
                    switch (param_block_state) {
                    case PBS_PT1000S:
                        if (current_temperature_idx >= CB_PROTO_MAX_PT1000S) {
                            /* we only warn when more values than allowed are given */
                            fprintf(stderr, "Warning: ignoring surplus temperature value (#%u): %s\n",
                                    current_temperature_idx + 1, yaml_token.data.scalar.value);
                        } else {
                            int16_t temp;
                            if (str_to_temperature(yaml_token.data.scalar.value, &temp)) {
                                fprintf(stderr, "Error: Cannot convert '%s' to a temperature value. Unit (°C) missing or wrong whitespace?\n",
                                        yaml_token.data.scalar.value);
                                goto err_out;
                            }
                            param_block.temperature[current_temperature_idx] = temp;
                        }
                        current_temperature_idx++;
                        break;
                    case PBS_CONTACTORS:
                        if (current_contactor_idx >= CB_PROTO_MAX_CONTACTORS) {
                            /* we only warn when more values than allowed are given */
                            fprintf(stderr, "Warning: ignoring surplus contactor configuration (#%u): %s\n",
                                    current_contactor_idx + 1, yaml_token.data.scalar.value);
                        } else {
                            param_block.contactor[current_contactor_idx] =
                                str_to_contactor_type(yaml_token.data.scalar.value);
                            if (param_block.contactor[current_contactor_idx] == CONTACTOR_MAX) {
                                fprintf(stderr, "Error: Cannot convert '%s' to a contactor configuration.\n",
                                        yaml_token.data.scalar.value);
                                goto err_out;
                            }
                        }
                        current_contactor_idx++;
                        break;
                    case PBS_ESTOPS:
                        if (current_estop_idx >= CB_PROTO_MAX_ESTOPS) {
                            /* we only warn when more values than allowed are given */
                            fprintf(stderr, "Warning: ignoring surplus estop configuration (#%u): %s\n",
                                    current_estop_idx + 1, yaml_token.data.scalar.value);
                        } else {
                            param_block.estop[current_estop_idx] =
                                    str_to_emergeny_stop_type(yaml_token.data.scalar.value);
                            if (param_block.estop[current_estop_idx] == EMERGENY_STOP_MAX) {
                                fprintf(stderr, "Error: Cannot convert '%s' to a estop configuration.\n",
                                        yaml_token.data.scalar.value);
                                goto err_out;
                            }
                        }
                        current_estop_idx++;
                        break;
                    default:
                        break;
                    }
                }
                break;

            case YAML_STREAM_END_TOKEN:
                parsing_done = true;
                break;

            default:
                break;
        }

        yaml_token_delete(&yaml_token);
    }

    yaml_parser_delete(&yaml_parser);

    /* special case: no properties found at all, e.g. libyaml could not parse, e.g. due to wrong YAML file encoding */
    if (current_temperature_idx == 0 &&
        current_contactor_idx == 0 &&
        current_estop_idx == 00) {
        fprintf(stderr, "Error: no or wrong input data - YAML file is probably not UTF-8 encoded.\n");
        goto err_out;
    }
    /* check that we saw at least the expected count of parameters, warn otherwise */
    if (current_temperature_idx < CB_PROTO_MAX_PT1000S)
        fprintf(stderr, "Warning: only %d temperature value(s) set instead of expected %d.\n", current_temperature_idx, CB_PROTO_MAX_PT1000S);
    if (current_contactor_idx < CB_PROTO_MAX_CONTACTORS)
        fprintf(stderr, "Warning: only %d contactor configuration(s) set instead of expected %d.\n", current_contactor_idx, CB_PROTO_MAX_CONTACTORS);
    if (current_estop_idx < CB_PROTO_MAX_ESTOPS)
        fprintf(stderr, "Warning: only %d estop configuration(s) set instead of expected %d.\n", current_estop_idx, CB_PROTO_MAX_ESTOPS);

    pb_refresh_crc(&param_block);

    if (fwrite(&param_block, sizeof(param_block), 1, outfile) != 1) {
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
