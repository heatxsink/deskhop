#!/usr/bin/env bash
# capture.sh -- snapshot everything we can observe about a touchpad device,
# so we can diff one capture against another to find what's different
# between a working device and a non-working one.
#
# Usage: sudo ./capture.sh <name> <event_node>
#   <name>        -- a label for this capture, e.g. "apple_native" or
#                    "surface_cover" or "deskhop_ptp_v3"
#   <event_node>  -- the evdev node, e.g. /dev/input/event22
#
# Output goes into ./out/<name>/. Re-running with the same name overwrites.
#
# Captures:
#   00_meta.txt           -- date, kernel, libinput version
#   01_proc_devices.txt   -- /proc/bus/input/devices entry for the device
#   02_lsusb.txt          -- lsusb -v for the parent USB device
#   03_udev.txt           -- udevadm info --query=property
#   04_libinput.txt       -- libinput list-devices entry
#   05_descriptor.bin     -- raw HID report descriptor bytes from sysfs
#   06_descriptor.hex     -- hex dump of the descriptor
#   07_descriptor.txt     -- usbhid-dump parsed view (if available)
#   08_evtest_8s.txt      -- 8 seconds of evtest output (capture during touch)
#
# After capturing two devices, run compare.sh to diff them.

set -e

if [[ $EUID -ne 0 ]]; then
    echo "Re-run with sudo. The capture needs to read /sys hidraw and run evtest." >&2
    exit 1
fi

if [[ $# -lt 2 ]]; then
    echo "Usage: sudo $0 <name> <event_node>" >&2
    echo "Example: sudo $0 apple_native /dev/input/event23" >&2
    exit 1
fi

NAME="$1"
EV="$2"
OUT="$(dirname "$0")/out/$NAME"

if [[ ! -e "$EV" ]]; then
    echo "Event node $EV does not exist." >&2
    exit 1
fi

mkdir -p "$OUT"

# 00 -- metadata
{
    echo "name:           $NAME"
    echo "event_node:     $EV"
    echo "captured_at:    $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "kernel:         $(uname -r)"
    echo "libinput:       $(libinput --version 2>/dev/null || echo unknown)"
    echo "host:           $(hostnamectl --static 2>/dev/null || hostname)"
} > "$OUT/00_meta.txt"

# Find the input device's directory in /sys
EV_NAME="$(basename "$EV")"
INPUT_DIR="$(readlink -f "/sys/class/input/$EV_NAME/device" 2>/dev/null)"
HID_DIR="$(echo "$INPUT_DIR" | sed -E 's|/input/input[0-9]+$||')"
USB_IF_DIR="$(echo "$HID_DIR" | sed -E 's|/0003:[0-9A-F]+:[0-9A-F]+\.[0-9A-F]+$||')"
USB_DEV_DIR="$(echo "$USB_IF_DIR" | sed -E 's|:[0-9]+\.[0-9]+$||')"

# 01 -- proc input devices entry
EV_BASENAME="$(basename "$EV")"
awk -v dev="$EV_BASENAME" '
    BEGIN { keep = 0; buf = "" }
    /^$/ {
        if (keep && buf != "") print buf;
        keep = 0; buf = "";
        next;
    }
    {
        buf = buf $0 "\n";
        if ($0 ~ "Handlers=" && $0 ~ dev) keep = 1;
    }
    END { if (keep && buf != "") print buf; }
' /proc/bus/input/devices > "$OUT/01_proc_devices.txt"

# 02 -- lsusb verbose for the USB device
USB_BUSDEV="$(basename "$USB_DEV_DIR" | tr -d -)"
if [[ -f "$USB_DEV_DIR/idVendor" && -f "$USB_DEV_DIR/idProduct" ]]; then
    VID="$(cat "$USB_DEV_DIR/idVendor")"
    PID="$(cat "$USB_DEV_DIR/idProduct")"
    echo "VID:PID = $VID:$PID" > "$OUT/02_lsusb.txt"
    echo "" >> "$OUT/02_lsusb.txt"
    lsusb -v -d "$VID:$PID" >> "$OUT/02_lsusb.txt" 2>&1
fi

# 03 -- udev properties
udevadm info --query=property --name="$EV" > "$OUT/03_udev.txt" 2>&1

# 04 -- libinput's view
NAME_LINE="$(awk '/^N: / {print substr($0, 4)}' "$OUT/01_proc_devices.txt" | head -1 | tr -d '"' | sed -E 's/Name=//')"
if [[ -n "$NAME_LINE" ]]; then
    libinput list-devices 2>&1 | awk -v n="$NAME_LINE" -v ev="$EV" '
        /^Device:/ { capture = 0; block = "" }
        /^Kernel:/ {
            if ($0 ~ ev) capture = 1
        }
        { block = block $0 "\n" }
        /^$/ {
            if (capture) print block;
            capture = 0; block = ""
        }
        END { if (capture) print block }
    ' > "$OUT/04_libinput.txt"
fi

# 05/06/07 -- HID report descriptor
if [[ -f "$HID_DIR/report_descriptor" ]]; then
    cp "$HID_DIR/report_descriptor" "$OUT/05_descriptor.bin"
    xxd "$OUT/05_descriptor.bin" > "$OUT/06_descriptor.hex"
fi
if command -v usbhid-dump >/dev/null 2>&1 && [[ -n "$VID" && -n "$PID" ]]; then
    usbhid-dump -d "$VID:$PID" -e descriptor 2>&1 > "$OUT/07_descriptor.txt"
fi

# 08 -- evtest stream for 8 seconds (user touches during this window)
echo ""
echo ">>> SLIDE FINGER ACROSS TRACKPAD CONTINUOUSLY FOR 8 SECONDS <<<"
echo ""
( timeout 8 evtest "$EV" > "$OUT/08_evtest_8s.txt" 2>&1 ) || true

# Summarize what fired
EVT_COUNT="$(grep -c '^Event:' "$OUT/08_evtest_8s.txt" || echo 0)"
echo ""
echo "Captured to $OUT/"
echo "  evtest events: $EVT_COUNT"
if [[ "$EVT_COUNT" -gt 0 ]]; then
    echo "  Distinct event codes that fired:"
    grep '^Event:' "$OUT/08_evtest_8s.txt" \
        | grep -oE 'code [0-9]+ \([A-Z_]+\)' \
        | sort | uniq -c | sort -rn \
        | sed 's/^/    /'
fi
