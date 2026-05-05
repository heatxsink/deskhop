#!/usr/bin/env bash
# diag.sh -- one-shot diagnostic for the deskhop PTP path.
# Run on the HOST PC that deskhop is plugged into (the side whose
# cursor should move). Dumps:
#   1. every HID interface deskhop exposes, with its driver and the
#      raw report descriptor bytes
#   2. libinput's view of any deskhop device
#   3. recent kernel messages mentioning HID / input / multitouch
#
# No arguments. Re-run any time. Output goes to stdout; pipe to a file
# if you want to share it.
#
# Usage: sudo ./diag.sh
#
# Why sudo: the report_descriptor sysfs files are root-readable only.

set -u

VID="1209"
PID="C000"
OUT="/tmp/deskhop_diag.txt"

if [[ $EUID -ne 0 ]]; then
    echo "Re-run with sudo -- need to read /sys report_descriptor files." >&2
    exit 1
fi

# Mirror everything to $OUT so it can be inspected / shared after the run.
exec > >(tee "$OUT") 2>&1
echo "(writing to $OUT)"

echo "=========================================="
echo " deskhop PTP diagnostic"
echo " host:   $(hostnamectl --static 2>/dev/null || hostname)"
echo " kernel: $(uname -r)"
echo " date:   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "=========================================="

echo ""
echo "### 1. HID interfaces ($VID:$PID) ###"
shopt -s nullglob
matches=(/sys/bus/hid/devices/0003:${VID}:${PID}.*)
if (( ${#matches[@]} == 0 )); then
    echo "NO HID INTERFACES FOUND for $VID:$PID."
    echo "Is deskhop plugged in to this host? Try: lsusb | grep -i $VID"
    lsusb | grep -i "$VID" || true
else
    for iface in "${matches[@]}"; do
        echo ""
        echo "--- $iface ---"
        if [[ -f "$iface/uevent" ]]; then
            grep -E "HID_NAME|MODALIAS|HID_PHYS" "$iface/uevent" \
                | sed 's/^/  /'
        fi
        drv="$(readlink "$iface/driver" 2>/dev/null | sed 's|.*/||')"
        echo "  driver: ${drv:-<unbound>}"
        if [[ -f "$iface/report_descriptor" ]]; then
            sz="$(wc -c < "$iface/report_descriptor")"
            echo "  descriptor size: $sz bytes"
            echo "  descriptor bytes:"
            xxd "$iface/report_descriptor" | sed 's/^/    /'
        else
            echo "  (no report_descriptor file)"
        fi
        # Show input nodes this interface produced
        for inp in "$iface"/input/input*; do
            [[ -d "$inp" ]] || continue
            name="$(cat "$inp/name" 2>/dev/null)"
            echo "  input node: $(basename "$inp") -- name=\"$name\""
            for ev in "$inp"/event*; do
                [[ -d "$ev" ]] || continue
                echo "    -> /dev/input/$(basename "$ev")"
            done
        done
    done
fi

echo ""
echo "### 2. libinput's view ###"
if ! command -v libinput >/dev/null 2>&1; then
    echo "libinput CLI not installed. Install libinput-tools / libinput-utils."
else
    # Print every Device: ... block whose body mentions deskhop or matches
    # one of our event nodes.
    libinput list-devices 2>&1 | awk '
        /^Device:/ { if (block != "" && keep) print block "\n"; block = $0 "\n"; keep = 0; next }
        /^$/       { if (block != "" && keep) print block; block = ""; keep = 0; next }
                   { block = block $0 "\n" }
        /[Dd]esk[Hh]op|1209:[Cc]000/ { keep = 1 }
        END        { if (block != "" && keep) print block }
    '
fi

echo ""
echo "### 3. kernel messages (last 5 minutes) ###"
dmesg --since "5min ago" 2>/dev/null \
    | grep -iE "hid|input|multitouch|deskhop|1209" \
    | tail -40

echo ""
echo "=========================================="
echo " end of diagnostic"
echo "=========================================="
