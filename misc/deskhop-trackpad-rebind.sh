#!/bin/sh
# Manual rebind helper -- run as root after plugging in the trackpad.
#
# We need to detach hid-magicmouse from interfaces 0 (keyboard) and 1
# (relmouse) and attach hid-generic instead. Plain unbind+bind races against
# the kernel's autoprobe, which immediately re-binds magicmouse (most
# specific VID/PID match) before our explicit bind to hid-generic can fire.
#
# Trick: temporarily disable autoprobe on the HID bus, do the rebind, then
# restore. While autoprobe is off, the kernel won't auto-attach drivers --
# so our explicit bind to hid-generic is the one that takes.

set -x

prev_autoprobe=$(cat /sys/bus/hid/drivers_autoprobe 2>/dev/null || echo 1)
echo 0 > /sys/bus/hid/drivers_autoprobe

for hid in /sys/bus/hid/drivers/magicmouse/0003:05AC:0265.*; do
    [ -L "$hid" ] || continue
    name=$(basename "$hid")
    real_path=$(readlink -f "$hid")
    iface_dir=$(dirname "$real_path")
    iface_num=$(cat "$iface_dir/bInterfaceNumber" 2>/dev/null)

    case "$iface_num" in
        00|01)
            echo "rebinding $name (iface $iface_num) magicmouse -> hid-generic"
            echo "$name" > /sys/bus/hid/drivers/magicmouse/unbind
            echo "$name" > /sys/bus/hid/drivers/hid-generic/bind
            ;;
        *)
            echo "leaving $name (iface $iface_num) bound to magicmouse"
            ;;
    esac
done

echo "$prev_autoprobe" > /sys/bus/hid/drivers_autoprobe
