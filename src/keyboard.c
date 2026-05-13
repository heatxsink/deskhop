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

/* ==================================================== *
 * Hotkeys to trigger actions via the keyboard.
 * ==================================================== */

hotkey_combo_t hotkeys[] = {
    /* Main keyboard switching hotkey */
    {.modifier       = HOTKEY_MODIFIER,
     .keys           = {HOTKEY_TOGGLE},
     .key_count      = 1,
     .pass_to_os     = false,
     .action_handler = &output_toggle_hotkey_handler},

    /* Pressing right ALT + right CTRL toggles the slow mouse mode */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTALT | KEYBOARD_MODIFIER_RIGHTCTRL,
     .keys           = {},
     .key_count      = 0,
     .pass_to_os     = true,
     .acknowledge    = true,
     .action_handler = &mouse_zoom_hotkey_handler},

    /* Switch lock */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTCTRL,
     .keys           = {HID_KEY_K},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &switchlock_hotkey_handler},

    /* Screen lock -- Left Super + L (matches the GNOME default lock shortcut).
       Deskhop intercepts this combo instead of passing it through, then
       re-emits Left Super + L to BOTH outputs so the user's single keypress
       locks both screens at once. */
    {.modifier       = KEYBOARD_MODIFIER_LEFTGUI,
     .keys           = {HID_KEY_L},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &screenlock_hotkey_handler},

    /* Toggle gaming mode */
    {.modifier       = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_G},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &toggle_gaming_mode_handler},

    /* Enable screensaver pong for active output */
    {.modifier       = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_S},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &enable_screensaver_pong_hotkey_handler},

    /* Enable screensaver jitter for active output */
    {.modifier       = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_J},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &enable_screensaver_jitter_hotkey_handler},

    /* Disable screensaver for active output */
    {.modifier       = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_X},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &disable_screensaver_hotkey_handler},

    /* Erase stored config */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_F12, HID_KEY_D},
     .key_count      = 2,
     .acknowledge    = true,
     .action_handler = &wipe_config_hotkey_handler},

    /* Record switch y coordinate  */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_F12, HID_KEY_Y},
     .key_count      = 2,
     .acknowledge    = true,
     .action_handler = &screen_border_hotkey_handler},

    /* Switch to configuration mode  */
    {.modifier       = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTSHIFT,
     .keys           = {HID_KEY_C, HID_KEY_O},
     .key_count      = 2,
     .acknowledge    = true,
     .action_handler = &config_enable_hotkey_handler},

    /* Hold down left shift + right shift + F12 + A ==> firmware upgrade mode for board A (kbd) */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTSHIFT | KEYBOARD_MODIFIER_LEFTSHIFT,
     .keys           = {HID_KEY_A},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &fw_upgrade_hotkey_handler_A},

    /* Hold down left shift + right shift + F12 + B ==> firmware upgrade mode for board B (mouse) */
    {.modifier       = KEYBOARD_MODIFIER_RIGHTSHIFT | KEYBOARD_MODIFIER_LEFTSHIFT,
     .keys           = {HID_KEY_B},
     .key_count      = 1,
     .acknowledge    = true,
     .action_handler = &fw_upgrade_hotkey_handler_B}};

/* ============================================================ *
 * Detect if any hotkeys were pressed
 * ============================================================ */

/* Tries to find if the keyboard report contains key, returns true/false */
bool key_in_report(uint8_t key, const hid_keyboard_report_t *report) {
    for (int j = 0; j < KEYS_IN_USB_REPORT; j++) {
        if (key == report->keycode[j]) {
            return true;
        }
    }

    return false;
}

/* Check if the current report matches a specific hotkey passed on */
bool check_specific_hotkey(hotkey_combo_t keypress, const hid_keyboard_report_t *report) {
    /* We expect all modifiers specified to be detected in the report */
    if (keypress.modifier != (report->modifier & keypress.modifier))
        return false;

    for (int n = 0; n < keypress.key_count; n++) {
        if (!key_in_report(keypress.keys[n], report)) {
            return false;
        }
    }

    /* Getting here means all of the keys were found. */
    return true;
}

/* Go through the list of hotkeys, check if any of them match. */
hotkey_combo_t *check_all_hotkeys(hid_keyboard_report_t *report, device_t *state) {
    for (int n = 0; n < ARRAY_SIZE(hotkeys); n++) {
        if (check_specific_hotkey(hotkeys[n], report)) {
            return &hotkeys[n];
        }
    }

    return NULL;
}

