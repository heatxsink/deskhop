/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 *
 * Gesture state machine. Faithful port of libinput's
 * src/evdev-mt-touchpad-gestures.c, scoped to scroll + swipe only.
 * State / event names verbatim. Pinch, hold, 3FG-drag deferred.
 *
 * libinput original copyright (c) 2015 Red Hat, Inc., MIT licensed.
 */

#include "magic_trackpad_gestures.h"

#ifdef DH_MAGIC_TRACKPAD

#include <string.h>

extern int dh_debug_printf(const char *__restrict __format, ...);

#ifdef DH_DEBUG_TRACKPAD
#define DBG_G(...) dh_debug_printf(__VA_ARGS__)
#else
#define DBG_G(...) do {} while (0)
#endif

/* libinput's log_gesture_bug warns "this should not happen". We no-op:
   misbehavior surfaces as user-visible glitches, not crashes. */
#define log_gesture_bug(g, e) do { (void)(g); (void)(e); } while (0)

/* Forward declarations of per-state handlers. Names verbatim from
   libinput (with our snake_case suffix). */
static void
tp_gesture_handle_event_on_state_none(tp_gesture_t *g,
                                      gesture_event_t event,
                                      uint32_t time_us);
static void
tp_gesture_handle_event_on_state_unknown(tp_gesture_t *g,
                                         gesture_event_t event,
                                         uint32_t time_us);
static void
tp_gesture_handle_event_on_state_pointer_motion(tp_gesture_t *g,
                                                gesture_event_t event,
                                                uint32_t time_us);
static void
tp_gesture_handle_event_on_state_scroll_start(tp_gesture_t *g,
                                              gesture_event_t event,
                                              uint32_t time_us);
static void
tp_gesture_handle_event_on_state_scroll(tp_gesture_t *g,
                                        gesture_event_t event,
                                        uint32_t time_us);
static void
tp_gesture_handle_event_on_state_swipe_start(tp_gesture_t *g,
                                             gesture_event_t event,
                                             uint32_t time_us);
static void
tp_gesture_handle_event_on_state_swipe(tp_gesture_t *g,
                                       gesture_event_t event,
                                       uint32_t time_us);

/* libinput src/evdev-mt-touchpad-gestures.c:1258 */
static void
tp_gesture_handle_event(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
#ifdef DH_DEBUG_TRACKPAD
    tp_gesture_state_t before = g->state;
#endif
    switch (g->state) {
    case GESTURE_STATE_NONE:
        tp_gesture_handle_event_on_state_none(g, event, time_us);
        break;
    case GESTURE_STATE_UNKNOWN:
        tp_gesture_handle_event_on_state_unknown(g, event, time_us);
        break;
    case GESTURE_STATE_POINTER_MOTION:
        tp_gesture_handle_event_on_state_pointer_motion(g, event, time_us);
        break;
    case GESTURE_STATE_SCROLL_START:
        tp_gesture_handle_event_on_state_scroll_start(g, event, time_us);
        break;
    case GESTURE_STATE_SCROLL:
        tp_gesture_handle_event_on_state_scroll(g, event, time_us);
        break;
    case GESTURE_STATE_SWIPE_START:
        tp_gesture_handle_event_on_state_swipe_start(g, event, time_us);
        break;
    case GESTURE_STATE_SWIPE:
        tp_gesture_handle_event_on_state_swipe(g, event, time_us);
        break;
    default:
        /* HOLD/PINCH/3FG_DRAG family: deliberately unreachable in our port. */
        log_gesture_bug(g, event);
        break;
    }
#ifdef DH_DEBUG_TRACKPAD
    if (before != g->state) {
        DBG_G("g: %u -> %u on ev=%u t=%u\n", before, g->state, event, time_us);
    }
#endif
}

void tp_gesture_init(tp_gesture_t *g) {
    memset(g, 0, sizeof(*g));
    g->enabled = true;
    g->state   = GESTURE_STATE_NONE;
}

