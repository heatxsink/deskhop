/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 *
 * Tap state machine. Faithfully translated from libinput's
 * src/evdev-mt-touchpad-tap.c. State names, event names, and handler
 * function names verbatim. See magic_trackpad_tap.h for the contract.
 *
 * libinput original copyright (c) 2013-2015 Red Hat, Inc., MIT licensed.
 */

#include "magic_trackpad_tap.h"

#ifdef DH_TRACKPAD_TAP_TO_CLICK

#include <string.h>

/* HID button bits. Match libinput's BTN_LEFT/RIGHT/MIDDLE constants. */
#define TP_BTN_LEFT     0x01
#define TP_BTN_RIGHT    0x02
#define TP_BTN_MIDDLE   0x04

void tp_tap_init(tp_tap_t *tap) {
    memset(tap, 0, sizeof(*tap));
    tap->state        = TAP_STATE_IDLE;
    tap->enabled      = true;
    tap->drag_enabled = true;
    for (int i = 0; i < MT_MAX_FINGERS; i++) {
        tap->touches[i].state = TAP_TOUCH_STATE_IDLE;
    }
}

void tp_tap_post_button(tp_tap_t *tap, uint32_t button, bool pressed, uint32_t time_us) {
    if (tap->pending_count >= TP_TAP_PENDING_MAX)
        return;  /* drop -- caller didn't drain fast enough */
    tap->pending[tap->pending_count++] = (tp_tap_button_event_t){
        .button  = button,
        .pressed = pressed,
        .time_us = time_us,
    };
    if (pressed)
        tap->buttons_pressed |= button;
    else
        tap->buttons_pressed &= ~button;
}

/* ============================================================ *
 *  Per-state event handlers. Stubs in this commit.
 *  Each will be filled in step-by-step matching libinput's source.
 * ============================================================ */

static void
tp_tap_idle_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO: translate libinput tp_tap_idle_handle_event */
}

static void
tp_tap_touch_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO: translate libinput tp_tap_touch_handle_event */
}

static void
tp_tap_hold_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO: translate libinput tp_tap_hold_handle_event */
}

static void
tp_tap_tapped_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    (void)tap; (void)ti; (void)event; (void)time_us; (void)nfingers;
    /* TODO: translate libinput tp_tap_tapped_handle_event (1FG/2FG/3FG variants) */
}

static void
tp_tap_touch2_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_touch2_hold_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_touch2_release_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_touch3_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_touch3_hold_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_touch3_release_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_touch3_release2_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

static void
tp_tap_dragging_or_doubletap_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    (void)tap; (void)ti; (void)event; (void)time_us; (void)nfingers;
    /* TODO */
}

static void
tp_tap_dragging_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    (void)tap; (void)ti; (void)event; (void)time_us; (void)nfingers;
    /* TODO */
}

static void
tp_tap_dragging_wait_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    (void)tap; (void)ti; (void)event; (void)time_us; (void)nfingers;
    /* TODO */
}

static void
tp_tap_dragging_tap_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    (void)tap; (void)ti; (void)event; (void)time_us; (void)nfingers;
    /* TODO */
}

static void
tp_tap_dragging2_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    (void)tap; (void)ti; (void)event; (void)time_us; (void)nfingers;
    /* TODO */
}

static void
tp_tap_dead_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    (void)tap; (void)ti; (void)event; (void)time_us;
    /* TODO */
}

/* libinput src/evdev-mt-touchpad-tap.c:1071 */
void tp_tap_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    if (!tap->enabled)
        return;

    switch (tap->state) {
        case TAP_STATE_IDLE:
            tp_tap_idle_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH:
            tp_tap_touch_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_HOLD:
            tp_tap_hold_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_1FGTAP_TAPPED:
            tp_tap_tapped_handle_event(tap, ti, event, time_us, 1);
            break;
        case TAP_STATE_2FGTAP_TAPPED:
            tp_tap_tapped_handle_event(tap, ti, event, time_us, 2);
            break;
        case TAP_STATE_3FGTAP_TAPPED:
            tp_tap_tapped_handle_event(tap, ti, event, time_us, 3);
            break;
        case TAP_STATE_TOUCH_2:
            tp_tap_touch2_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH_2_HOLD:
            tp_tap_touch2_hold_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH_2_RELEASE:
            tp_tap_touch2_release_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH_3:
            tp_tap_touch3_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH_3_HOLD:
            tp_tap_touch3_hold_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH_3_RELEASE:
            tp_tap_touch3_release_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_TOUCH_3_RELEASE_2:
            tp_tap_touch3_release2_handle_event(tap, ti, event, time_us);
            break;
        case TAP_STATE_1FGTAP_DRAGGING_OR_DOUBLETAP:
            tp_tap_dragging_or_doubletap_handle_event(tap, ti, event, time_us, 1);
            break;
        case TAP_STATE_2FGTAP_DRAGGING_OR_DOUBLETAP:
            tp_tap_dragging_or_doubletap_handle_event(tap, ti, event, time_us, 2);
            break;
        case TAP_STATE_3FGTAP_DRAGGING_OR_DOUBLETAP:
            tp_tap_dragging_or_doubletap_handle_event(tap, ti, event, time_us, 3);
            break;
        case TAP_STATE_1FGTAP_DRAGGING:
            tp_tap_dragging_handle_event(tap, ti, event, time_us, 1);
            break;
        case TAP_STATE_2FGTAP_DRAGGING:
            tp_tap_dragging_handle_event(tap, ti, event, time_us, 2);
            break;
        case TAP_STATE_3FGTAP_DRAGGING:
            tp_tap_dragging_handle_event(tap, ti, event, time_us, 3);
            break;
        case TAP_STATE_1FGTAP_DRAGGING_WAIT:
            tp_tap_dragging_wait_handle_event(tap, ti, event, time_us, 1);
            break;
        case TAP_STATE_2FGTAP_DRAGGING_WAIT:
            tp_tap_dragging_wait_handle_event(tap, ti, event, time_us, 2);
            break;
        case TAP_STATE_3FGTAP_DRAGGING_WAIT:
            tp_tap_dragging_wait_handle_event(tap, ti, event, time_us, 3);
            break;
        case TAP_STATE_1FGTAP_DRAGGING_OR_TAP:
            tp_tap_dragging_tap_handle_event(tap, ti, event, time_us, 1);
            break;
        case TAP_STATE_2FGTAP_DRAGGING_OR_TAP:
            tp_tap_dragging_tap_handle_event(tap, ti, event, time_us, 2);
            break;
        case TAP_STATE_3FGTAP_DRAGGING_OR_TAP:
            tp_tap_dragging_tap_handle_event(tap, ti, event, time_us, 3);
            break;
        case TAP_STATE_1FGTAP_DRAGGING_2:
            tp_tap_dragging2_handle_event(tap, ti, event, time_us, 1);
            break;
        case TAP_STATE_2FGTAP_DRAGGING_2:
            tp_tap_dragging2_handle_event(tap, ti, event, time_us, 2);
            break;
        case TAP_STATE_3FGTAP_DRAGGING_2:
            tp_tap_dragging2_handle_event(tap, ti, event, time_us, 3);
            break;
        case TAP_STATE_DEAD:
            tp_tap_dead_handle_event(tap, ti, event, time_us);
            break;
    }
}

#endif /* DH_TRACKPAD_TAP_TO_CLICK */
