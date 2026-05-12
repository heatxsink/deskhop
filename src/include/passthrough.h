/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 */
#pragma once

#ifdef DH_PASSTHROUGH

#include <stdbool.h>
#include <stdint.h>

/*==============================================================================
 *  Apple Magic Trackpad USB passthrough
 *
 *  When a Magic Trackpad is mounted on this Pico's host port, spoof Apple
 *  VID/PID and re-emit the trackpad's own HID report descriptor on the
 *  device side. Linux's hid-magicmouse kernel driver then claims the
 *  device by VID/PID match and decodes Apple's proprietary multi-touch
 *  frames natively. No firmware-side parsing on the active output path.
 *
 *  Cross-output (the other Pico is the active output): decode locally
 *  and forward as a regular relmouse over UART -- native gestures lost
 *  but cursor + click + scroll preserved.
 *
 *  Lifecycle:
 *    tuh_hid_mount_cb     -> passthrough_cache_apple_descriptor()
 *                            -> sets trackpad_attached, reenumerate_pending
 *    tuh_hid_umount_cb    -> sets passthrough_unmount_at_us (sticky mode
 *                            means the unmount never actually commits)
 *    usb_host_task        -> passthrough_tick_unmount_debounce() (no-op
 *                            in sticky mode)
 *    usb_device_task      -> consumes reenumerate_pending, drops/re-raises
 *                            the device-side USB pull-up
 *============================================================================*/

/* Cache the trackpad's HID report descriptor and build the runtime
   device-side configuration descriptor that re-exposes it. Called from
   tuh_hid_mount_cb when an Apple Magic Trackpad's mouse-class interface
   mounts on the host port. Also resets per-session passthrough state. */
void passthrough_cache_apple_descriptor(uint8_t const *desc, uint16_t len);

/* Clear the cached descriptor (descriptor_ready -> false). Called by
   passthrough_tick_unmount_debounce when an unmount commits. In sticky
   mode this is dead until firmware reboot. */
void passthrough_clear_apple_descriptor(void);

/* True once a descriptor has been cached. Drives the trackpad-spoofed
   branch in tud_descriptor_*_cb. */
bool passthrough_descriptor_ready(void);

/* Pointer to the cached Apple HID report descriptor. Returned by
   tud_hid_descriptor_report_cb when in spoofed mode. */
uint8_t const *passthrough_apple_hid_descriptor(uint16_t *out_len);

/* Timestamp of the most recent unmount attempt. Set in tuh_hid_umount_cb,
   cleared in tuh_hid_mount_cb if a remount lands within the debounce
   window. Consumed by passthrough_tick_unmount_debounce. */
extern uint64_t passthrough_unmount_at_us;

/* Polled from usb_host_task. Commits a pending unmount only if the
   debounce window has elapsed. Currently a no-op (sticky mode) -- the
   debounce window is set to UINT64_MAX so the unmount never commits.
   Sticky mode trades auto-revert-on-unplug for a stable host identity. */
void passthrough_tick_unmount_debounce(void);

#endif /* DH_PASSTHROUGH */
