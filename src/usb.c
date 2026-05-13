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

#include "main.h"
#include "magic_trackpad.h"
#ifdef DH_MAGIC_TRACKPAD
#include "magic_trackpad_tap.h"
#endif
#ifdef DH_PASSTHROUGH
#include "passthrough.h"
#include "passthrough_uart.h"
#endif

_Static_assert(MAX_DEVICES <= CFG_TUH_DEVICE_MAX,
               "MAX_DEVICES must not exceed CFG_TUH_DEVICE_MAX");

#define APPLE_VID          0x05AC
#define MAGIC_TRACKPAD_PID 0x0265

#ifdef DH_MAGIC_TRACKPAD
static mt_gesture_state_t mt_state;
#endif

#ifdef DH_MAGIC_TRACKPAD
/* Switches Magic Trackpad 2 USB out of mouse-emulation mode and into the
   proprietary multi-touch report format. Layout matches Linux's
   drivers/hid/hid-magicmouse.c feature_mt_trackpad2_usb.

   Linux's usbhid_set_raw_report sends the buffer including the report ID byte
   AS the wire payload (count=2, buf={0x02,0x01}). TinyUSB's tuh_hid_set_report
   only encodes the report ID in wValue -- the buffer is the wire payload as-is.
   So we have to include the report ID byte ourselves. */
static uint8_t magic_trackpad_activate[] = { 0x02, 0x01 };

static void send_magic_trackpad_activation(uint8_t dev_addr, uint8_t instance) {
    bool ok = tuh_hid_set_report(dev_addr, instance, 0x02,
                                 HID_REPORT_TYPE_FEATURE,
                                 magic_trackpad_activate,
                                 sizeof(magic_trackpad_activate));
    dh_debug_printf("Magic Trackpad activate: dev=%u inst=%u %s\n",
                    dev_addr, instance, ok ? "queued" : "FAILED");
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance,
                                    uint8_t report_id, uint8_t report_type,
                                    uint16_t len) {
    dh_debug_printf("set_report complete: dev=%u inst=%u id=%02x type=%u len=%u\n",
                    dev_addr, instance, report_id, report_type, len);
}
#endif /* DH_MAGIC_TRACKPAD */

#ifdef DH_DEBUG_TRACKPAD
static void hex_dump(const char *label, const uint8_t *data, int len) {
    dh_debug_printf("%s (len=%d):\n", label, len);
    char line[80];
    for (int i = 0; i < len; i += 16) {
        int pos = snprintf(line, sizeof(line), "  %04x:", i);
        for (int j = 0; j < 16 && i + j < len; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, " %02x", data[i + j]);
        }
        dh_debug_printf("%s\n", line);
    }
}
#endif

/* ================================================== *
 * ===========  TinyUSB Device Callbacks  =========== *
 * ================================================== */

/* Invoked when we get GET_REPORT control request.
 * We are expected to fill buffer with the report content, update reqlen
 * and return its length. We return 0 to STALL the request. */
uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t request_len) {
    return 0;
}

/**
 * Computer controls our LEDs by sending USB SetReport messages with a payload
 * of just 1 byte and report type output. It's type 0x21 (USB_REQ_DIR_OUT |
 * USB_REQ_TYP_CLASS | USB_REQ_REC_IFACE) Request code for SetReport is 0x09,
 * report type is 0x02 (HID_REPORT_TYPE_OUTPUT). We get a set_report callback
 * from TinyUSB device HID and then figure out what to do with the LEDs.
 */