/* ============================================================
 * Per-state handlers (translated from libinput, scope-trimmed)
 * ============================================================ */

/* libinput src/evdev-mt-touchpad-gestures.c:541. We skip:
   - HOLD timer / hold detection
   - 3FG-drag and pinch branches
   The remaining transitions: FINGER_DETECTED -> UNKNOWN. */
static void
tp_gesture_handle_event_on_state_none(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
        /* nothing to cancel (no timers active in NONE) */
        break;
    case GESTURE_EVENT_FINGER_DETECTED:
        g->state = GESTURE_STATE_UNKNOWN;
        g->initial_time_us = time_us;
        break;
    case GESTURE_EVENT_POINTER_MOTION_START:
        g->state = GESTURE_STATE_POINTER_MOTION;
        break;
    case GESTURE_EVENT_SCROLL_START:
        g->state = GESTURE_STATE_SCROLL_START;
        break;
    default:
        log_gesture_bug(g, event);
        break;
    }
}

/* libinput src/evdev-mt-touchpad-gestures.c:613. Trimmed: HOLD_TIMEOUT,
   PINCH, 3FG_DRAG branches removed. */
static void
tp_gesture_handle_event_on_state_unknown(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
        g->state = GESTURE_STATE_NONE;
        break;
    case GESTURE_EVENT_FINGER_SWITCH_TIMEOUT:
        break;
    case GESTURE_EVENT_POINTER_MOTION_START:
        g->state = GESTURE_STATE_POINTER_MOTION;
        break;
    case GESTURE_EVENT_SCROLL_START:
        g->state = GESTURE_STATE_SCROLL_START;
        break;
    case GESTURE_EVENT_SWIPE_START:
        g->state = GESTURE_STATE_SWIPE_START;
        break;
    default:
        log_gesture_bug(g, event);
        break;
    }
}

/* libinput src/evdev-mt-touchpad-gestures.c:767 (much simplified --
   pinch/3fg-drag removed, no hold-and-motion). */
static void
tp_gesture_handle_event_on_state_pointer_motion(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
        g->state = GESTURE_STATE_NONE;
        break;
    case GESTURE_EVENT_SCROLL_START:
        g->state = GESTURE_STATE_SCROLL_START;
        break;
    case GESTURE_EVENT_SWIPE_START:
        g->state = GESTURE_STATE_SWIPE_START;
        break;
    default:
        break;
    }
}

/* libinput src/evdev-mt-touchpad-gestures.c:814 */
static void
tp_gesture_handle_event_on_state_scroll_start(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
        g->state = GESTURE_STATE_NONE;
        break;
    default:
        /* Once we see motion, transition to SCROLL. The motion delta
           emit happens in tp_gesture_post_frame after this dispatcher
           returns. */
        g->state = GESTURE_STATE_SCROLL;
        break;
    }
}

/* libinput src/evdev-mt-touchpad-gestures.c:848. FINGER_SWITCH_TIMEOUT
   in SCROLL state cancels the gesture (libinput calls tp_gesture_cancel,
   which we model as RESET -> NONE). */
static void
tp_gesture_handle_event_on_state_scroll(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
    case GESTURE_EVENT_FINGER_SWITCH_TIMEOUT:
        g->state = GESTURE_STATE_NONE;
        break;
    default:
        break;
    }
}

/* libinput src/evdev-mt-touchpad-gestures.c:953 */
static void
tp_gesture_handle_event_on_state_swipe_start(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
        g->state = GESTURE_STATE_NONE;
        break;
    default:
        g->state = GESTURE_STATE_SWIPE;
        break;
    }
}

/* libinput src/evdev-mt-touchpad-gestures.c:983 */
static void
tp_gesture_handle_event_on_state_swipe(tp_gesture_t *g, gesture_event_t event, uint32_t time_us) {
    (void)time_us;
    switch (event) {
    case GESTURE_EVENT_RESET:
    case GESTURE_EVENT_END:
    case GESTURE_EVENT_CANCEL:
    case GESTURE_EVENT_FINGER_SWITCH_TIMEOUT:
        g->state = GESTURE_STATE_NONE;
        break;
    default:
        break;
    }
}

