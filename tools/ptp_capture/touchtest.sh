#!/usr/bin/env bash
# touchtest.sh -- 8-second evtest capture on the deskhop PTP node.
# Slide / tap during the capture window. Output goes to stdout AND
# to /tmp/deskhop_touchtest.txt.
#
# Usage: sudo ./touchtest.sh
#
# Auto-detects the touchpad event node by walking the deskhop HID
# devices and finding the one bound to hid-multitouch.

set -u

VID="1209"
PID="C000"
OUT="/tmp/deskhop_touchtest.txt"

if [[ $EUID -ne 0 ]]; then
    echo "Re-run with sudo." >&2
    exit 1
fi

exec > >(tee "$OUT") 2>&1
echo "(writing to $OUT)"

# Find the hid-multitouch interface
shopt -s nullglob
EV=""
for iface in /sys/bus/hid/devices/0003:${VID}:${PID}.*; do
    drv="$(readlink "$iface/driver" 2>/dev/null | sed 's|.*/||')"
    [[ "$drv" == "hid-multitouch" ]] || continue
    for ev in "$iface"/input/input*/event*; do
        [[ -d "$ev" ]] || continue
        EV="/dev/input/$(basename "$ev")"
        break 2
    done
done

if [[ -z "$EV" ]]; then
    echo "No hid-multitouch event node found for $VID:$PID."
    exit 1
fi

echo "event node: $EV"
echo ""
echo "============================================="
echo " SLIDE A FINGER ACROSS THE TRACKPAD NOW"
echo " (capturing 8 seconds)"
echo "============================================="
echo ""

timeout 8 evtest "$EV" 2>&1 || true

echo ""
echo "============================================="
echo " summary"
echo "============================================="

EVT_COUNT=$(awk '/^Event:/ {n++} END {print n+0}' "$OUT")
echo "evtest events captured: $EVT_COUNT"
if (( EVT_COUNT > 0 )); then
    echo ""
    echo "distinct event codes that fired:"
    grep '^Event:' "$OUT" \
        | grep -oE 'code [0-9]+ \([A-Z_0-9]+\)' \
        | sort | uniq -c | sort -rn \
        | sed 's/^/  /'
fi