void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize) {
    /* We received a report on the config report ID */
    if (instance == ITF_NUM_HID_VENDOR && report_id == REPORT_ID_VENDOR) {
        /* Security - only if config mode is enabled are we allowed to do anything. While the report_id
           isn't even advertised when not in config mode, security must always be explicit and never assume */
        if (!global_state.config_mode_active)
            return;

        /* We insist on a fixed size packet. No overflows. */
        if (bufsize != RAW_PACKET_LENGTH)
            return;

        uart_packet_t *packet = (uart_packet_t *) (buffer + START_LENGTH);

        /* Only a certain packet types are accepted */
        if (!validate_packet(packet))
            return;

        process_packet(packet, &global_state);
    }

    /* Only other set report we care about is LED state change, and that's exactly 1 byte long */
    if (report_id != REPORT_ID_KEYBOARD || bufsize != 1 || report_type != HID_REPORT_TYPE_OUTPUT)
        return;

    uint8_t leds = buffer[0];

    /* If we are using caps lock LED to indicate the chosen output, that has priority */
    if (global_state.config.kbd_led_as_indicator) {
        leds = leds & 0xFD; /* 1111 1101 (Clear Caps Lock bit) */

        if (global_state.active_output)
            leds |= KEYBOARD_LED_CAPSLOCK;
    }

    global_state.keyboard_leds[BOARD_ROLE] = leds;

    /* If the board has a keyboard connected directly, restore those leds. */
    if (global_state.keyboard_connected && CURRENT_BOARD_IS_ACTIVE_OUTPUT)
        restore_leds(&global_state);

    /* Always send to the other one, so it is aware of the change */
    send_value(leds, KBD_SET_REPORT_MSG);
}

/* Invoked when device is mounted */
void tud_mount_cb(void) {
    global_state.tud_connected = true;
}

/* Invoked when device is unmounted */
void tud_umount_cb(void) {
    global_state.tud_connected = false;
}

#ifdef DH_DEBUG_CDC_FLASH
void tud_cdc_rx_cb(uint8_t itf) {
    char buf[64];
    uint32_t count = tud_cdc_n_available(itf);

    if (count == 0)
        return;

    if (count > sizeof(buf))
        count = sizeof(buf);

    tud_cdc_n_read(itf, buf, count);

    if (count >= 5 && memcmp(buf, "flash", 5) == 0) {
        reset_usb_boot(0, 0);
    }
}
#endif

/* ================================================== *
 * ===============  USB HOST Section  =============== *
 * ================================================== */

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (dev_addr > MAX_DEVICES || instance >= MAX_INTERFACES)
        return;

    hid_interface_t *iface = &global_state.iface[dev_addr-1][instance];

#ifdef DH_PASSTHROUGH
    if (iface->vendor_id == APPLE_VID
        && iface->product_id == MAGIC_TRACKPAD_PID
        && itf_protocol == HID_ITF_PROTOCOL_MOUSE
        && global_state.trackpad_attached) {
        /* Defer the unmount. PIO-USB on the Pico's host port drops the
           Magic Trackpad's connection for short windows (sub-second),
           which would otherwise cause us to immediately re-enumerate
           the device side back to deskhop VID/PID -- visible to the
           host PC as the trackpad disappearing every few seconds. We
           hold off and let usb_host_task commit the unmount only if it
           sticks. A remount within the debounce window cancels the
           pending unmount entirely. */
        passthrough_unmount_at_us = time_us_64();
    }
#endif

    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            global_state.keyboard_connected = false;
            break;

        case HID_ITF_PROTOCOL_MOUSE:
            global_state.mouse_connected = false;
            break;
    }

    /* Also clear the interface structure, otherwise plugging something else later
       might be a fun (and confusing) experience */
    memset(iface, 0, sizeof(hid_interface_t));
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (dev_addr > MAX_DEVICES || instance >= MAX_INTERFACES)
        return;

    /* Get interface information */
    hid_interface_t *iface = &global_state.iface[dev_addr-1][instance];

    iface->protocol = tuh_hid_get_protocol(dev_addr, instance);

    /* Cache VID/PID so the report path doesn't have to re-query TinyUSB. */
    tuh_vid_pid_get(dev_addr, &iface->vendor_id, &iface->product_id);

#ifdef DH_DEBUG_TRACKPAD
    dh_debug_printf("HID mount: dev_addr=%u instance=%u itf_protocol=%u vid=%04x pid=%04x\n",
                    dev_addr, instance, itf_protocol,
                    iface->vendor_id, iface->product_id);
    hex_dump("HID descriptor", desc_report, desc_len);
