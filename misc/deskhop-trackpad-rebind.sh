#!/bin/sh
# Manual rebind helper -- run as root after plugging in the trackpad.
# If this script makes the keyboard work, we know the rebind logic is
# correct and the udev rule just needs better event timing.

set -x  # echo every command for diagnostic visibility

ls -l /sys/bus/hid/drivers/magicmouse/ | grep -E "05AC|0265" || echo "no magicmouse-bound deskhop HIDs found"

for hid in /sys/bus/hid/drivers/magicmouse/0003:05AC:0265.*; do
    [ -L "$hid" ] || continue
    name=$(basename "$hid")
    # The hid device is a symlink under the driver dir.  Resolve to the real
    # /sys/devices path, then walk up one level to reach the USB interface
    # device that carries bInterfaceNumber.
    real_path=$(readlink -f "$hid")
    iface_dir=$(dirname "$real_path")
    iface_num=$(cat "$iface_dir/bInterfaceNumber" 2>/dev/null)

    echo "found $name -> $real_path (interface=$iface_num)"

    case "$iface_num" in
        00|01)
            echo "rebinding $name (interface $iface_num) magicmouse -> hid-generic"
            echo "$name" > /sys/bus/hid/drivers/magicmouse/unbind
            echo "$name" > /sys/bus/hid/drivers/hid-generic/bind
            ;;
        *)
            echo "leaving $name (interface $iface_num) bound to magicmouse"
            ;;
    esac
done
