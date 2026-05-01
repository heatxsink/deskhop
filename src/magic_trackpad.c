/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * See magic_trackpad.h for protocol notes and references.
 */

#include "magic_trackpad.h"
#include <string.h>

#ifdef DH_TRACKPAD_TAP_TO_CLICK
#include "magic_trackpad_tap.h"
/* One static tap state for the (single) trackpad. mt_gesture_state_t
   carries a void* pointing here so the header doesn't need to know about
   the libinput-port internals when the flag is off. */
static tp_tap_t mt_tap_state;
#endif

#ifdef DH_TRACKPAD_GESTURES
#include "magic_trackpad_gestures.h"
/* Same pattern as the tap state above: one static instance for the
   (single) trackpad. Path G replaces Phase 1's scroll/swipe paths in
   mt_gesture_step when this flag is on. */
static tp_gesture_t mt_gesture_state_path_g;
#endif

#if defined(DH_TRACKPAD_TAP_TO_CLICK) || defined(DH_TRACKPAD_GESTURES)

extern int dh_debug_printf(const char *__restrict __format, ...);
#ifdef DH_DEBUG_TRACKPAD
#define DBG_GS(...) dh_debug_printf(__VA_ARGS__)
#else
#define DBG_GS(...) do {} while (0)
#endif
#endif

#define MT_REPORT_ID  0x02

/* Sign-extend an n-bit integer (n <= 32). */
static inline int32_t sign_extend(int32_t value, int bits) {
    int32_t mask = 1 << (bits - 1);
    return (value ^ mask) - mask;
}

/* For Magic Trackpad 2 USB the per-finger touch state lives in the top
   2 bits of byte 3 (the same byte whose low 2 bits encode the high bits
   of the y coordinate). The contact-down condition is exactly state ==
   0x80 -- 0x00/0x40/0xC0 are not actively touching. Reference:
   drivers/hid/hid-magicmouse.c, USB_DEVICE_ID_APPLE_MAGICTRACKPAD2 path:
       state = tdata[3] & 0xC0;
       down  = state == 0x80;
   Byte 8 is (orientation << 5) | (id & 0xf), not state -- that was the
   source of the long phantom-contact bug. */
#define MT_TOUCH_STATE_MASK  0xC0
#define MT_TOUCH_STATE_DOWN  0x80

static bool finger_is_down(const uint8_t *t) {
    return (t[3] & MT_TOUCH_STATE_MASK) == MT_TOUCH_STATE_DOWN;
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

    int n_raw = finger_bytes / MT_FINGER_SIZE;
    if (n_raw > MT_MAX_FINGERS) n_raw = MT_MAX_FINGERS;

    out->timestamp = (uint16_t)report[9] | ((uint16_t)report[10] << 8);

    /* Filter out fingers in TOUCH_STATE_NONE (state high-nibble == 0).
       The trackpad continues to include lifted fingers in frames briefly
       after lift to signal the lift event. We exclude them so finger_count
       reflects actual contacts; the state-machine's "ID gone from frame"
       check then correctly detects lifts. */
    int n = 0;
    for (int i = 0; i < n_raw; i++) {
        const uint8_t *t = &report[MT_HEADER_SIZE + i * MT_FINGER_SIZE];
        if (!finger_is_down(t)) continue;
        decode_finger(t, &out->fingers[n]);
        n++;
    }
    out->finger_count = (uint8_t)n;
    return true;
}