#endif

    /* Parse the report descriptor into our internal structure. */
    parse_report_descriptor(iface, desc_report, desc_len);

    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            if (global_state.config.enforce_ports && BOARD_ROLE == OUTPUT_B)
                return;

            if (global_state.config.force_kbd_boot_protocol)
                tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT);

            /* Keeping this is required for setting leds from device set_report callback */
            global_state.kbd_dev_addr       = dev_addr;
            global_state.kbd_instance       = instance;
            global_state.keyboard_connected = true;
            break;

        case HID_ITF_PROTOCOL_MOUSE:
            if (global_state.config.enforce_ports && BOARD_ROLE == OUTPUT_A)
                return;

            if (global_state.config.force_mouse_boot_mode) {
                /* User requested boot mode - simpler protocol for compatibility.
                   Note: many mice still send wheel data even in boot mode. */
                tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT);
            } else {
                /* Switch to using report protocol instead of boot, it's more complicated but
                   at least we get all the information we need (looking at you, mouse wheel) */
                if (tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT) {
                    tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);
                }
            }
            global_state.mouse_connected = true;
            break;

        case HID_ITF_PROTOCOL_NONE:
            break;
    }

    /* Also set mouse_connected if report descriptor contains mouse, even if interface
       protocol says keyboard. This handles composite devices like QMK. */
    if (iface->mouse.is_found) {
        global_state.mouse_connected = true;
    }

    /* Flash local led to indicate a device was connected */
    blink_led(&global_state);

    /* Also signal the other board to flash LED, to enable easy verification if serial works */
    send_value(ENABLE, FLASH_LED_MSG);

    /* Kick off the report querying */
    tuh_hid_receive_report(dev_addr, instance);

#ifdef DH_MAGIC_TRACKPAD
    /* Magic Trackpad: switch out of mouse-emulation mode so we get full
       multi-touch frames on Report ID 0x02. We send the activation to the
       mouse-class instance (Instance 1 in the trackpad's enumeration). */
    if (iface->vendor_id == APPLE_VID
        && iface->product_id == MAGIC_TRACKPAD_PID
        && itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        mt_gesture_init(&mt_state);
        send_magic_trackpad_activation(dev_addr, instance);
#ifdef DH_PASSTHROUGH
        /* Cache the trackpad's HID report descriptor so we can re-emit
           it as our own. The mouse-class interface (instance 1) is the
           one whose descriptor describes the multi-touch reports
           hid-magicmouse expects to parse, so this is the right one to
           cache. */
        bool was_pending_unmount = (passthrough_unmount_at_us != 0);
        passthrough_unmount_at_us = 0;  /* cancel any pending unmount */
#ifdef DH_DEBUG_TRACKPAD
        dh_debug_printf("PASSTHROUGH: mount; caching Apple HID descriptor (%u bytes)%s\n",
                        desc_len, was_pending_unmount ? " [debounce-cancel]" : "");
#endif
        passthrough_cache_apple_descriptor(desc_report, desc_len);
        if (!global_state.trackpad_attached) {
            /* Phase 2B: tell the other Pico that a trackpad is on this
               side and ship our cached descriptor so it can prime its
               own cache. Only on fresh mounts; debounce-cancel mounts
               are continuations and the other side already has it. */
            send_value(1, TRACKPAD_PRESENCE_MSG);
            (void)passthrough_uart_send_chunked(TRACKPAD_DESC_CHUNK_MSG,
                                                desc_report, desc_len);

            global_state.trackpad_attached    = true;
            global_state.reenumerate_pending  = true;
        }
        /* If was_pending_unmount, the host PC never saw us drop the
           trackpad -- no re-enumeration needed. */
        (void)was_pending_unmount;
#endif
    }
#endif
}

/* Drain the tap state machine's pending button-event queue, emitting one
   mouse report per entry. Each entry is a button-state transition; we
   compute the resulting button bitmap by mutating global_state.mouse_buttons
   incrementally. Bypasses the 100-Hz throttle because taps are infrequent
   and need immediate emission. No-op when DH_MAGIC_TRACKPAD is off. */
