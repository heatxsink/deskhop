#!/usr/bin/env bash
# cdctest.sh -- capture deskhop's debug CDC console for 10 seconds.
# Slide a finger on the trackpad during the capture window. PTP send
# debug prints emit ~5/s when finger is moving.
#
# Usage: sudo ./cdctest.sh
#
# Output: /tmp/deskhop_cdc.txt

set -u

VID="1209"
PID="C000"
OUT="/tmp/deskhop_cdc.txt"

if [[ $EUID -ne 0 ]]; then
    echo "Re-run with sudo." >&2
    exit 1
fi

exec > >(tee "$OUT") 2>&1
echo "(writing to $OUT)"

# Find a /dev/ttyACM* whose USB parent matches deskhop's VID:PID
TTY=""
for dev in /dev/ttyACM*; do
    [[ -c "$dev" ]] || continue
    sysdev="$(udevadm info --query=path --name="$dev" 2>/dev/null)"
    [[ -n "$sysdev" ]] || continue
    parent="/sys$sysdev"
    while [[ "$parent" != "/sys" && "$parent" != "/" ]]; do
        if [[ -f "$parent/idVendor" ]]; then
            v="$(cat "$parent/idVendor")"
            p="$(cat "$parent/idProduct")"
            if [[ "${v,,}" == "${VID,,}" && "${p,,}" == "${PID,,}" ]]; then
                TTY="$dev"
                break 2
            fi
        fi
        parent="$(dirname "$parent")"
    done
done

if [[ -z "$TTY" ]]; then
    echo "No /dev/ttyACM* found for $VID:$PID."
    echo "Available ttyACM devices:"
    ls -la /dev/ttyACM* 2>/dev/null || echo "  (none)"
    exit 1
fi

echo "CDC tty: $TTY"

# Set sane line settings (raw, 115200) so cat doesn't munge anything
stty -F "$TTY" 115200 raw -echo -echoe -echok -echonl -isig

echo ""
echo "============================================="
echo " SLIDE A FINGER ACROSS THE TRACKPAD NOW"
echo " (capturing CDC log for 10 seconds)"
echo "============================================="
echo ""

timeout 10 cat "$TTY" 2>/dev/null || true

echo ""
echo "============================================="
echo " summary"
echo "============================================="
PTP_LINES=$(grep -c "PATH_P: send" "$OUT" || true)
PTP_LINES=${PTP_LINES:-0}
echo "PATH_P send-lines captured: $PTP_LINES"
echo ""
echo "All lines mentioning PATH_P:"
grep "PATH_P" "$OUT" | tail -10 || echo "  (none)"
