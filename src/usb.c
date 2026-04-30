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

_Static_assert(MAX_DEVICES <= CFG_TUH_DEVICE_MAX,
               "MAX_DEVICES must not exceed CFG_TUH_DEVICE_MAX");

#define APPLE_VID          0x05AC
#define MAGIC_TRACKPAD_PID 0x0265

#ifdef DH_TRACKPAD_PHASE1
static mt_gesture_state_t mt_state;
#endif

#ifdef DH_DEBUG_TRACKPAD_LED
/* Set in tuh_hid_set_report_complete_cb. Currently informational only --
   could be exposed to user via a status pattern from main loop later. */
static volatile uint8_t dh_trackpad_activation_state; /* 0=pending, 1=ok, 2=fail */
#endif

#ifdef DH_TRACKPAD_PHASE1
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

#ifdef DH_DEBUG_TRACKPAD_LED
    if (report_type == 3 /* HID_REPORT_TYPE_FEATURE */) {
        dh_trackpad_activation_state = (len > 0) ? 1 : 2;
    }
#endif
}
#endif /* DH_TRACKPAD_PHASE1 */

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

#ifdef DH_TRACKPAD_PHASE1
    /* Magic Trackpad: switch out of mouse-emulation mode so we get full
       multi-touch frames on Report ID 0x02. We send the activation to the
       mouse-class instance (Instance 1 in the trackpad's enumeration). */
    if (iface->vendor_id == APPLE_VID
        && iface->product_id == MAGIC_TRACKPAD_PID
        && itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        mt_gesture_init(&mt_state);
        send_magic_trackpad_activation(dev_addr, instance);
    }
#endif
}

/* Invoked when received report from device via interrupt endpoint */
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (dev_addr > MAX_DEVICES || instance >= MAX_INTERFACES)
        return;

    hid_interface_t *iface = &global_state.iface[dev_addr-1][instance];

#ifdef DH_DEBUG_TRACKPAD_LED
    /* Visible signal that trackpad reports are reaching this callback.
       LED flicker = reports flowing; LED dark = nothing arriving on this
       board. Non-blocking: just an instantaneous GPIO toggle, safe to call
       from inside the host stack callback. */
    if (iface->vendor_id == APPLE_VID && iface->product_id == MAGIC_TRACKPAD_PID) {
        gpio_xor_mask(1u << GPIO_LED_PIN);
    }
    (void)dh_trackpad_activation_state;
#endif

#ifdef DH_TRACKPAD_PHASE1
    /* Magic Trackpad fast path: if the trackpad is in multi-touch mode the
       payload here is the proprietary frame format, not the standard mouse
       layout the descriptor advertises. Decode it ourselves and feed the
       result through the existing mouse pipeline + (for swipes) the keyboard
       pipeline. Pre-activation reports (8-byte mouse-emulation) WON'T decode
       as multi-touch frames; we fall through to the standard mouse pipeline
       below so the trackpad behaves like a generic mouse until activation
       lands -- and continues to work even if activation fails for any reason. */
    if (iface->vendor_id == APPLE_VID && iface->product_id == MAGIC_TRACKPAD_PID) {
        mt_frame_t frame;
        bool consumed = false;
        if (mt_decode_report(report, len, &frame)) {
            consumed = true;
            int32_t dx = 0, dy = 0, wheel = 0, pan = 0;
            mt_swipe_t swipe = MT_SWIPE_NONE;
            bool moved = mt_gesture_step(&mt_state, &frame,
                                         &dx, &dy, &wheel, &pan, &swipe);

            /* Linux's hid-magicmouse reads data[1] as the click bitmap for
               trackpad2 (BTN_MOUSE = data[1] & 1). We mirror that. */
            uint8_t buttons = report[1] & 0x07;
            bool buttons_changed = (buttons != global_state.mouse_buttons);

#ifndef DH_TRACKPAD_PHASE1_SKIP_EMIT
            if (moved || buttons_changed) {
                mouse_values_t values = {
                    .move_x  = dx,
                    .move_y  = dy,
                    .wheel   = wheel,
                    .pan     = pan,
                    .buttons = buttons,
                };
                enum screen_pos_e dir = update_mouse_position(&global_state, &values);
                mouse_report_t mr = create_mouse_report(&global_state, &values);
#ifndef DH_TRACKPAD_PHASE1_NO_OUTPUT_REPORT
                output_mouse_report(&mr, &global_state);
#endif
#ifndef DH_TRACKPAD_PHASE1_NO_SCREEN_SWITCH
                if (dir != NONE) do_screen_switch(&global_state, dir);
#endif
                global_state.mouse_buttons = buttons;
                (void)mr; (void)dir;
            }
#endif
            (void)moved; (void)buttons; (void)buttons_changed;

#ifndef DH_TRACKPAD_PHASE1_SKIP_EMIT
            if (swipe != MT_SWIPE_NONE) {
                /* Workspace / Spaces switching:
                     GNOME (Linux) default = Ctrl+Alt+Left / Ctrl+Alt+Right
                     macOS default         = Ctrl+Left / Ctrl+Right
                   Including Alt as well as Ctrl matches GNOME and is harmless
                   on macOS (Ctrl+Alt+Left isn't bound by default), so it's a
                   no-op there rather than firing the wrong action. Once we
                   have a config knob this should become user-selectable. */
                uint8_t keycode = (swipe == MT_SWIPE_LEFT)
                                  ? HID_KEY_ARROW_LEFT
                                  : HID_KEY_ARROW_RIGHT;
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
#endif
            (void)swipe;

#ifdef DH_DEBUG_TRACKPAD
            /* Per-frame visibility on 3-finger gestures, throttled to 1 in 8
               so CDC isn't drowned. Useful when tuning the swipe threshold. */
            static uint8_t three_finger_log_div = 0;
            if (frame.finger_count == 3 && (++three_finger_log_div & 0x07) == 0) {
                dh_debug_printf("3F frame: accum_x=%ld emitted=%d\n",
                                (long)mt_state.swipe_accum_x,
                                mt_state.swipe_emitted ? 1 : 0);
            }
#endif
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
#endif /* DH_TRACKPAD_PHASE1 */

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