static void mt_tap_drain_pending(void) {
#ifdef DH_MAGIC_TRACKPAD
    tp_tap_t *tap = (tp_tap_t *)mt_state.tap;
    if (!tap || tap->pending_count == 0) return;
#ifdef DH_DEBUG_TRACKPAD
    dh_debug_printf("drain: %u events\n", tap->pending_count);
#endif
    for (int p = 0; p < tap->pending_count; p++) {
        tp_tap_button_event_t ev = tap->pending[p];
        uint8_t b = (uint8_t)global_state.mouse_buttons;
        if (ev.pressed) b |= (uint8_t)ev.button;
        else            b &= (uint8_t)~ev.button;
#ifdef DH_DEBUG_TRACKPAD
        dh_debug_printf("drain[%d]: btn=%u pressed=%u -> mouse_buttons=%u\n",
                        p, ev.button, ev.pressed, b);
#endif
        mouse_values_t tv = {
            .move_x = 0, .move_y = 0, .wheel = 0, .pan = 0,
            .buttons = b,
        };
        mouse_report_t tmr = create_mouse_report(&global_state, &tv);
        output_mouse_report(&tmr, &global_state);
        global_state.mouse_buttons = b;
    }
    tap->pending_count = 0;
#endif
}

/* core1 task. Runs periodically to advance the tap state machine even
   when no HID reports are coming in. This is how we detect tap lifts:
   the trackpad goes silent after the last finger lifts (no terminator
   frame), so without this tick the state machine never sees RELEASE
   events. Also fires TIMEOUT events when tap timers have expired. */
void mt_tap_idle_tick_task(device_t *state) {
    (void)state;
#ifdef DH_MAGIC_TRACKPAD
    uint32_t now_us = time_us_32();
    if (mt_gesture_idle_tick(&mt_state, now_us)) {
        mt_tap_drain_pending();
    }
#endif
}

/* Invoked when received report from device via interrupt endpoint */
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (dev_addr > MAX_DEVICES || instance >= MAX_INTERFACES)
        return;

    hid_interface_t *iface = &global_state.iface[dev_addr-1][instance];