/* ============================================================ *
 * Output-toggle pattern: double-tap Left Ctrl. Implemented separately
 * from the hotkeys[] table because it's a temporal pattern (timing
 * across multiple reports), not a single-report match.
 *
 * The Ctrl key is NOT consumed. The OS still sees every Ctrl press;
 * we just observe the pattern alongside.
 * ============================================================ */

#define DOUBLE_TAP_WINDOW_US 300000  /* 300 ms */

typedef enum {
    DT_IDLE,            /* nothing tracked */
    DT_FIRST_HELD,      /* clean LCtrl press in progress, waiting for release */
    DT_FIRST_RELEASED   /* clean LCtrl release recorded, waiting for second press */
} double_tap_state_t;

static struct {
    double_tap_state_t state;
    uint32_t           release_us;
    bool               prev_ctrl;   /* LCtrl bit in the previous report */
} dt_left_ctrl = { .state = DT_IDLE };

static bool report_keys_empty(const hid_keyboard_report_t *r) {
    for (int i = 0; i < KEYS_IN_USB_REPORT; i++) {
        if (r->keycode[i] != 0)
            return false;
    }
    return true;
}

/* Returns true if this report completes a double-tap of left Ctrl. */
static bool detect_output_toggle_patterns(const hid_keyboard_report_t *r,
                                          uint32_t now_us) {
    bool keys_empty = report_keys_empty(r);

    /* Detect: clean LCtrl press, clean release, second clean LCtrl press,
       all within DOUBLE_TAP_WINDOW_US. "Clean" means only LCtrl in the
       modifier byte and no keys held. Any deviation (another modifier
       added, another key pressed, Ctrl combined with anything) aborts
       the in-progress detection. */
    bool ctrl_bit  = (r->modifier & KEYBOARD_MODIFIER_LEFTCTRL) != 0;
    bool ctrl_solo = (r->modifier == KEYBOARD_MODIFIER_LEFTCTRL) && keys_empty;
    bool fully_empty = (r->modifier == 0) && keys_empty;
    bool dt_fired = false;

    switch (dt_left_ctrl.state) {
        case DT_IDLE:
            /* Require a press-edge: previous report had no LCtrl, this
               one has LCtrl alone. Prevents "Ctrl+C released, Ctrl
               still held alone" from starting a tap pattern. */
            if (ctrl_solo && !dt_left_ctrl.prev_ctrl) {
                dt_left_ctrl.state = DT_FIRST_HELD;
            }
            break;

        case DT_FIRST_HELD:
            if (fully_empty) {
                dt_left_ctrl.state = DT_FIRST_RELEASED;
                dt_left_ctrl.release_us = now_us;
            } else if (!ctrl_solo) {
                /* Another key or modifier joined LCtrl -- abandon. */
                dt_left_ctrl.state = DT_IDLE;
            }
            break;

        case DT_FIRST_RELEASED:
            if (ctrl_solo && !dt_left_ctrl.prev_ctrl) {
                if ((now_us - dt_left_ctrl.release_us) < DOUBLE_TAP_WINDOW_US) {
                    dt_left_ctrl.state = DT_IDLE;
                    dt_fired = true;
                } else {
                    /* Window expired; treat as fresh first press. */
                    dt_left_ctrl.state = DT_FIRST_HELD;
                }
            } else if (!fully_empty && !ctrl_solo) {
                /* Any other activity within the window -- abandon. */
                dt_left_ctrl.state = DT_IDLE;
            }
            break;
    }
    dt_left_ctrl.prev_ctrl = ctrl_bit;
    return dt_fired;
}

/* Apply runtime config overrides to the static hotkeys[] table. Right
   now this is just the screen-lock trigger combo (config-driven so the
   user can rebind via flash config / web UI). Call after load_config
   and after any config update. */
void apply_runtime_hotkey_overrides(device_t *state) {
    for (int n = 0; n < ARRAY_SIZE(hotkeys); n++) {
        if (hotkeys[n].action_handler == &screenlock_hotkey_handler) {
            hotkeys[n].modifier  = state->config.screen_lock_trigger_modifier;
            hotkeys[n].keys[0]   = state->config.screen_lock_trigger_keycode;
            hotkeys[n].key_count = 1;
            return;
        }
    }
}

/* ==================================================== *
 * Keyboard State Management
 * ==================================================== */

