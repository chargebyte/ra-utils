#!/bin/sh
#
# This script checks whether the Safety Controller needs an update.
# For this, it checks the current flashed firmware information and compares
# it with the data from the firmware bundle in the rootfs.
# If this differs, then the firmware is updated.
#

LIBDIR="/usr/share/ra-utils"

FW_FILE="$(ls -1 $LIBDIR/*_fw_*.bin 2>/dev/null)"
if [ -z "$FW_FILE" ]; then
    echo "No firmware file found." >&2
    exit 1
fi

PARAM_FILE="$(ls -1 $LIBDIR/*_parameter-block_only-contactor.yaml 2>/dev/null)"

TARGET_VERSION="$(ra-update fw-info "$FW_FILE" 2>/dev/null)"
CURRENT_VERSION="$(ra-update fw-info)"

CMP_TARGET_VERSION="$(echo "$TARGET_VERSION" | tail -n +3 | head -n 6)"
CMP_CURRENT_VERSION="$(echo "$CURRENT_VERSION" | tail -n +3 | head -n 6)"

# nothing to do when versions are equal
[ "$CMP_TARGET_VERSION" = "$CMP_CURRENT_VERSION" ] && exit 0

cat <<EOF
== Safety Controller Firmware Update required ==

<<< Current Firmware <<<
$CMP_CURRENT_VERSION
<<<

>>> New Firmware >>>
$CMP_TARGET_VERSION
>>>

================================================
EOF

CURRENT_VERSION_ONLY="$(echo "$CMP_CURRENT_VERSION" | grep "Version" | awk '{print $3}')"

version_cmp() {
    local v1="$1"
    local v2="$2"
    local smaller_version="$(printf "%s\n%s" "$v1" "$v2" | sort -V | head -n1)"

    if [ "$smaller_version" = "$v1" ] && [ "$v1" != "$v2" ]; then
        echo "-1"
    elif [ "$v1" = "$v2" ]; then
        echo "0"
    else
        echo "1"
    fi
}

# lesser than
cur_version_lt() {
    [ "$(version_cmp "$CURRENT_VERSION_ONLY" "$1")" = "-1" ]
}

# lesser than or equal
cur_version_le() {
    local rv="$(version_cmp "$CURRENT_VERSION_ONLY" "$1")"
    [ "$rv" = "-1" ] || [ "$rv" = "0" ]
}

# greater than
cur_version_gt() {
    [ "$(version_cmp "$CURRENT_VERSION_ONLY" "$1")" = "1" ]
}

# equal
cur_version_eq() {
    [ "$CURRENT_VERSION_ONLY" = "$1" ]
}

# when upgrading from 0.1.0 we need to install a parameter block for the first time;
# later we assume that a valid parameter block is already installed and we don't touch it
if cur_version_eq "0.1.0"; then
    pbfile_target_bin=$(mktemp)

    ra-pb-create -i "$PARAM_FILE" -o "$pbfile_target_bin"

    echo -n "Installing Parameter Block..."
    ra-update -a data flash "$pbfile_target_bin"

    rm -f "$pbfile_target_bin"
    echo "done."
fi

# when upgrading from 0.2.2 or before we need to update the parameter block since
# the magic value for disabled PT1000 channels changed; the check for >= 0.1.0
# is just a short-cut since in this case, we already updated with the install above
if cur_version_gt "0.1.0" && cur_version_le "0.2.2"; then
    echo "Updating Parameter Block..."

    pbfile_current_bin=$(mktemp)
    pbfile_current_yaml=$(mktemp)
    pbfile_target_bin=$(mktemp)

    # dump current parameter block to YAML file
    ra-update -a data dump "$pbfile_current_bin"
    ra-pb-dump "$pbfile_current_bin" > "$pbfile_current_yaml"

    # dump it indented to stdout (for debug purpose only)
    echo "Current parameters:"
    cat "$pbfile_current_yaml" | sed 's/^/    /'

    # re-create parameter block - while creating YAML, the old magic values was already handled
    # so we can just re-create the new binary file using the dumped YAML file
    ra-pb-create -i "$pbfile_current_yaml" -o "$pbfile_target_bin"

    # finally flash it
    ra-update -a data flash "$pbfile_target_bin"

    rm -f "$pbfile_current_bin" "$pbfile_current_yaml" "$pbfile_target_bin"
    echo "done."
fi

# firmware < 0.2.6 had no versioned parameter file, ra-pb-dump can handle this
# but we need to write it back
if cur_version_lt "0.2.6"; then
    echo "Updating Parameter Block..."

    pbfile_current_bin=$(mktemp)
    pbfile_current_yaml=$(mktemp)
    pbfile_target_bin=$(mktemp)

    # dump current parameter block to YAML file
    ra-update -a data dump "$pbfile_current_bin"
    ra-pb-dump "$pbfile_current_bin" > "$pbfile_current_yaml"

    # dump it indented to stdout (for debug purpose only)
    echo "Current parameters:"
    cat "$pbfile_current_yaml" | sed 's/^/    /'

    # re-create parameter block
    ra-pb-create -i "$pbfile_current_yaml" -o "$pbfile_target_bin"

    # finally flash it
    ra-update -a data flash "$pbfile_target_bin"

    rm -f "$pbfile_current_bin" "$pbfile_current_yaml" "$pbfile_target_bin"
    echo "done."
fi

echo -n "Updating Firmware..."
ra-update flash "$FW_FILE"
rv=$?
echo "done."

exit "$rv"