#ifdef DH_MAGIC_TRACKPAD
    /* Magic Trackpad fast path: if the trackpad is in multi-touch mode the
       payload here is the proprietary frame format, not the standard mouse
       layout the descriptor advertises. Decode it ourselves and feed the
       result through the existing mouse pipeline + (for swipes) the keyboard
       pipeline. Pre-activation reports (8-byte mouse-emulation) WON'T decode
       as multi-touch frames; we fall through to the standard mouse pipeline
       below so the trackpad behaves like a generic mouse until activation
       lands -- and continues to work even if activation fails for any reason. */
    if (iface->vendor_id == APPLE_VID && iface->product_id == MAGIC_TRACKPAD_PID) {
#ifdef DH_PASSTHROUGH
        /* Passthrough mode logic:
            - Active output (trackpad-side host PC is the focused screen):
              forward raw Apple bytes so hid-magicmouse decodes them
              natively. ALSO run our own decode + update_mouse_position
              on the side, so an edge-sweep can still switch outputs.
            - Inactive output: don't forward raw bytes (they'd vanish
              into a host PC the user isn't looking at). Instead decode
              the frame to a mouse report and call output_mouse_report,
              which routes it over UART to the Pico that IS active.
              That side then re-emits as a regular mouse. The user
              loses native gestures on the cross-output path but cursor
              + click + scroll work, so a single trackpad still drives
              whichever screen is active. */

        /* Forward raw bytes only when this side is active. */
        if (global_state.trackpad_attached &&
            itf_protocol == HID_ITF_PROTOCOL_MOUSE &&
            CURRENT_BOARD_IS_ACTIVE_OUTPUT &&
            len > 0) {
            uint8_t report_id = report[0];
            (void)tud_hid_n_report(0, report_id, &report[1],
                                   (uint16_t)(len - 1));
        }

        /* Decode the frame to drive edge-detection (active side) or
           UART-forward as a regular mouse (inactive side). */
        mt_frame_t pt_frame;
        if (itf_protocol == HID_ITF_PROTOCOL_MOUSE &&
            mt_decode_report(report, len, &pt_frame)) {
            static bool          pt_prev_phys_click = false;
            static mt_button_t   pt_held_button     = MT_BTN_NONE;
            bool phys_click_now = (report[1] & 0x01) != 0;
            if (phys_click_now && !pt_prev_phys_click) {
                pt_held_button = (pt_frame.finger_count >= 2)
                                 ? MT_BTN_RIGHT : MT_BTN_LEFT;
            } else if (!phys_click_now && pt_prev_phys_click) {
                pt_held_button = MT_BTN_NONE;
            }
            pt_prev_phys_click = phys_click_now;

            int32_t dx = 0, dy = 0, wheel = 0, pan = 0;
            mt_swipe_t swipe = MT_SWIPE_NONE;
            uint32_t now_us = time_us_32();
            bool moved = mt_gesture_step(&mt_state, &pt_frame, pt_held_button,
                                         now_us, &dx, &dy, &wheel, &pan, &swipe);
            mt_tap_drain_pending();

            uint8_t buttons = (uint8_t)pt_held_button;
            bool buttons_changed = (buttons != global_state.mouse_buttons);

            if ((moved || buttons_changed) && !CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
                /* Inactive output side: forward decoded mouse via UART
                   to whichever Pico is active. update_mouse_position
                   tracks our internal virtual cursor across screens
                   AND triggers an output-switch on edge cross -- this
                   is in sync with the real cursor on the active host
                   because deskhop is the one driving that cursor on
                   that side (via the relmouse UART path). */
                mouse_values_t values = {
                    .move_x  = dx,
                    .move_y  = dy,
                    .wheel   = wheel,
                    .pan     = pan,
                    .buttons = buttons,
                };
                enum screen_pos_e dir =
                    update_mouse_position(&global_state, &values);
                mouse_report_t mr =
                    create_mouse_report(&global_state, &values);
                output_mouse_report(&mr, &global_state);
                global_state.mouse_buttons = buttons;
                if (dir != NONE)
                    do_screen_switch(&global_state, dir);
            }

            /* Active-side edge-sweep detection. We can't track the real
               cursor (hid-magicmouse owns it), so detect a "push past
               the trackpad's physical edge" gesture instead: a finger
               in the outer ~10% of the trackpad surface that's moving
               toward that edge. Apple's coordinate range is approx
               -3678..+3934 X, so the edge zones are at around |x|>3300.
               Per-slot state so we can compute dx between frames; 1s
               cooldown on the trigger so a held-finger-at-edge doesn't
               re-fire continuously. */
            if (CURRENT_BOARD_IS_ACTIVE_OUTPUT && pt_frame.finger_count > 0) {
                static int16_t  edge_prev_x[MT_MAX_FINGERS] = {0};
                static bool     edge_prev_valid[MT_MAX_FINGERS] = {0};
                static uint64_t edge_last_switch_us = 0;
                const int16_t   EDGE_X_RIGHT_GATE = 3300;
                const int16_t   EDGE_X_LEFT_GATE  = -3000;
                const int16_t   EDGE_DX_GATE      = 30;
                const uint64_t  EDGE_COOLDOWN_US  = 1000000;  /* 1 s */

                uint64_t now64 = time_us_64();
                bool may_fire = (now64 - edge_last_switch_us) > EDGE_COOLDOWN_US;

                for (int i = 0; i < pt_frame.finger_count && i < MT_MAX_FINGERS; i++) {
                    uint8_t slot = pt_frame.fingers[i].finger_id % MT_MAX_FINGERS;
                    int16_t cur_x = pt_frame.fingers[i].x;

                    if (may_fire && edge_prev_valid[slot]) {
                        int16_t fdx = cur_x - edge_prev_x[slot];
                        if (cur_x >  EDGE_X_RIGHT_GATE && fdx >  EDGE_DX_GATE) {
                            do_screen_switch(&global_state, RIGHT);
                            edge_last_switch_us = now64;
                            break;
                        }
                        if (cur_x <  EDGE_X_LEFT_GATE  && fdx < -EDGE_DX_GATE) {
                            do_screen_switch(&global_state, LEFT);
                            edge_last_switch_us = now64;
                            break;
                        }
                    }
                    edge_prev_x[slot]     = cur_x;
                    edge_prev_valid[slot] = true;
                }
            }
        }

        tuh_hid_receive_report(dev_addr, instance);
        return;
        /* Note: when DH_PASSTHROUGH is on and mt_decode_report() failed
           (trackpad still in 8-byte mouse-emulation mode pre-activation),
           we return here without falling through to the standard mouse
           pipeline. Active-side: the raw bytes were forwarded above and
           hid-magicmouse handles them. Inactive-side: those bytes are
           dropped during the brief window before activation lands. The
           window is typically <1 s; if it bites in practice, fall through
           to the Path G translation block below instead of returning. */
#endif
        mt_frame_t frame;
        bool consumed = false;
        if (mt_decode_report(report, len, &frame)) {
            consumed = true;

            /* Trackpad has only one physical button. We translate it to LEFT
               or RIGHT at click-down based on how many fingers were on the
               surface at that moment, then keep it sticky for the duration
               of the held click. Two-finger click-down = right click;
               one-finger click-down = left click. */
            static bool prev_phys_click = false;
            static mt_button_t held_button = MT_BTN_NONE;

            bool phys_click_now = (report[1] & 0x01) != 0;
            if (phys_click_now && !prev_phys_click) {
                held_button = (frame.finger_count >= 2) ? MT_BTN_RIGHT : MT_BTN_LEFT;
            } else if (!phys_click_now && prev_phys_click) {
                held_button = MT_BTN_NONE;
            }
            prev_phys_click = phys_click_now;

            int32_t dx = 0, dy = 0, wheel = 0, pan = 0;
            mt_swipe_t swipe = MT_SWIPE_NONE;
            uint32_t step_now_us = time_us_32();
            bool moved = mt_gesture_step(&mt_state, &frame, held_button,
                                         step_now_us,
                                         &dx, &dy, &wheel, &pan, &swipe);

            /* Drain any tap state-machine button events generated by the
               step above. Helper handles the no-op case when the flag
               is off. */
            mt_tap_drain_pending();

            /* Mouse-emit path. The translated button persists for the
               duration of the held click rather than reflecting the raw
               trackpad bit. */
            uint8_t buttons = (uint8_t)held_button;
            bool buttons_changed = (buttons != global_state.mouse_buttons);

            /* Throttle emit to ~100 Hz, accumulating motion across frames in
               between. The trackpad's native report rate (100-250 Hz) appears
               to trigger a watchdog reboot when output_mouse_report fires on
               every frame -- root cause unknown, bisected to this call.
               Throttling sidesteps it. */
            static int32_t accum_dx = 0, accum_dy = 0, accum_wheel = 0, accum_pan = 0;
            static uint32_t last_emit_us = 0;
            const uint32_t EMIT_INTERVAL_US = 10000; /* 100 Hz */

            if (moved) {
                accum_dx    += dx;
                accum_dy    += dy;
                accum_wheel += wheel;
                accum_pan   += pan;
            }

            uint32_t now_us = time_us_32();
            bool have_motion = (accum_dx || accum_dy || accum_wheel || accum_pan);
            if ((have_motion || buttons_changed) &&
                (now_us - last_emit_us) >= EMIT_INTERVAL_US) {
                mouse_values_t values = {
                    .move_x  = accum_dx,
                    .move_y  = accum_dy,
                    .wheel   = accum_wheel,
                    .pan     = accum_pan,
                    .buttons = buttons,
                };
                enum screen_pos_e dir = update_mouse_position(&global_state, &values);
                mouse_report_t mr = create_mouse_report(&global_state, &values);
                output_mouse_report(&mr, &global_state);
                if (dir != NONE) do_screen_switch(&global_state, dir);
                global_state.mouse_buttons = buttons;
                accum_dx = accum_dy = accum_wheel = accum_pan = 0;
                last_emit_us = now_us;
            }

            if (swipe != MT_SWIPE_NONE) {
                /* Workspace / Spaces switching:
                     GNOME (Linux) default = Ctrl+Alt+Left / Ctrl+Alt+Right
                     macOS default         = Ctrl+Left / Ctrl+Right
                   Including Alt as well as Ctrl matches GNOME and is harmless
                   on macOS (Ctrl+Alt+Left isn't bound by default), so it's a
                   no-op there rather than firing the wrong action. Once we
                   have a config knob this should become user-selectable. */
                /* Natural-scroll convention: swipe right -> show what's to the
                   left (previous workspace). Inverted from finger direction. */
                uint8_t keycode = (swipe == MT_SWIPE_LEFT)
                                  ? HID_KEY_ARROW_RIGHT
                                  : HID_KEY_ARROW_LEFT;
                hid_keyboard_report_t press = {
                    .modifier = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTALT,
                    .reserved = 0,
                    .keycode  = { keycode, 0, 0, 0, 0, 0 },
                };
                hid_keyboard_report_t release = { 0 };
                if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
                    queue_kbd_report(&press, &global_state);
                    queue_kbd_report(&release, &global_state);
                } else {
                    queue_packet((uint8_t *)&press,   KEYBOARD_REPORT_MSG, KBD_REPORT_LENGTH);
                    queue_packet((uint8_t *)&release, KEYBOARD_REPORT_MSG, KBD_REPORT_LENGTH);
                }
            }
        }
        if (consumed) {
            tuh_hid_receive_report(dev_addr, instance);
            return;
        }
        /* Decode failed -- trackpad is in mouse-emulation mode (activation
           hasn't landed yet, or failed entirely). Fall through to the
           standard mouse pipeline so the user gets at least basic cursor
           movement. */
    }
