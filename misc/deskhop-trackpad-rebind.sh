#!/bin/sh
# Manual rebind helper -- run as root after plugging in the trackpad.
#
# We need:
#   interfaces 0 (keyboard) and 1 (relmouse helper) bound to hid-generic
#   interface 2 (the actual trackpad)              bound to hid-magicmouse
#
# Plain unbind+bind races against the kernel's autoprobe (which always
# picks magicmouse since it's the most specific VID/PID match). We disable
# autoprobe on the HID bus for the duration, then do explicit binds.
#
# Iterate over ALL deskhop-spoofed HID devices regardless of which driver
# they're currently bound to (or none) -- the previous version of this
# script could leave interfaces orphaned, this one heals from that state.

set -x

prev_autoprobe=$(cat /sys/bus/hid/drivers_autoprobe 2>/dev/null || echo 1)
echo 0 > /sys/bus/hid/drivers_autoprobe

for hid in /sys/bus/hid/devices/0003:05AC:0265.*; do
    [ -L "$hid" ] || continue
    name=$(basename "$hid")
    real_path=$(readlink -f "$hid")
    iface_dir=$(dirname "$real_path")
    iface_num=$(cat "$iface_dir/bInterfaceNumber" 2>/dev/null)
    current_driver=""
    if [ -L "$real_path/driver" ]; then
        current_driver=$(basename "$(readlink "$real_path/driver")")
    fi

    case "$iface_num" in
        00|01) target="hid-generic" ;;
        02)    target="magicmouse"  ;;
        *)     target=""            ;;
    esac

    [ -z "$target" ] && continue

    if [ "$current_driver" = "$target" ]; then
        echo "$name already bound to $target -- nothing to do"
        continue
    fi

    if [ -n "$current_driver" ]; then
        echo "$name: unbinding from $current_driver"
        echo "$name" > "/sys/bus/hid/drivers/$current_driver/unbind"
    fi
    echo "$name: binding to $target"
    echo "$name" > "/sys/bus/hid/drivers/$target/bind"
done

echo "$prev_autoprobe" > /sys/bus/hid/drivers_autoprobe