/* Update the keyboard state for a specific device */
void update_kbd_state(device_t *state, hid_keyboard_report_t *report, uint8_t device_idx) {
    /* Ensure device_idx is within bounds */
    if (device_idx >= MAX_DEVICES)
        return;

    /* Update the keyboard state for this device */
    memcpy(&state->local_kbd_states[device_idx], report, sizeof(hid_keyboard_report_t));

    /* Track the largest keyboard index we have */
    if (state->max_kbd_idx < device_idx)
        state->max_kbd_idx = device_idx;
}

/* Update the struct storing the state of the keyboard(s) connected to the other board */
void update_remote_kbd_state(device_t *state, hid_keyboard_report_t *report) {
    memcpy(&state->remote_kbd_state, report, sizeof(hid_keyboard_report_t));
}

/* Add keys from source to destination, avoiding duplicates */
static void add_keys(hid_keyboard_report_t *dest, const hid_keyboard_report_t *src) {
    for (uint8_t i = 0; i < KEYS_IN_USB_REPORT; i++) {
        uint8_t key = src->keycode[i];
        
        if (key == 0 || key_in_report(key, dest))
            continue;
            
        uint8_t *empty_slot = memchr(dest->keycode, 0, KEYS_IN_USB_REPORT);
        if (empty_slot)
            *empty_slot = key;
    }
}

/* Release all keys */
void release_all_keys(device_t *state) {
    memset(state->local_kbd_states, 0, sizeof(state->local_kbd_states));
    memset(&state->remote_kbd_state, 0, sizeof(hid_keyboard_report_t));
    
    static hid_keyboard_report_t empty_report = {0};
    queue_kbd_report(&empty_report, state);
}


/* Combine all keyboard states into a single report */
void combine_kbd_states(device_t *state, hid_keyboard_report_t *combined_report) {
    memset(combined_report, 0, sizeof(hid_keyboard_report_t));

    /* Combine all local keyboards up to max_kbd_idx */
    for (uint8_t i = 0; i <= state->max_kbd_idx; i++) {
        combined_report->modifier |= state->local_kbd_states[i].modifier;
        add_keys(combined_report, &state->local_kbd_states[i]);
    }
    
    /* Add remote keyboard */
    combined_report->modifier |= state->remote_kbd_state.modifier;
    add_keys(combined_report, &state->remote_kbd_state);
}

/* ==================================================== *
 * Keyboard Queue Section
 * ==================================================== */

void process_kbd_queue_task(device_t *state) {
    hid_keyboard_report_t report;

    /* If we're not connected, we have nowhere to send reports to. */
    if (!state->tud_connected)
        return;

    /* Peek first, if there is anything there... */
    if (!queue_try_peek(&state->kbd_queue, &report))
        return;

    /* If we are suspended, let's wake the host up */
    if (tud_suspended())
        tud_remote_wakeup();

    /* If it's not ok to send yet, we'll try on the next pass */
    if (!tud_hid_n_ready(ITF_NUM_HID))
        return;

    /* ... try sending it to the host, if it's successful */
    bool succeeded = tud_hid_keyboard_report(REPORT_ID_KEYBOARD, report.modifier, report.keycode);

    /* ... then we can remove it from the queue. Race conditions shouldn't happen [tm] */
    if (succeeded)
        queue_try_remove(&state->kbd_queue, &report);
}

void queue_kbd_report(hid_keyboard_report_t *report, device_t *state) {
    /* It wouldn't be fun to queue up a bunch of messages and then dump them all on host */
    if (!state->tud_connected)
        return;

    queue_try_add(&state->kbd_queue, report);
}

/* If keys need to go locally, queue packet to kbd queue, else send them through UART */
void send_key(hid_keyboard_report_t *report, device_t *state) {
    /* Create a combined report from all device states */
    hid_keyboard_report_t combined_report;
    combine_kbd_states(state, &combined_report);

    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        /* Queue the combined report */
        queue_kbd_report(&combined_report, state);
        state->last_activity[BOARD_ROLE] = time_us_64();
    } else {
        /* Send the combined report to ensure all keys are included */
        queue_packet((uint8_t *)&combined_report, KEYBOARD_REPORT_MSG, KBD_REPORT_LENGTH);
    }
}

/* Decide if consumer control reports go local or to the other board */
void send_consumer_control(uint8_t *raw_report, device_t *state) {
    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        queue_cc_packet(raw_report, state);
        state->last_activity[BOARD_ROLE] = time_us_64();
    } else {
        queue_packet((uint8_t *)raw_report, CONSUMER_CONTROL_MSG, CONSUMER_CONTROL_LENGTH);
    }
}