void mt_gesture_init(mt_gesture_state_t *s) {
    memset(s, 0, sizeof(*s));
#ifdef DH_TRACKPAD_TAP_TO_CLICK
    tp_tap_init(&mt_tap_state);
    s->tap = &mt_tap_state;
#endif
#ifdef DH_TRACKPAD_GESTURES
    tp_gesture_init(&mt_gesture_state_path_g);
    s->gesture = &mt_gesture_state_path_g;
#endif
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

/* 3-finger horizontal swipe must accumulate this much summed trackpad-x
   distance (across all three fingers) in one direction before it fires.
   Working in raw sum (not per-finger average) avoids integer-division
   truncation eating tiny per-frame contributions. ~1/4 pad width per finger. */
#define SWIPE_THRESHOLD_X  1200

bool mt_gesture_step(mt_gesture_state_t *s, const mt_frame_t *frame,
                     mt_button_t button_held,
                     uint32_t now_us,
                     int32_t *out_move_x, int32_t *out_move_y,
                     int32_t *out_wheel, int32_t *out_pan,
                     mt_swipe_t *out_swipe) {
    *out_move_x = 0;
    *out_move_y = 0;
    *out_wheel  = 0;
    *out_pan    = 0;
    *out_swipe  = MT_SWIPE_NONE;

    bool emit = false;

#ifdef DH_TRACKPAD_TAP_TO_CLICK
    tp_tap_t *tap = (tp_tap_t *)s->tap;
    if (tap) {
        /* Tap event-gen uses tap->touches[slot].state as the "is this
           finger currently tracked" indicator -- decoupled from the
           motion logic's prev_id list below. The idle tick can
           synthesize RELEASE events by clearing this state without
           breaking the motion tracker's frame-to-frame deltas. */

        /* TIMER: synthesize a TIMEOUT event if the deadline has passed.
           Wraparound-safe signed compare. */
        if (tap->timer_us != 0 && (int32_t)(now_us - tap->timer_us) >= 0) {
            tap->timer_us = 0;
            tp_tap_handle_event(tap, 0, TAP_EVENT_TIMEOUT, now_us);
        }

        /* TOUCH events for fingers whose slot is IDLE, MOTION events for
           fingers whose slot is in TOUCH and have moved past threshold.
           Slot = Apple finger_id mod MT_MAX_FINGERS. */
        for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
            uint8_t id = frame->fingers[i].finger_id;
            uint8_t slot = id % MT_MAX_FINGERS;
            if (tap->touches[slot].state == TAP_TOUCH_STATE_IDLE) {
                tap->touches[slot].state     = TAP_TOUCH_STATE_TOUCH;
                tap->touches[slot].initial_x = frame->fingers[i].x;
                tap->touches[slot].initial_y = frame->fingers[i].y;
                tap->touches[slot].is_thumb  = false;
                tap->touches[slot].is_palm   = false;
                if (tap->nfingers_down < MT_MAX_FINGERS) tap->nfingers_down++;
                DBG_GS("gs:T s=%u id=%u\n", slot, id);
                tp_tap_handle_event(tap, slot, TAP_EVENT_TOUCH, now_us);
            } else if (tap->touches[slot].state == TAP_TOUCH_STATE_TOUCH) {
                int32_t dx = frame->fingers[i].x - tap->touches[slot].initial_x;
                int32_t dy = frame->fingers[i].y - tap->touches[slot].initial_y;
                int32_t dist_sq = dx * dx + dy * dy;
                if (dist_sq > DEFAULT_TAP_MOVE_THRESHOLD_SQ) {
                    tp_tap_handle_event(tap, slot, TAP_EVENT_MOTION, now_us);
                }
            }
        }

        /* RELEASE events for any slot that's tracked but has no
           corresponding finger in the current frame. */
        for (int slot = 0; slot < MT_MAX_FINGERS; slot++) {
            if (tap->touches[slot].state == TAP_TOUCH_STATE_IDLE) continue;
            bool found = false;
            for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
                if ((frame->fingers[i].finger_id % MT_MAX_FINGERS) == slot) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (tap->nfingers_down) tap->nfingers_down--;
                DBG_GS("gs:R s=%d\n", slot);
                tp_tap_handle_event(tap, (uint8_t)slot, TAP_EVENT_RELEASE, now_us);
                tap->touches[slot].state = TAP_TOUCH_STATE_IDLE;
            }
        }

        /* BUTTON events on physical click rising/falling edge. */
        if (button_held != s->prev_button_held) {
            tp_tap_handle_event(tap, 0, TAP_EVENT_BUTTON, now_us);
        }
        s->prev_button_held = (uint8_t)button_held;
    }
#else
    (void)now_us;  /* unused when tap-to-click disabled */
#endif

    /* Mark this MT frame's arrival. Used by the idle tick to detect when
       the trackpad has stopped transmitting (i.e. all fingers have lifted)
       and synthesize the RELEASE events the state machine needs to fire
       a tap. */
    s->last_mt_frame_us = now_us;

#ifdef DH_TRACKPAD_GESTURES
    /* Path G: feed the frame to libinput's ported gesture state machine
       and return its output bundle directly. Phase 1's hand-rolled
       gesture logic below is short-circuited entirely when this flag
       is on. */
    {
        tp_gesture_t *g = (tp_gesture_t *)s->gesture;
        if (g) {
            bool g_emit = tp_gesture_post_frame(g, frame, now_us);
            *out_move_x = g->out.move_x;
            *out_move_y = g->out.move_y;
            *out_wheel  = g->out.wheel;
            *out_pan    = g->out.pan;
            switch (g->out.swipe) {
            case GESTURE_OUT_SWIPE_LEFT:  *out_swipe = MT_SWIPE_LEFT;  break;
            case GESTURE_OUT_SWIPE_RIGHT: *out_swipe = MT_SWIPE_RIGHT; break;
            default: *out_swipe = MT_SWIPE_NONE; break;
            }
            return g_emit;
        }
    }
