#!/usr/bin/env bash
# hidrawtest.sh -- read raw HID input reports from the deskhop touchpad
# interface for 8 seconds, bypassing hid-multitouch's parsing.
#
# Tells us whether the device is sending reports at all. If reports
# show up here but evtest sees nothing, hid-multitouch is rejecting
# them at parse time. If nothing shows up here, the device-side isn't
# emitting reports.
#
# Usage: sudo ./hidrawtest.sh
#
# Output to /tmp/deskhop_hidraw.txt and stdout.

set -u

VID="1209"
PID="C000"
OUT="/tmp/deskhop_hidraw.txt"

if [[ $EUID -ne 0 ]]; then
    echo "Re-run with sudo." >&2
    exit 1
fi

exec > >(tee "$OUT") 2>&1
echo "(writing to $OUT)"

# Find the hidraw node bound to the hid-multitouch interface
shopt -s nullglob
HIDRAW=""
for iface in /sys/bus/hid/devices/0003:${VID}:${PID}.*; do
    drv="$(readlink "$iface/driver" 2>/dev/null | sed 's|.*/||')"
    [[ "$drv" == "hid-multitouch" ]] || continue
    for hr in "$iface"/hidraw/hidraw*; do
        [[ -d "$hr" ]] || continue
        HIDRAW="/dev/$(basename "$hr")"
        break 2
    done
done

if [[ -z "$HIDRAW" ]]; then
    echo "No hidraw node found on hid-multitouch interface for $VID:$PID."
    exit 1
fi

echo "hidraw node: $HIDRAW"
echo ""
echo "============================================="
echo " SLIDE A FINGER ACROSS THE TRACKPAD NOW"
echo " (capturing 8 seconds of raw reports)"
echo "============================================="
echo ""

# xxd -c 32 will hex-dump each chunk of incoming report data on its
# own line; combined with timeout, we get an 8-second snapshot.
# We use unbuffer-friendly read via dd to flush after each report.
RAW="/tmp/deskhop_hidraw.bin"
rm -f "$RAW"
( timeout 8 cat "$HIDRAW" > "$RAW" 2>/dev/null ) || true

SZ="$(wc -c < "$RAW")"
echo "raw bytes captured: $SZ"
if (( SZ > 0 )); then
    echo ""
    echo "first 512 bytes (hex):"
    xxd -c 32 "$RAW" | head -16
fi

echo ""
echo "============================================="
echo " summary"
echo "============================================="
if (( SZ == 0 )); then
    echo "NO RAW REPORTS RECEIVED."
    echo "Device-side is not sending PTP reports."
else
    # Each input report on the wire is reportSize+0 bytes (no leading
    # report ID byte from hidraw -- that's stripped). Our payload is
    # 27 bytes; if reports show up they'll come in 27-byte chunks.
    # Actually hidraw DOES include the report ID byte as byte 0, so
    # expect 28-byte chunks for our PTP_REPORT_ID_TOUCH (0x10).
    echo "RAW REPORTS RECEIVED ($SZ bytes)."
    echo "If $SZ is a multiple of 28, that's $((SZ / 28)) reports."
    echo "Means: hid-multitouch is binding but rejecting reports at parse time."
fi
