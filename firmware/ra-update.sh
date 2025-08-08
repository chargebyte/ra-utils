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

PARAM_FILE="$(ls -1 $LIBDIR/*_parameter-block_only-contactor.bin 2>/dev/null)"

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

# when upgrading from 0.1.0 we need to install a parameter block for the first time;
# later we assume that a valid parameter block is already installed and we don't touch it
if [ "$CURRENT_VERSION_ONLY" = "0.1.0" ]; then
    echo -n "Updating Parameter Block..."
    ra-update -a data flash "$PARAM_FILE"
    echo "done."
fi

echo -n "Updating Firmware..."
ra-update flash "$FW_FILE"
rv=$?
echo "done."

exit "$rv"
