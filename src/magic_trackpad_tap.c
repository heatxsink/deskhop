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

/* Default tap-button map: 1F=LEFT, 2F=RIGHT, 3F=MIDDLE. libinput supports
   an alternate map (LMR) via configuration; we hardcode LRM for now. */
static const uint32_t tp_tap_button_map[3] = {
    TP_BTN_LEFT, TP_BTN_RIGHT, TP_BTN_MIDDLE,
};

/* Helpers translated from libinput src/evdev-mt-touchpad-tap.c. */

static void
tp_tap_notify(tp_tap_t *tap, uint32_t time_us, int nfingers, bool pressed) {
    if (nfingers < 1 || nfingers > 3)
        return;
    /* libinput tracks bit(nfingers) so it knows which button-finger-count
       is currently held; mirror that. */
    if (pressed)
        tap->buttons_pressed |= (1u << nfingers);
    else
        tap->buttons_pressed &= ~(1u << nfingers);

    tp_tap_post_button(tap, tp_tap_button_map[nfingers - 1], pressed, time_us);
}

static void
tp_tap_set_timer(tp_tap_t *tap, uint32_t time_us) {
    tap->timer_us = time_us + DEFAULT_TAP_TIMEOUT_PERIOD_US;
}

static void
tp_tap_set_drag_timer(tp_tap_t *tap, uint32_t time_us, int nfingers_tapped) {
    uint32_t per_finger = DEFAULT_DRAG_TIMEOUT_PERIOD_PERFINGER_US * (uint32_t)nfingers_tapped;
    uint32_t timeout    = DEFAULT_DRAG_TIMEOUT_PERIOD_BASE_US + per_finger;
    tap->timer_us = time_us + timeout;
}

static void
tp_tap_set_draglock_timer(tp_tap_t *tap, uint32_t time_us) {
    tap->timer_us = time_us + DEFAULT_DRAGLOCK_TIMEOUT_PERIOD_US;
}

static void
tp_tap_clear_timer(tp_tap_t *tap) {
    tap->timer_us = 0;
}

static void
tp_tap_move_to_dead(tp_tap_t *tap, uint8_t ti) {
    tap->state             = TAP_STATE_DEAD;
    tap->touches[ti].state = TAP_TOUCH_STATE_DEAD;
    tp_tap_clear_timer(tap);
}

/* libinput's log_tap_bug logs a "this should not happen" warning; we
   just no-op. State machine bugs surface as misbehavior, not crashes. */
#define log_tap_bug(tap, ti, event) do { (void)(tap); (void)(ti); (void)(event); } while (0)

/* libinput coordinates with its higher-level gesture system through
   tp_gesture_tap_timeout; we don't have that layer, so it's a no-op. */
#define tp_gesture_tap_timeout(tap, time_us) do { (void)(tap); (void)(time_us); } while (0)

void tp_tap_init(tp_tap_t *tap) {
    memset(tap, 0, sizeof(*tap));
    tap->state        = TAP_STATE_IDLE;
    tap->enabled      = true;
    /* drag_enabled OFF until we wire up a periodic timer tick. With it on,
       libinput defers tap release to a timer that only fires when
       mt_gesture_step is called, and the trackpad may go silent after a
       lift -- leaving the click stuck pressed. With drag_enabled=false,
       release fires immediately on lift. Tap-and-drag is therefore not
       yet supported; will re-enable when a periodic core1 task is added
       to advance the timer between HID reports. */
    tap->drag_enabled = false;
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

/* libinput src/evdev-mt-touchpad-tap.c:197 */
static void
tp_tap_idle_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    switch (event) {
    case TAP_EVENT_TOUCH:
        tap->state = TAP_STATE_TOUCH;
        tap->saved_press_us = time_us;
        tp_tap_set_timer(tap, time_us);
        break;
    case TAP_EVENT_RELEASE:
        break;
    case TAP_EVENT_MOTION:
        log_tap_bug(tap, ti, event);
        break;
    case TAP_EVENT_TIMEOUT:
        break;
    case TAP_EVENT_BUTTON:
        tap->state = TAP_STATE_DEAD;
        break;
    case TAP_EVENT_THUMB:
        log_tap_bug(tap, ti, event);
        break;
    case TAP_EVENT_PALM:
        tap->state = TAP_STATE_IDLE;
        break;
    case TAP_EVENT_PALM_UP:
        break;
    }
}

