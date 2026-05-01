/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * Magic Trackpad 2 USB multi-touch decoder. References Linux's
 * drivers/hid/hid-magicmouse.c for wire format. Decoded and verified in
 * docs/captures/trackpad-multitouch-frames.txt.
 */

#ifndef MAGIC_TRACKPAD_H_
#define MAGIC_TRACKPAD_H_

#include <stdint.h>
#include <stdbool.h>

#define MT_MAX_FINGERS  6
#define MT_HEADER_SIZE  12
#define MT_FINGER_SIZE  9

typedef struct {
    int16_t  x;             /* signed 13-bit, sign-extended */
    int16_t  y;             /* signed 13-bit, sign-extended (post Linux negation) */
    uint8_t  touch_major;
    uint8_t  touch_minor;
    uint8_t  size;
    uint8_t  pressure;
    uint8_t  finger_id;     /* low 4 bits of finger byte 8 */
} mt_finger_t;

typedef struct {
    uint16_t    timestamp;
    uint8_t     finger_count;
    mt_finger_t fingers[MT_MAX_FINGERS];
} mt_frame_t;

bool mt_decode_report(const uint8_t *report, int len, mt_frame_t *out);

/* Discrete swipe outputs for 3-finger horizontal gestures. */
typedef enum {
    MT_SWIPE_NONE  = 0,
    MT_SWIPE_LEFT  = 1,
    MT_SWIPE_RIGHT = 2,
} mt_swipe_t;

/* Button state passed into the gesture step. The trackpad has only one
   physical button -- LEFT vs RIGHT is decided at click-down time based on
   how many fingers were on the surface (1 = left, 2+ = right). */
typedef enum {
    MT_BTN_NONE  = 0,
    MT_BTN_LEFT  = 1,
    MT_BTN_RIGHT = 2,
} mt_button_t;

/* Per-trackpad gesture tracking. Persists across frames. */
typedef struct {
    /* Tap state -- forward-declared via void* to keep this header
       agnostic of the libinput-port internals. magic_trackpad.c casts
       it back to tp_tap_t. */
    void    *tap;

    /* Gesture state -- same void-pointer pattern as tap above.
       magic_trackpad.c casts it back to tp_gesture_t. */
    void    *gesture;

    /* Track physical-click state across frames so the tap state machine
       gets a BUTTON event on rising/falling edges. */
    uint8_t  prev_button_held;

    /* Track when we last saw an active MT frame. The trackpad goes silent
       after the last finger lifts -- no terminator frame is sent. A
       periodic tick uses this timestamp to synthesize RELEASE events
       when the trackpad has been quiet long enough that we know all
       fingers must be lifted. */
    uint32_t last_mt_frame_us;
} mt_gesture_state_t;

void mt_gesture_init(mt_gesture_state_t *s);

/* Consume a frame and produce mouse-pipeline values + optional swipe event.
   Writes into out_move_x/y/wheel/pan. Sets *out_swipe to MT_SWIPE_LEFT or
   MT_SWIPE_RIGHT for at most one frame per 3-finger gesture; otherwise
   MT_SWIPE_NONE. Returns true if movement deltas (move/wheel/pan) are usable.

   button_held lets the gesture step know what physical click is in effect
   so it can branch correctly:
     2 fingers + MT_BTN_NONE  -> scroll
     2 fingers + MT_BTN_LEFT  -> drag (cursor follows the second finger)
     2 fingers + MT_BTN_RIGHT -> no motion (right-click held; cursor stays put) */
bool mt_gesture_step(mt_gesture_state_t *s, const mt_frame_t *frame,
                     mt_button_t button_held,
                     uint32_t now_us,
                     int32_t *out_move_x, int32_t *out_move_y,
                     int32_t *out_wheel, int32_t *out_pan,
                     mt_swipe_t *out_swipe);

/* Periodic tick. Called from a core1 task at ~100-200 Hz. Synthesizes
   RELEASE events for active touches when the trackpad has been silent
   long enough to imply all fingers have lifted, and dispatches TIMEOUT
   events when tap state-machine timers have expired. Returns true if
   any pending button events were enqueued -- caller drains. No-op
   when DH_MAGIC_TRACKPAD is off. */
bool mt_gesture_idle_tick(mt_gesture_state_t *s, uint32_t now_us);

#endif /* MAGIC_TRACKPAD_H_ */