/* Decide if consumer control reports go local or to the other board */
void send_system_control(uint8_t *raw_report, device_t *state) {
    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        queue_system_packet(raw_report, state);
        state->last_activity[BOARD_ROLE] = time_us_64();
    } else {
        queue_packet((uint8_t *)raw_report, SYSTEM_CONTROL_MSG, SYSTEM_CONTROL_LENGTH);
    }
}

/* ==================================================== *
 * Parse and interpret the keys pressed on the keyboard
 * ==================================================== */

void process_keyboard_report(uint8_t *raw_report, int length, uint8_t itf, hid_interface_t *iface) {
    hid_keyboard_report_t new_report = {0};
    device_t *state                  = &global_state;
    hotkey_combo_t *hotkey           = NULL;

    if (length < KBD_REPORT_LENGTH)
        return;

    /* No more keys accepted if we're about to reboot */
    if (global_state.reboot_requested)
        return;

    extract_kbd_data(raw_report, length, itf, iface, &new_report);

    /* Update the keyboard state for this device */
    update_kbd_state(state, &new_report, itf);

    /* Check if any hotkey was pressed */
    hotkey = check_all_hotkeys(&new_report, state);

    /* ... and take appropriate action */
    if (hotkey != NULL) {
        /* Provide visual feedback we received the action */
        if (hotkey->acknowledge)
            blink_led(state);

        /* Execute the corresponding handler */
        hotkey->action_handler(state, &new_report);

        /* And pass the key to the output PC if configured to do so. */
        if (!hotkey->pass_to_os)
            return;
    }

    /* Double-tap-LCtrl output-toggle. Pass-through -- the Ctrl keys
       still go to the OS in send_key below, so Ctrl+anything shortcuts
       keep working. The pattern just triggers an output switch alongside. */
    if (detect_output_toggle_patterns(&new_report, time_us_32())) {
        blink_led(state);
        output_toggle_hotkey_handler(state, &new_report);
    }

    /* This method will decide if the key gets queued locally or sent through UART */
    send_key(&new_report, state);
}

void process_consumer_report(uint8_t *raw_report, int length, uint8_t itf, hid_interface_t *iface) {
    uint8_t new_report[CONSUMER_CONTROL_LENGTH] = {0};
    uint16_t *report_ptr = (uint16_t *)new_report;

    device_t *state = &global_state;
    keyboard_t *keyboard = get_keyboard(iface, raw_report[0]);

    /* If consumer control is variable, read the values from cc_array and send as array. */
    if (iface->consumer.is_variable) {
        for (int i = 0; i < MAX_CC_BUTTONS && i < 8 * (length - 1); i++) {
            int bit_idx = i % 8;
            int byte_idx = i >> 3;

            if ((raw_report[byte_idx + 1] >> bit_idx) & 1) {
                report_ptr[0] = keyboard->cc_array[i];
            }
        }
    }
    else {
        for (int i = 0; i < length - 1 && i < CONSUMER_CONTROL_LENGTH; i++)
            new_report[i] = raw_report[i + 1];
    }

    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        send_consumer_control(new_report, state);
    } else {
        queue_packet((uint8_t *)new_report, CONSUMER_CONTROL_MSG, CONSUMER_CONTROL_LENGTH);
    }
}

void process_system_report(uint8_t *raw_report, int length, uint8_t itf, hid_interface_t *iface) {
    uint16_t new_report = raw_report[1];
    uint8_t *report_ptr = (uint8_t *)&new_report;
    device_t *state = &global_state;

    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        send_system_control(report_ptr, state);
    } else {
        queue_packet(report_ptr, SYSTEM_CONTROL_MSG, SYSTEM_CONTROL_LENGTH);
    }
}

keyboard_t *get_keyboard(hid_interface_t *iface, uint8_t report_id) {
    /* When we have just one keyboard (most cases), or don't use report ID */
    if (iface->num_keyboards == 1 || !iface->uses_report_id)
        return &iface->keyboards[PRIMARY_KEYBOARD];

    /* Go through known keyboards and match on report ID, return pointer to keyboard_t */
    for (int i = 0; i < iface->num_keyboards && i < MAX_KEYBOARDS; i++) {
        if (iface->keyboards[i].report_id == report_id) {
            return &iface->keyboards[i];
        }
    }

    /* If nothing else is matched, return the primary keyboard. */
    return &iface->keyboards[PRIMARY_KEYBOARD];
}