/* libinput src/evdev-mt-touchpad-tap.c:230 */
static void
tp_tap_touch_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    switch (event) {
    case TAP_EVENT_TOUCH:
        tap->state = TAP_STATE_TOUCH_2;
        tap->saved_press_us = time_us;
        tp_tap_set_timer(tap, time_us);
        break;
    case TAP_EVENT_RELEASE:
        tp_tap_notify(tap, tap->saved_press_us, 1, true);
        if (tap->drag_enabled) {
            tap->state = TAP_STATE_1FGTAP_TAPPED;
            tap->saved_release_us = time_us;
            tp_tap_set_drag_timer(tap, time_us, 1);
        } else {
            tp_tap_notify(tap, time_us, 1, false);
            tap->state = TAP_STATE_IDLE;
        }
        break;
    case TAP_EVENT_MOTION:
        tp_tap_move_to_dead(tap, ti);
        break;
    case TAP_EVENT_TIMEOUT:
        tap->state = TAP_STATE_HOLD;
        tp_tap_clear_timer(tap);
        tp_gesture_tap_timeout(tap, time_us);
        break;
    case TAP_EVENT_BUTTON:
        tap->state = TAP_STATE_DEAD;
        break;
    case TAP_EVENT_THUMB:
        tap->state = TAP_STATE_IDLE;
        tap->touches[ti].is_thumb = true;
        if (tap->nfingers_down) tap->nfingers_down--;
        tap->touches[ti].state = TAP_TOUCH_STATE_DEAD;
        tp_tap_clear_timer(tap);
        break;
    case TAP_EVENT_PALM:
        tap->state = TAP_STATE_IDLE;
        tp_tap_clear_timer(tap);
        break;
    case TAP_EVENT_PALM_UP:
        break;
    }
}

/* libinput src/evdev-mt-touchpad-tap.c:284 */
static void
tp_tap_hold_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us) {
    switch (event) {
    case TAP_EVENT_TOUCH:
        tap->state = TAP_STATE_TOUCH_2;
        tap->saved_press_us = time_us;
        tp_tap_set_timer(tap, time_us);
        break;
    case TAP_EVENT_RELEASE:
        tap->state = TAP_STATE_IDLE;
        break;
    case TAP_EVENT_MOTION:
        tp_tap_move_to_dead(tap, ti);
        break;
    case TAP_EVENT_TIMEOUT:
        break;
    case TAP_EVENT_BUTTON:
        tap->state = TAP_STATE_DEAD;
        break;
    case TAP_EVENT_THUMB:
        tap->state = TAP_STATE_IDLE;
        tap->touches[ti].is_thumb = true;
        if (tap->nfingers_down) tap->nfingers_down--;
        tap->touches[ti].state = TAP_TOUCH_STATE_DEAD;
        break;
    case TAP_EVENT_PALM:
        tap->state = TAP_STATE_IDLE;
        break;
    case TAP_EVENT_PALM_UP:
        break;
    }
}

/* libinput src/evdev-mt-touchpad-tap.c:322 */
static void
tp_tap_tapped_handle_event(tp_tap_t *tap, uint8_t ti, tap_event_t event, uint32_t time_us, int nfingers) {
    switch (event) {
    case TAP_EVENT_MOTION:
    case TAP_EVENT_RELEASE:
        log_tap_bug(tap, ti, event);
        break;
    case TAP_EVENT_TOUCH: {
        static const tp_tap_state_t dest[3] = {
            TAP_STATE_1FGTAP_DRAGGING_OR_DOUBLETAP,
            TAP_STATE_2FGTAP_DRAGGING_OR_DOUBLETAP,
            TAP_STATE_3FGTAP_DRAGGING_OR_DOUBLETAP,
        };
        if (nfingers < 1 || nfingers > 3) break;  /* libinput asserts */
        tap->state = dest[nfingers - 1];
        tap->saved_press_us = time_us;
        tp_tap_set_timer(tap, time_us);
        break;
    }
    case TAP_EVENT_TIMEOUT:
        tap->state = TAP_STATE_IDLE;
        tp_tap_notify(tap, tap->saved_release_us, nfingers, false);
        break;
    case TAP_EVENT_BUTTON:
        tap->state = TAP_STATE_DEAD;
        tp_tap_notify(tap, tap->saved_release_us, nfingers, false);
        break;
    case TAP_EVENT_THUMB:
        log_tap_bug(tap, ti, event);
        break;
    case TAP_EVENT_PALM:
        log_tap_bug(tap, ti, event);
        break;
    case TAP_EVENT_PALM_UP:
        break;
    }
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
