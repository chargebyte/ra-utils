#!/bin/sh
#
# Dump the current Safety Controller Parameter Block as YAML file.
#

set -eu

: "${SAFETY_MCU_UART:?SAFETY_MCU_UART must be set}"

interface="${SAFETY_MCU_UART#/dev/}"
if [ -z "$interface" ] || [ "$interface" != "${interface##*/}" ]; then
    echo "Invalid Safety Controller interface: $SAFETY_MCU_UART" >&2
    exit 1
fi

output_dir=/run/ra-utils
output_file="$output_dir/$interface.yaml"

mkdir -p "$output_dir"

current_bin=
yaml_file=

cleanup()
{
    [ -z "$current_bin" ] || rm -f "$current_bin"
    [ -z "$yaml_file" ] || rm -f "$yaml_file"
}
trap cleanup EXIT
trap 'cleanup; exit 1' HUP INT TERM

current_bin=$(mktemp "$output_dir/.$interface.parameter-block.XXXXXX")
yaml_file=$(mktemp "$output_dir/.$interface.yaml.XXXXXX")

ra-update -a data dump "$current_bin"
ra-pb-dump "$current_bin" > "$yaml_file"
mv -f "$yaml_file" "$output_file"

# Retain the YAML file in /run for the remainder of the runtime, but remove
# the temporary binary dump after the YAML conversion.
rm -f "$current_bin"
current_bin=

# The EXIT trap is no longer needed after both temporary files are handled.
trap - EXIT HUP INT TERM
