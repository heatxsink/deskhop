/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 *
 * Gesture state machine. Faithful port of libinput's
 * src/evdev-mt-touchpad-gestures.c, scoped to scroll + swipe only.
 * Pinch, hold, hold-and-motion, and 3FG-drag states are intentionally
 * deferred (out of scope for this branch).
 *
 * libinput original copyright (c) 2015 Red Hat, Inc., MIT licensed.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "magic_trackpad.h"

#ifdef DH_TRACKPAD_GESTURES

/* libinput src/evdev-mt-touchpad.h:157. State numbering preserved
   (matches libinput) so debugging side-by-side against libinput is
   easy. We only ever transition through the subset listed below; the
   skipped states (HOLD, PINCH_*, 3FG_DRAG_*) are present in the enum
   but unreachable in our port. */
typedef enum {
    GESTURE_STATE_NONE = 0,
    GESTURE_STATE_UNKNOWN,
    GESTURE_STATE_HOLD,                     /* skipped */
    GESTURE_STATE_HOLD_AND_MOTION,          /* skipped */
    GESTURE_STATE_POINTER_MOTION,
    GESTURE_STATE_SCROLL_START,
    GESTURE_STATE_SCROLL,
    GESTURE_STATE_PINCH_START,              /* skipped */
    GESTURE_STATE_PINCH,                    /* skipped */
    GESTURE_STATE_SWIPE_START,
    GESTURE_STATE_SWIPE,
    GESTURE_STATE_3FG_DRAG_OR_SWIPE_START,  /* skipped */
    GESTURE_STATE_3FG_DRAG_OR_SWIPE,        /* skipped */
    GESTURE_STATE_3FG_DRAG_START,           /* skipped */
    GESTURE_STATE_3FG_DRAG,                 /* skipped */
    GESTURE_STATE_3FG_DRAG_RELEASED,        /* skipped */
} tp_gesture_state_t;

/* libinput src/evdev-mt-touchpad-gestures.c:47. Same scoping caveat:
   PINCH_START, 3FG_DRAG_* events present in enum but never fired. */
typedef enum {
    GESTURE_EVENT_RESET = 0,
    GESTURE_EVENT_END,
    GESTURE_EVENT_CANCEL,
    GESTURE_EVENT_FINGER_DETECTED,
    GESTURE_EVENT_FINGER_SWITCH_TIMEOUT,
    GESTURE_EVENT_TAP_TIMEOUT,
    GESTURE_EVENT_HOLD_TIMEOUT,
    GESTURE_EVENT_HOLD_AND_MOTION_START,    /* skipped */
    GESTURE_EVENT_POINTER_MOTION_START,
    GESTURE_EVENT_SCROLL_START,
    GESTURE_EVENT_SWIPE_START,
    GESTURE_EVENT_PINCH_START,              /* skipped */
    GESTURE_EVENT_3FG_DRAG_OR_SWIPE_START,  /* skipped */
    GESTURE_EVENT_3FG_DRAG_OR_SWIPE_TIMEOUT,/* skipped */
    GESTURE_EVENT_3FG_DRAG_RELEASE_TIMEOUT, /* skipped */
} gesture_event_t;

/* Output bundle. Populated by tp_gesture_post_frame before returning to
   the caller. The caller drains motion / scroll / swipe in the same
   shapes Phase 1 produced. */
typedef enum {
    GESTURE_OUT_SWIPE_NONE = 0,
    GESTURE_OUT_SWIPE_LEFT,
    GESTURE_OUT_SWIPE_RIGHT,
    GESTURE_OUT_SWIPE_UP,
    GESTURE_OUT_SWIPE_DOWN,
} tp_gesture_swipe_dir_t;

typedef struct {
    int32_t move_x;
    int32_t move_y;
    int32_t wheel;
    int32_t pan;
    tp_gesture_swipe_dir_t swipe;
} tp_gesture_output_t;

/* Top-level gesture state. Translated from libinput tp_dispatch.gesture
   (mt-touchpad.h ~432), trimmed to fields the in-scope states actually
   read. */
typedef struct {
    bool                enabled;
    tp_gesture_state_t  state;
    uint32_t            finger_count;     /* current frame finger count */
    uint32_t            initial_time_us;  /* time of state-NONE -> UNKNOWN */
    uint32_t            timer_us;         /* polled deadline, 0 = inactive */

    /* Per-finger initial positions (recorded on UNKNOWN entry) so the
       direction detector can compute deltas without depending on Phase 1's
       prev_id tracking. Indexed by slot (id % MT_MAX_FINGERS). */
    int16_t             initial_x[MT_MAX_FINGERS];
    int16_t             initial_y[MT_MAX_FINGERS];
    bool                initial_recorded[MT_MAX_FINGERS];

    /* Last frame's per-finger position for delta-since-last accumulation
       (used by SCROLL state to feed wheel/pan). */
    int16_t             prev_x[MT_MAX_FINGERS];
    int16_t             prev_y[MT_MAX_FINGERS];
    bool                prev_valid[MT_MAX_FINGERS];

    /* Sub-pixel remainders for division, like Phase 1's scroll/pointer
       paths. Without these, slow finger motion truncates to zero forever. */
    int32_t             scroll_rem_y;
    int32_t             scroll_rem_x;
    int32_t             pointer_rem_x;
    int32_t             pointer_rem_y;

    /* SWIPE accumulation for direction lock + threshold. */
    int32_t             swipe_accum_x;
    int32_t             swipe_accum_y;
    bool                swipe_emitted;

    /* Output staging for the current frame. */
    tp_gesture_output_t out;
} tp_gesture_t;

/* libinput defaults, adapted. */
#define GESTURE_HOLD_TIMEOUT_US             180000UL  /* unused (HOLD skipped) */
#define GESTURE_FINGER_SWITCH_TIMEOUT_US    100000UL
#define GESTURE_SWIPE_TIMEOUT_US            150000UL

/* Movement threshold to leave UNKNOWN. Magic Trackpad 2 ~50 units/mm.
   1.5 mm = 75 units, squared = 5625. */
#define GESTURE_DIRECTION_THRESHOLD_SQ      5625L

/* SWIPE accumulated-distance threshold. ~1/4 pad width per finger
   summed across all fingers. Same value Phase 1's swipe code used; we
   inherit the muscle memory. */
#define GESTURE_SWIPE_THRESHOLD             1200

/* SCROLL/POINTER divisors: tuned for Magic Trackpad 2 raw units. */
#define GESTURE_POINTER_DIV   4
#define GESTURE_SCROLL_DIV    12

/* Initialize a gesture state to clean NONE. Call once at trackpad mount
   or whenever the trackpad re-enumerates. */
void tp_gesture_init(tp_gesture_t *g);

/* Feed a decoded multi-touch frame into the gesture state machine.
   Drives all internal events (FINGER_DETECTED, motion-start
   detection, etc.) and populates the output bundle for this frame.
   Returns true if any output is non-zero. */
bool tp_gesture_post_frame(tp_gesture_t *g,
                           const mt_frame_t *frame,
                           uint32_t now_us);

/* Advance internal timers (FINGER_SWITCH_TIMEOUT etc.) when no HID
   reports are arriving. Caller invokes from a low-frequency tick task,
   parallel to mt_gesture_idle_tick / mt_tap_idle_tick_task. */
void tp_gesture_idle_tick(tp_gesture_t *g, uint32_t now_us);

#endif /* DH_TRACKPAD_GESTURES */