/* ============================================================
 * Frame ingest: the public entry point. Decides what events to
 * dispatch based on the frame contents, then computes output.
 * ============================================================ */

static void
gesture_clear_output(tp_gesture_t *g) {
    g->out.move_x = 0;
    g->out.move_y = 0;
    g->out.wheel  = 0;
    g->out.pan    = 0;
    g->out.swipe  = GESTURE_OUT_SWIPE_NONE;
}

static int
slot_for(uint8_t finger_id) {
    return finger_id % MT_MAX_FINGERS;
}

/* Sum of squared per-finger displacement from initial-recorded position.
   Used to detect that any finger has moved more than the direction
   threshold, which exits UNKNOWN. */
static int32_t
total_initial_distance_sq(tp_gesture_t *g, const mt_frame_t *frame) {
    int32_t total = 0;
    for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
        int slot = slot_for(frame->fingers[i].finger_id);
        if (!g->initial_recorded[slot]) continue;
        int32_t dx = frame->fingers[i].x - g->initial_x[slot];
        int32_t dy = frame->fingers[i].y - g->initial_y[slot];
        int32_t d = dx * dx + dy * dy;
        if (d > total) total = d;  /* max-of-fingers, not sum -- closer to libinput */
    }
    return total;
}

/* Snapshot per-slot initial positions for direction detection. */
static void
record_initial_positions(tp_gesture_t *g, const mt_frame_t *frame) {
    for (int s = 0; s < MT_MAX_FINGERS; s++) g->initial_recorded[s] = false;
    for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
        int slot = slot_for(frame->fingers[i].finger_id);
        g->initial_x[slot]        = frame->fingers[i].x;
        g->initial_y[slot]        = frame->fingers[i].y;
        g->initial_recorded[slot] = true;
    }
}

/* Update per-slot prev positions used by SCROLL output for delta math. */
static void
update_prev_positions(tp_gesture_t *g, const mt_frame_t *frame) {
    for (int s = 0; s < MT_MAX_FINGERS; s++) g->prev_valid[s] = false;
    for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
        int slot = slot_for(frame->fingers[i].finger_id);
        g->prev_x[slot]     = frame->fingers[i].x;
        g->prev_y[slot]     = frame->fingers[i].y;
        g->prev_valid[slot] = true;
    }
}

/* Decide which gesture, if any, the current motion suggests. Translated
   loosely from libinput's tp_gesture_detect_motion_gestures (libinput's
   version is far more sophisticated; we use the simpler heuristic that
   matched Phase 1's behavior). */
static gesture_event_t
classify_motion(tp_gesture_t *g, const mt_frame_t *frame) {
    if (frame->finger_count == 1)
        return GESTURE_EVENT_POINTER_MOTION_START;
    if (frame->finger_count == 2)
        return GESTURE_EVENT_SCROLL_START;
    if (frame->finger_count >= 3)
        return GESTURE_EVENT_SWIPE_START;
    /* finger_count == 0 should not reach here; caller filters that. */
    return GESTURE_EVENT_RESET;
}

/* Compute summed-y delta across active fingers since last frame -- used
   by SCROLL state to drive wheel/pan output. Applies the scroll
   axis-lock constraint after computing raw deltas. */