#endif /* DH_MAGIC_TRACKPAD */

    /* Calculate a device index that distinguishes between different devices
       while staying within the bounds of MAX_DEVICES.

       Device index assignment:
       - 0: Primary keyboard (the one set in tuh_hid_mount_cb)
       - 1: Mouse devices
       - MAX_DEVICES-2: Secondary keyboards (e.g., wireless keyboard through unified dongle)
       - (dev_addr-1) % (MAX_DEVICES-1): Other devices

       Note: Slot MAX_DEVICES-1 is reserved for the remote device (used in handle_keyboard_uart_msg) */
    uint8_t device_idx;

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        if (dev_addr == global_state.kbd_dev_addr && instance == global_state.kbd_instance) {
            /* Primary keyboard */
            device_idx = 0;
        } else {
            /* Secondary keyboard (e.g., wireless keyboard through unified dongle) */
            device_idx = (MAX_DEVICES - 2);
        }
    } else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        /* Mouse devices */
        device_idx = 1;
    } else {
        /* Other devices */
        device_idx = (dev_addr - 1) % (MAX_DEVICES - 1);
    }

    if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = 0;

        if (iface->uses_report_id)
            report_id = report[0];

        if (report_id < MAX_REPORTS) {
            process_report_f receiver = iface->report_handler[report_id];

            if (receiver != NULL)
                receiver((uint8_t *)report, len, device_idx, iface);
        }
    }
    else if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        process_keyboard_report((uint8_t *)report, len, device_idx, iface);
    }
    else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        process_mouse_report((uint8_t *)report, len, device_idx, iface);
    }

    /* Continue requesting reports */
    tuh_hid_receive_report(dev_addr, instance);
}

/* Set protocol in a callback. This is tied to an interface, not a specific report ID */
void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t protocol) {
    if (dev_addr > MAX_DEVICES || idx > MAX_INTERFACES)
        return;

    hid_interface_t *iface = &global_state.iface[dev_addr-1][idx];
    iface->protocol = protocol;
}
