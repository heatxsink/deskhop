#!/bin/sh
# Manual rebind helper -- run as root after plugging in the trackpad.
# If this script makes the keyboard work, we know the rebind logic is
# correct and the udev rule just needs better event timing.

for hid in /sys/bus/hid/drivers/magicmouse/0003:05AC:0265.*; do
    [ -L "$hid" ] || continue
    name=$(basename "$hid")
    iface_num=$(cat "$hid/../bInterfaceNumber" 2>/dev/null || cat "$hid/../../bInterfaceNumber" 2>/dev/null)
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