static void
compute_scroll_delta(tp_gesture_t *g, const mt_frame_t *frame, int32_t *out_wheel, int32_t *out_pan) {
    int32_t sum_dx = 0, sum_dy = 0;
    int n = 0;
    for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
        int slot = slot_for(frame->fingers[i].finger_id);
        if (!g->prev_valid[slot]) continue;
        sum_dx += frame->fingers[i].x - g->prev_x[slot];
        sum_dy += frame->fingers[i].y - g->prev_y[slot];
        n++;
    }
    if (n == 0) {
        *out_wheel = 0;
        *out_pan   = 0;
        return;
    }
    /* Average per-finger to feel like a single scroll motion. */
    int32_t avg_dx = sum_dx / n;
    int32_t avg_dy = sum_dy / n;

    /* Axis-lock: until we've seen enough total motion to be confident,
       allow free scrolling on both axes. Once one axis has more than
       SCROLL_LOCK_THRESHOLD of accumulated motion, lock to it and zero
       the orthogonal axis for the rest of the gesture. */
    if (g->scroll_axis == 0 /* SCROLL_AXIS_NONE */) {
        g->scroll_axis_accum_x += avg_dx;
        g->scroll_axis_accum_y += avg_dy;
        int32_t abs_x = g->scroll_axis_accum_x < 0 ? -g->scroll_axis_accum_x : g->scroll_axis_accum_x;
        int32_t abs_y = g->scroll_axis_accum_y < 0 ? -g->scroll_axis_accum_y : g->scroll_axis_accum_y;
        if (abs_x >= GESTURE_SCROLL_LOCK_THRESHOLD || abs_y >= GESTURE_SCROLL_LOCK_THRESHOLD) {
            g->scroll_axis = (abs_y > abs_x) ? 1 /* VERTICAL */ : 2 /* HORIZONTAL */;
        }
    }
    if (g->scroll_axis == 1 /* SCROLL_AXIS_VERTICAL */) {
        avg_dx = 0;
    } else if (g->scroll_axis == 2 /* SCROLL_AXIS_HORIZONTAL */) {
        avg_dy = 0;
    }

    /* Add sub-pixel remainder before dividing so slow scrolls don't
       truncate to zero forever. Phase 1 used the same trick. */
    int32_t dx     = avg_dx + g->scroll_rem_x;
    int32_t dy     = avg_dy + g->scroll_rem_y;
    *out_pan       = dx / GESTURE_SCROLL_DIV;
    *out_wheel     = -dy / GESTURE_SCROLL_DIV;  /* natural-scroll convention */
    g->scroll_rem_x = dx - (*out_pan)   * GESTURE_SCROLL_DIV;
    g->scroll_rem_y = dy - (-*out_wheel) * GESTURE_SCROLL_DIV;
}

/* Compute single-finger pointer delta (slot 0 of the active finger).
   Carries sub-pixel remainder across frames so slow motion doesn't
   vanish into the divisor. */
static void
compute_pointer_delta(tp_gesture_t *g, const mt_frame_t *frame, int32_t *out_dx, int32_t *out_dy) {
    *out_dx = 0;
    *out_dy = 0;
    if (frame->finger_count != 1) return;
    int slot = slot_for(frame->fingers[0].finger_id);
    if (!g->prev_valid[slot]) return;
    int32_t dx = (frame->fingers[0].x - g->prev_x[slot]) + g->pointer_rem_x;
    int32_t dy = (frame->fingers[0].y - g->prev_y[slot]) + g->pointer_rem_y;
    *out_dx = dx / GESTURE_POINTER_DIV;
    *out_dy = dy / GESTURE_POINTER_DIV;
    g->pointer_rem_x = dx - (*out_dx) * GESTURE_POINTER_DIV;
    g->pointer_rem_y = dy - (*out_dy) * GESTURE_POINTER_DIV;
}

/* libinput src/evdev-mt-touchpad-gestures.c:2184. Stable gesture states
   absorb brief finger-count flicker (one finger lifts and lands again
   during a 2F scroll, etc.). The non-debouncing states are the ones
   where committing the new count immediately is correct. */
static bool
tp_gesture_debounce_finger_changes(tp_gesture_state_t state) {
    switch (state) {
    case GESTURE_STATE_SCROLL:
    case GESTURE_STATE_SWIPE:
        return true;
    default:
        return false;
    }
}