#endif

    /* No fingers: just clear state, no signal. */
    if (frame->finger_count == 0) {
        s->prev_count = 0;
        s->have_prev  = false;
        s->swipe_accum_x = 0;
        s->swipe_emitted = false;
        return false;
    }

    /* If finger count changed since last frame, skip the delta this frame
       to avoid jumps. Update tracking and bail. Always reset 3-finger swipe
       and pointer-remainder state on a transition -- a fresh gesture should
       arm cleanly without carrying leftover sub-pixel motion across modes. */
    if (!s->have_prev || frame->finger_count != s->prev_count) {
        s->swipe_accum_x = 0;
        s->swipe_emitted = false;
        s->pointer_rem_x = 0;
        s->pointer_rem_y = 0;
        goto save_and_return;
    }

    if (frame->finger_count == 1) {
        int prev_idx = find_prev(s, frame->fingers[0].finger_id);
        if (prev_idx >= 0) {
            /* Add carried remainder before dividing so slow movements don't
               truncate to zero forever. Save the new remainder for next frame. */
            int32_t dx = (frame->fingers[0].x - s->prev_x[prev_idx]) + s->pointer_rem_x;
            int32_t dy = (frame->fingers[0].y - s->prev_y[prev_idx]) + s->pointer_rem_y;
            *out_move_x = dx / POINTER_DIV;
            *out_move_y = dy / POINTER_DIV;
            s->pointer_rem_x = dx - (*out_move_x) * POINTER_DIV;
            s->pointer_rem_y = dy - (*out_move_y) * POINTER_DIV;
            emit = (*out_move_x != 0 || *out_move_y != 0);
        }
    } else if (frame->finger_count == 2) {
        if (button_held == MT_BTN_LEFT) {
            /* Drag mode: cursor follows the SECOND finger. The first finger is
               anchoring the click; user adds a second finger to drag. */
            int prev_idx = find_prev(s, frame->fingers[1].finger_id);
            if (prev_idx >= 0) {
                int32_t dx = (frame->fingers[1].x - s->prev_x[prev_idx]) + s->pointer_rem_x;
                int32_t dy = (frame->fingers[1].y - s->prev_y[prev_idx]) + s->pointer_rem_y;
                *out_move_x = dx / POINTER_DIV;
                *out_move_y = dy / POINTER_DIV;
                s->pointer_rem_x = dx - (*out_move_x) * POINTER_DIV;
                s->pointer_rem_y = dy - (*out_move_y) * POINTER_DIV;
                emit = (*out_move_x != 0 || *out_move_y != 0);
            }
        } else if (button_held == MT_BTN_RIGHT) {
            /* Right click held: don't emit any motion. Cursor should stay put
               while the right-click context menu is open. */
        } else {
            /* No click held: scroll. Average the two fingers' deltas. */
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
    } else if (frame->finger_count == 3) {
        /* 3-finger swipe: accumulate the per-frame summed dx across all three
           fingers. Fire once per gesture when the threshold is crossed. */
        int32_t sum_dx = 0;
        int matched = 0;
        for (int i = 0; i < 3; i++) {
            int prev_idx = find_prev(s, frame->fingers[i].finger_id);
            if (prev_idx < 0) continue;
            sum_dx += frame->fingers[i].x - s->prev_x[prev_idx];
            matched++;
        }
        if (matched == 3) {
            s->swipe_accum_x += sum_dx;
            if (!s->swipe_emitted) {
                if (s->swipe_accum_x >= SWIPE_THRESHOLD_X) {
                    *out_swipe = MT_SWIPE_RIGHT;
                    s->swipe_emitted = true;
                } else if (s->swipe_accum_x <= -SWIPE_THRESHOLD_X) {
                    *out_swipe = MT_SWIPE_LEFT;
                    s->swipe_emitted = true;
                }
            }
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

bool mt_gesture_idle_tick(mt_gesture_state_t *s, uint32_t now_us) {
#ifdef DH_TRACKPAD_TAP_TO_CLICK
    tp_tap_t *tap = (tp_tap_t *)s->tap;
    if (!tap) return false;

    bool had_pending = (tap->pending_count > 0);

    /* Timer expiry. Wraparound-safe signed compare. */
    if (tap->timer_us != 0 && (int32_t)(now_us - tap->timer_us) >= 0) {
        tap->timer_us = 0;
        tp_tap_handle_event(tap, 0, TAP_EVENT_TIMEOUT, now_us);
    }

    /* Inactivity-based RELEASE synthesis. The trackpad goes silent when
       all fingers lift -- there's no terminator frame, so the only way
       to detect the lift is "no MT frames for a while." Threshold is
       conservative (200 ms) to avoid firing during slow finger movement
       where the trackpad's report rate naturally drops. Motion logic
       (s->prev_id, prev_count) is untouched -- this only flips the
       per-slot tap state. */
    #define MT_TAP_LIFT_INACTIVE_US 200000UL
    if (tap->nfingers_down > 0 &&
        (int32_t)(now_us - s->last_mt_frame_us) >= MT_TAP_LIFT_INACTIVE_US) {
        DBG_GS("gs:IR\n");
        for (int slot = 0; slot < MT_MAX_FINGERS; slot++) {
            if (tap->touches[slot].state == TAP_TOUCH_STATE_IDLE) continue;
            if (tap->nfingers_down) tap->nfingers_down--;
            tp_tap_handle_event(tap, (uint8_t)slot, TAP_EVENT_RELEASE, now_us);
            tap->touches[slot].state = TAP_TOUCH_STATE_IDLE;
        }
    }

    return had_pending || (tap->pending_count > 0);
#else
    (void)s; (void)now_us;
    return false;
#endif
}
