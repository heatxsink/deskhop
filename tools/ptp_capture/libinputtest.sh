#!/usr/bin/env bash
# libinputtest.sh -- run libinput debug-events on the deskhop touchpad
# for 8 seconds, then libinput record on top to capture the kernel
# event stream as YAML (libinput's own diagnostic format).
#
# debug-events tells us what libinput INTERPRETS (motion, taps,
# scrolls, palm rejection, button events). If it shows nothing while
# evtest shows events, libinput is grabbing the device but discarding
# its events -- e.g. classifying every touch as a palm.
#
# Note: GNOME's compositor often has the device exclusively grabbed,
# in which case libinput debug-events on the same device will spin up
# its own context that gets *copies* of events via libudev's monitor.
#
# Usage: sudo ./libinputtest.sh
#
# Output: /tmp/deskhop_libinput.txt (debug-events log)
#         /tmp/deskhop_libinput.yml (record dump for offline analysis)

set -u

VID="1209"
PID="C000"
LOG="/tmp/deskhop_libinput.txt"
YAML="/tmp/deskhop_libinput.yml"

if [[ $EUID -ne 0 ]]; then
    echo "Re-run with sudo." >&2
    exit 1
fi

# Find touchpad event node
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
    echo "No hid-multitouch event node found for $VID:$PID." | tee "$LOG"
    exit 1
fi

echo "event node: $EV" | tee "$LOG"
echo "" | tee -a "$LOG"
echo "============================================="  | tee -a "$LOG"
echo " Phase 1: libinput debug-events --device $EV"   | tee -a "$LOG"
echo " SLIDE A FINGER ACROSS THE TRACKPAD NOW"        | tee -a "$LOG"
echo " (8 seconds)"                                   | tee -a "$LOG"
echo "============================================="  | tee -a "$LOG"
echo "" | tee -a "$LOG"

timeout 8 libinput debug-events --device "$EV" --show-keycodes 2>&1 | tee -a "$LOG" || true

echo "" | tee -a "$LOG"
echo "============================================="  | tee -a "$LOG"
echo " Phase 2: libinput record (YAML capture)"       | tee -a "$LOG"
echo " SLIDE A FINGER AGAIN"                          | tee -a "$LOG"
echo " (8 seconds)"                                   | tee -a "$LOG"
echo "============================================="  | tee -a "$LOG"
echo "" | tee -a "$LOG"

timeout 8 libinput record --output-file "$YAML" "$EV" 2>&1 | tee -a "$LOG" || true

echo "" | tee -a "$LOG"
echo "============================================="  | tee -a "$LOG"
echo " summary"                                       | tee -a "$LOG"
echo "============================================="  | tee -a "$LOG"
echo "  log:  $LOG" | tee -a "$LOG"
echo "  yaml: $YAML ($(wc -c < "$YAML" 2>/dev/null || echo 0) bytes)" | tee -a "$LOG"

# Count what libinput reported
MOTION=$(grep -c "POINTER_MOTION " "$LOG" 2>/dev/null || true)
MOTION=${MOTION:-0}
TAP=$(grep -c "GESTURE_\|TAP\|POINTER_BUTTON" "$LOG" 2>/dev/null || true)
TAP=${TAP:-0}
ADDED=$(grep -c "DEVICE_ADDED" "$LOG" 2>/dev/null || true)
ADDED=${ADDED:-0}
echo "  POINTER_MOTION lines:   $MOTION" | tee -a "$LOG"
echo "  TAP/BUTTON/GESTURE:     $TAP"    | tee -a "$LOG"
echo "  DEVICE_ADDED only:      $ADDED"  | tee -a "$LOG"
