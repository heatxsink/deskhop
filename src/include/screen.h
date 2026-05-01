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

#include <stdint.h>


/*==============================================================================
 *  Constants
 *==============================================================================*/

#define MAX_SCREEN_COORD 32767
#define MIN_SCREEN_COORD 0

/*==============================================================================
 *  Data Structures
 *==============================================================================*/

typedef struct {
    int top;    // When jumping from a smaller to a bigger screen, go to THIS top height
    int bottom; // When jumping from a smaller to a bigger screen, go to THIS bottom
                // height
} border_size_t;

typedef struct {
    uint8_t mode;
    uint8_t only_if_inactive;
    uint64_t idle_time_us;
    uint64_t max_time_us;
} screensaver_t;

typedef struct {
    uint32_t number;           // Number of this output (e.g. OUTPUT_A = 0 etc)
    uint32_t screen_count;     // How many monitors per output (e.g. Output A is Windows with 3 monitors)
    uint32_t screen_index;     // Current active screen
    int32_t speed_x;           // Mouse speed per output, in direction X
    int32_t speed_y;           // Mouse speed per output, in direction Y
    border_size_t border;      // Screen border size/offset to keep cursor at same height when switching
    uint8_t os;                // Operating system on this output
    uint8_t pos;               // Screen position on this output
    uint8_t mouse_park_pos;    // Where the mouse goes after switch
    screensaver_t screensaver; // Screensaver parameters for this output

    /* Per-output screen-lock keystroke. When the user fires the lock
       hotkey (currently hardcoded as Super+L globally), deskhop emits
       this modifier+keycode pair to this output. Defaults to Super+L
       (0x08+0x0F), which works on Linux/Windows; macOS users typically
       set 0x09+0x14 (Ctrl+Cmd+Q). modifier is the standard HID
       modifier bitmask (KEYBOARD_MODIFIER_*). keycode is a HID usage. */
    uint8_t lock_modifier;
    uint8_t lock_keycode;
} output_t;