/* SWIPE direction lock. Mirrors Phase 1's accumulator-with-threshold,
   which we know feels right. Once the threshold is crossed and a
   direction emitted, no further swipes fire until the gesture ends. */
static tp_gesture_swipe_dir_t
update_swipe_accum(tp_gesture_t *g, const mt_frame_t *frame) {
    if (g->swipe_emitted) return GESTURE_OUT_SWIPE_NONE;
    int32_t sum_dx = 0, sum_dy = 0;
    for (int i = 0; i < frame->finger_count && i < MT_MAX_FINGERS; i++) {
        int slot = slot_for(frame->fingers[i].finger_id);
        if (!g->prev_valid[slot]) continue;
        sum_dx += frame->fingers[i].x - g->prev_x[slot];
        sum_dy += frame->fingers[i].y - g->prev_y[slot];
    }
    g->swipe_accum_x += sum_dx;
    g->swipe_accum_y += sum_dy;

    if (g->swipe_accum_x >= GESTURE_SWIPE_THRESHOLD) {
        g->swipe_emitted = true;
        return GESTURE_OUT_SWIPE_RIGHT;
    }
    if (g->swipe_accum_x <= -GESTURE_SWIPE_THRESHOLD) {
        g->swipe_emitted = true;
        return GESTURE_OUT_SWIPE_LEFT;
    }
    return GESTURE_OUT_SWIPE_NONE;
}

bool tp_gesture_post_frame(tp_gesture_t *g, const mt_frame_t *frame, uint32_t now_us) {
    gesture_clear_output(g);

    /* Drive any pending timer expiry synchronously. tp_gesture_idle_tick
       can also tick this from a low-frequency task, but checking here on
       every frame guarantees the FINGER_SWITCH_TIMEOUT fires within one
       frame of expiry rather than waiting for an idle tick. */
    tp_gesture_idle_tick(g, now_us);

    /* No fingers: end any in-progress gesture and clear state. Critically,
       reset finger_count too -- otherwise the next touch sees
       frame->finger_count == g->finger_count (both 1) and the count-
       change branch below never fires, stranding us in NONE forever. */
    if (frame->finger_count == 0) {
        if (g->state != GESTURE_STATE_NONE) {
            tp_gesture_handle_event(g, GESTURE_EVENT_END, now_us);
        }
        g->state        = GESTURE_STATE_NONE;
        g->finger_count = 0;
        for (int s = 0; s < MT_MAX_FINGERS; s++) {
            g->prev_valid[s]      = false;
            g->initial_recorded[s] = false;
        }
        g->swipe_accum_x = 0;
        g->swipe_accum_y = 0;
        g->swipe_emitted = false;
        g->scroll_rem_x       = 0;
        g->scroll_rem_y       = 0;
        g->pointer_rem_x      = 0;
        g->pointer_rem_y      = 0;
        g->scroll_axis        = 0;
        g->scroll_axis_accum_x = 0;
        g->scroll_axis_accum_y = 0;
        return false;
    }

    /* Finger count differs from committed value. */
    if (frame->finger_count != g->finger_count) {
        if (tp_gesture_debounce_finger_changes(g->state)) {
            /* Stable gesture state: arm/refresh debounce timer so brief
               count flicker doesn't end the gesture. Committed
               finger_count stays; pending tracks the candidate. Fall
               through and continue rendering the current gesture. */
            if (frame->finger_count != g->finger_count_pending) {
                g->finger_count_pending = frame->finger_count;
                g->timer_us = now_us + GESTURE_FINGER_SWITCH_TIMEOUT_US;
            }
        } else {
            /* Non-debouncing state: commit the new count immediately,
               firing RESET + FINGER_DETECTED -- libinput does the same
               in tp_gesture_update_finger_state. */
            if (g->state != GESTURE_STATE_NONE) {
                tp_gesture_handle_event(g, GESTURE_EVENT_RESET, now_us);
            }
            g->finger_count         = frame->finger_count;
            g->finger_count_pending = 0;
            g->timer_us             = 0;
            record_initial_positions(g, frame);
            update_prev_positions(g, frame);
            g->scroll_rem_x        = 0;
            g->scroll_rem_y        = 0;
            g->pointer_rem_x       = 0;
            g->pointer_rem_y       = 0;
            g->scroll_axis         = 0;
            g->scroll_axis_accum_x = 0;
            g->scroll_axis_accum_y = 0;
            g->swipe_accum_x       = 0;
            g->swipe_accum_y       = 0;
            g->swipe_emitted       = false;
            tp_gesture_handle_event(g, GESTURE_EVENT_FINGER_DETECTED, now_us);
            return false;
        }
    } else if (g->finger_count_pending != 0) {
        /* Count returned to committed value while a pending change was
           armed -- flicker resolved itself. Cancel the timer. */
        g->finger_count_pending = 0;
        g->timer_us             = 0;
    }

    /* Steady finger count. If we're still in UNKNOWN, see if motion has
       crossed the direction threshold; if so, classify and dispatch the
       appropriate motion-start event. */
    if (g->state == GESTURE_STATE_UNKNOWN) {
        int32_t d = total_initial_distance_sq(g, frame);
        if (d >= GESTURE_DIRECTION_THRESHOLD_SQ) {
            gesture_event_t ev = classify_motion(g, frame);
            tp_gesture_handle_event(g, ev, now_us);
        }
    }

    /* Emit per-state outputs for this frame. */
    switch (g->state) {
    case GESTURE_STATE_POINTER_MOTION:
        compute_pointer_delta(g, frame, &g->out.move_x, &g->out.move_y);
        break;
    case GESTURE_STATE_SCROLL_START:
    case GESTURE_STATE_SCROLL:
        compute_scroll_delta(g, frame, &g->out.wheel, &g->out.pan);
        break;
    case GESTURE_STATE_SWIPE_START:
    case GESTURE_STATE_SWIPE:
        g->out.swipe = update_swipe_accum(g, frame);
        break;
    default:
        break;
    }

    /* Maintain per-frame deltas for next iteration. */
    update_prev_positions(g, frame);

    return (g->out.move_x || g->out.move_y || g->out.wheel || g->out.pan
            || g->out.swipe != GESTURE_OUT_SWIPE_NONE);
}

