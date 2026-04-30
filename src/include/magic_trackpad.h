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

/* Per-trackpad gesture tracking. Persists across frames. */
typedef struct {
    int16_t  prev_x[MT_MAX_FINGERS];
    int16_t  prev_y[MT_MAX_FINGERS];
    uint8_t  prev_id[MT_MAX_FINGERS];
    uint8_t  prev_count;
    bool     have_prev;
} mt_gesture_state_t;

void mt_gesture_init(mt_gesture_state_t *s);

/* Consume a frame and produce mouse-pipeline values.
   Writes into out_move_x/y/wheel/pan. Returns false if no usable signal
   (transition frame, finger count change, etc.). */
bool mt_gesture_step(mt_gesture_state_t *s, const mt_frame_t *frame,
                     int32_t *out_move_x, int32_t *out_move_y,
                     int32_t *out_wheel, int32_t *out_pan);

#endif /* MAGIC_TRACKPAD_H_ */
