/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * See magic_trackpad.h for protocol notes and references.
 */

#include "magic_trackpad.h"
#include <string.h>

#define MT_REPORT_ID  0x02

/* Sign-extend an n-bit integer (n <= 32). */
static inline int32_t sign_extend(int32_t value, int bits) {
    int32_t mask = 1 << (bits - 1);
    return (value ^ mask) - mask;
}

static void decode_finger(const uint8_t *t, mt_finger_t *out) {
    /* Linux: x = ((t[1] << 27) | (t[0] << 19)) >> 19  -- 13-bit signed.
       Equivalent: low 8 bits = t[0], next 5 bits = t[1] & 0x1F. */
    int32_t x_raw = ((int32_t)(t[1] & 0x1F) << 8) | t[0];
    int32_t x = sign_extend(x_raw, 13);

    /* Linux: y = -(((t[3] << 30) | (t[2] << 22) | (t[1] << 14)) >> 19).
       Equivalent: bits [12:11] = t[3] & 0x3, [10:3] = t[2], [2:0] = t[1] >> 5. */
    int32_t y_raw = ((int32_t)(t[3] & 0x3) << 11)
                  | ((int32_t)t[2] << 3)
                  | (t[1] >> 5);
    int32_t y = -sign_extend(y_raw, 13);

    out->x           = (int16_t)x;
    out->y           = (int16_t)y;
    out->touch_major = t[4];
    out->touch_minor = t[5];
    out->size        = t[6];
    out->pressure    = t[7];
    out->finger_id   = t[8] & 0x0F;
}

bool mt_decode_report(const uint8_t *report, int len, mt_frame_t *out) {
    if (report == NULL || out == NULL) return false;
    if (len < MT_HEADER_SIZE) return false;
    if (report[0] != MT_REPORT_ID) return false;

    int finger_bytes = len - MT_HEADER_SIZE;
    if (finger_bytes % MT_FINGER_SIZE != 0) return false;

    int n = finger_bytes / MT_FINGER_SIZE;
    if (n > MT_MAX_FINGERS) n = MT_MAX_FINGERS;

    out->timestamp    = (uint16_t)report[9] | ((uint16_t)report[10] << 8);
    out->finger_count = (uint8_t)n;

    for (int i = 0; i < n; i++) {
        decode_finger(&report[MT_HEADER_SIZE + i * MT_FINGER_SIZE], &out->fingers[i]);
    }
    return true;
}

void mt_gesture_init(mt_gesture_state_t *s) {
    memset(s, 0, sizeof(*s));
}

/* Find a finger in the previous frame by id. Returns its index, or -1. */
static int find_prev(mt_gesture_state_t *s, uint8_t id) {
    for (int i = 0; i < s->prev_count; i++) {
        if (s->prev_id[i] == id) return i;
    }
    return -1;
}

/* Trackpad coordinate units are quite fine. These divisors map raw deltas
   to mouse-pipeline units that feel similar to a regular mouse. Tuned by
   eye for now -- expect to revisit. */
#define POINTER_DIV  4
#define SCROLL_DIV   12

bool mt_gesture_step(mt_gesture_state_t *s, const mt_frame_t *frame,
                     int32_t *out_move_x, int32_t *out_move_y,
                     int32_t *out_wheel, int32_t *out_pan) {
    *out_move_x = 0;
    *out_move_y = 0;
    *out_wheel  = 0;
    *out_pan    = 0;

    bool emit = false;

    /* No fingers: just clear state, no signal. */
    if (frame->finger_count == 0) {
        s->prev_count = 0;
        s->have_prev  = false;
        return false;
    }

    /* If finger count changed since last frame, skip the delta this frame
       to avoid jumps. Update tracking and bail. */
    if (!s->have_prev || frame->finger_count != s->prev_count) {
        goto save_and_return;
    }

    if (frame->finger_count == 1) {
        int prev_idx = find_prev(s, frame->fingers[0].finger_id);
        if (prev_idx >= 0) {
            int32_t dx = frame->fingers[0].x - s->prev_x[prev_idx];
            int32_t dy = frame->fingers[0].y - s->prev_y[prev_idx];
            *out_move_x = dx / POINTER_DIV;
            /* Trackpad Y already negated by decoder so increasing Y means moving
               up the trackpad. For mouse movement we want screen-down to be +y,
               which on macOS/Linux maps inversely to physical finger motion. */
            *out_move_y = -dy / POINTER_DIV;
            emit = (*out_move_x != 0 || *out_move_y != 0);
        }
    } else if (frame->finger_count >= 2) {
        /* Average the two largest movers' deltas. For a stable scroll, both
           fingers move in roughly the same direction. */
        int32_t sum_dx = 0, sum_dy = 0;
        int matched = 0;
        for (int i = 0; i < 2; i++) {
            int prev_idx = find_prev(s, frame->fingers[i].finger_id);
            if (prev_idx < 0) continue;
            sum_dx += frame->fingers[i].x - s->prev_x[prev_idx];
            sum_dy += frame->fingers[i].y - s->prev_y[prev_idx];
            matched++;
        }
        if (matched == 2) {
            int32_t dx = sum_dx / 2;
            int32_t dy = sum_dy / 2;

            /* Wheel sign chosen empirically: natural-scroll on macOS expects
               two-finger swipe up to scroll page content up. Our decoder
               negates trackpad Y, so swipe-up gives positive dy; an unflipped
               wheel reads inverted on the host. Negate. */
            *out_wheel = -dy / SCROLL_DIV;
            *out_pan   = dx / SCROLL_DIV;
            emit = (*out_wheel != 0 || *out_pan != 0);
        }
    }

save_and_return:
    /* Save current frame as previous for the next call. */
    s->prev_count = (frame->finger_count > MT_MAX_FINGERS) ? MT_MAX_FINGERS : frame->finger_count;
    for (int i = 0; i < s->prev_count; i++) {
        s->prev_x[i]  = frame->fingers[i].x;
        s->prev_y[i]  = frame->fingers[i].y;
        s->prev_id[i] = frame->fingers[i].finger_id;
    }
    s->have_prev = true;
    return emit;
}