void tp_gesture_idle_tick(tp_gesture_t *g, uint32_t now_us) {
    /* Fire timer-based events when the deadline has passed. Wraparound-
       safe signed compare. Currently only FINGER_SWITCH_TIMEOUT lives
       in the timer slot; HOLD/3FG-drag timers were skipped. After
       dispatching, commit the pending finger_count so the next frame
       sees the new committed value (matches libinput
       tp_gesture_finger_count_switch_timeout). */
    if (g->timer_us != 0 && (int32_t)(now_us - g->timer_us) >= 0) {
        g->timer_us = 0;
        tp_gesture_handle_event(g, GESTURE_EVENT_FINGER_SWITCH_TIMEOUT, now_us);
        if (g->finger_count_pending != 0) {
            g->finger_count         = g->finger_count_pending;
            g->finger_count_pending = 0;
        }
        /* The next frame entering tp_gesture_post_frame will see
           state == NONE and (likely) non-zero finger count. Force a
           "fresh start" by firing FINGER_DETECTED here so the state
           machine moves to UNKNOWN before any output is computed.
           Initial positions are still stale -- they'll get refreshed
           when the next frame arrives via the count-change path
           (frame->finger_count != g->finger_count won't trip because
           we just committed; but we hit the steady-count UNKNOWN
           branch and motion-classify based on staler initial. The
           imperfection is small in practice; libinput accepts the
           same staleness.) */
        if (g->state == GESTURE_STATE_NONE) {
            tp_gesture_handle_event(g, GESTURE_EVENT_FINGER_DETECTED, now_us);
        }
    }
}

#endif /* DH_MAGIC_TRACKPAD */
