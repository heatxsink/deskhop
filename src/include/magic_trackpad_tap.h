/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 *
 * Tap state machine. Faithfully translated from libinput's
 * src/evdev-mt-touchpad-tap.c (MIT licensed). State names, event names,
 * and handler-function names are kept verbatim so libinput's source
 * can be read side-by-side as the canonical reference. Do not rename,
 * collapse, or "simplify" -- the corner-case decisions encoded in
 * libinput's structure are the whole point.
 *
 * libinput original copyright:
 *   Copyright (c) 2013-2015 Red Hat, Inc.
 *   Permission to use under MIT terms (see libinput LICENSE).
 *
 * Compile-time gated by DH_TRACKPAD_TAP_TO_CLICK; default builds do
 * not include this state machine.
 */

#ifndef MAGIC_TRACKPAD_TAP_H_
#define MAGIC_TRACKPAD_TAP_H_

#include <stdint.h>
#include <stdbool.h>
#include "magic_trackpad.h"

#ifdef DH_TRACKPAD_TAP_TO_CLICK

/* libinput src/evdev-mt-touchpad.h:105 */
typedef enum {
    TAP_STATE_IDLE = 4,
    TAP_STATE_TOUCH,
    TAP_STATE_HOLD,
    TAP_STATE_1FGTAP_TAPPED,
    TAP_STATE_2FGTAP_TAPPED,
    TAP_STATE_3FGTAP_TAPPED,
    TAP_STATE_TOUCH_2,
    TAP_STATE_TOUCH_2_HOLD,
    TAP_STATE_TOUCH_2_RELEASE,
    TAP_STATE_TOUCH_3,
    TAP_STATE_TOUCH_3_HOLD,
    TAP_STATE_TOUCH_3_RELEASE,
    TAP_STATE_TOUCH_3_RELEASE_2,
    TAP_STATE_1FGTAP_DRAGGING_OR_DOUBLETAP,
    TAP_STATE_2FGTAP_DRAGGING_OR_DOUBLETAP,
    TAP_STATE_3FGTAP_DRAGGING_OR_DOUBLETAP,
    TAP_STATE_1FGTAP_DRAGGING_OR_TAP,
    TAP_STATE_2FGTAP_DRAGGING_OR_TAP,
    TAP_STATE_3FGTAP_DRAGGING_OR_TAP,
    TAP_STATE_1FGTAP_DRAGGING,
    TAP_STATE_2FGTAP_DRAGGING,
    TAP_STATE_3FGTAP_DRAGGING,
    TAP_STATE_1FGTAP_DRAGGING_WAIT,
    TAP_STATE_2FGTAP_DRAGGING_WAIT,
    TAP_STATE_3FGTAP_DRAGGING_WAIT,
    TAP_STATE_1FGTAP_DRAGGING_2,
    TAP_STATE_2FGTAP_DRAGGING_2,
    TAP_STATE_3FGTAP_DRAGGING_2,
    TAP_STATE_DEAD,                  /**< finger count exceeded */
} tp_tap_state_t;

/* libinput src/evdev-mt-touchpad.h:137 */
typedef enum {
    TAP_TOUCH_STATE_IDLE = 16,       /**< not in touch */
    TAP_TOUCH_STATE_TOUCH,           /**< touching, may tap */
    TAP_TOUCH_STATE_DEAD,            /**< exceeded motion/timeout */
} tp_tap_touch_state_t;

/* libinput src/evdev-mt-touchpad-tap.c:35 */
typedef enum {
    TAP_EVENT_TOUCH = 12,
    TAP_EVENT_MOTION,
    TAP_EVENT_RELEASE,
    TAP_EVENT_BUTTON,
    TAP_EVENT_TIMEOUT,
    TAP_EVENT_THUMB,
    TAP_EVENT_PALM,
    TAP_EVENT_PALM_UP,
} tap_event_t;

/* Per-touch state. Translated from libinput tp_touch.tap (mt-touchpad.h:254). */
typedef struct {
    tp_tap_touch_state_t state;
    int16_t              initial_x;
    int16_t              initial_y;
    bool                 is_thumb;
    bool                 is_palm;
} tp_touch_tap_t;

/* Pending button events. State handlers call tp_tap_post_button() which
   appends here; the caller drains after each event dispatch and emits the
   reports through deskhop's mouse pipeline. Capacity > realistic max
   per-event burst (a tap-and-release is 2; tap-then-doubletap-followed-by-
   drag-end is at most 4-5). 8 is comfortable. */
typedef struct {
    uint32_t button;        /* HID button bitmask: 0x01 left, 0x02 right, 0x04 middle */
    bool     pressed;
    uint32_t time_us;
} tp_tap_button_event_t;

#define TP_TAP_PENDING_MAX 8

/* Top-level tap state. Translated from libinput tp_dispatch.tap
   (mt-touchpad.h:435). Omits libinput's config and drag_3fg fields --
   added later if needed. */
typedef struct {
    tp_tap_state_t state;
    bool           enabled;
    bool           drag_enabled;

    uint32_t       timer_us;          /* when timer fires; 0 = inactive */
    uint32_t       saved_press_us;
    uint32_t       saved_release_us;
    uint32_t       buttons_pressed;   /* tracks emitted button state */
    uint8_t        nfingers_down;     /* fingers down for tapping (excl thumb/palm) */

    tp_touch_tap_t touches[MT_MAX_FINGERS];

    /* Output queue. Populated by state handlers; drained by the caller. */
    tp_tap_button_event_t pending[TP_TAP_PENDING_MAX];
    uint8_t               pending_count;
} tp_tap_t;

/* libinput defaults, src/evdev-mt-touchpad-tap.c:32 */
#define DEFAULT_TAP_TIMEOUT_PERIOD_US             180000UL  /* 180 ms */
#define DEFAULT_DRAG_TIMEOUT_PERIOD_BASE_US       160000UL  /* 160 ms */
#define DEFAULT_DRAG_TIMEOUT_PERIOD_PERFINGER_US   20000UL  /*  20 ms */
#define DEFAULT_DRAGLOCK_TIMEOUT_PERIOD_US        300000UL  /* 300 ms */

/* DEFAULT_TAP_MOVE_THRESHOLD = 1.3 mm in libinput. Apple Magic Trackpad
   reports in its own coordinate units (~13-bit signed range across the pad,
   pad is ~16 cm wide). 1.3 mm is roughly 1.3/160 of pad width, applied to a
   ~8000-unit half-range we get ~65. Squared = 4225. Tunable. */
#define DEFAULT_TAP_MOVE_THRESHOLD_SQ              4225L

/* Initialize a tap state machine to a clean IDLE state. Call once at boot
   or whenever the trackpad re-enumerates. */
void tp_tap_init(tp_tap_t *tap);

/* Drive the state machine with one event. touch_index identifies which
   touch slot the event applies to (relevant for TOUCH, MOTION, RELEASE,
   THUMB, PALM, PALM_UP); pass 0 for events not tied to a specific touch
   (TIMEOUT, BUTTON). State handlers may emit zero or more button events
   into tap->pending; the caller drains them. */
void tp_tap_handle_event(tp_tap_t *tap,
                         uint8_t touch_index,
                         tap_event_t event,
                         uint32_t time_us);

/* Internal helper exposed for state handlers. Callers should not invoke
   directly. Appends a button event to tap->pending. */
void tp_tap_post_button(tp_tap_t *tap, uint32_t button, bool pressed, uint32_t time_us);

#endif /* DH_TRACKPAD_TAP_TO_CLICK */
#endif /* MAGIC_TRACKPAD_TAP_H_ */
