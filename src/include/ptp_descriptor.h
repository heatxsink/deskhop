/*
 * This file is part of DeskHop (https://github.com/heatxsink/deskhop).
 *
 * Microsoft Precision Touchpad (PTP) HID report descriptor for Path P.
 * Compiled in only when DH_PATH_P is defined; the descriptor itself is
 * not yet wired into the device-side configuration. This header is the
 * Step P.1 checkpoint: the descriptor blob exists and can be inspected
 * / linted, no behavior change.
 *
 * References:
 *   - Microsoft "Required HID top-level collections": Digitizer/TouchPad
 *     https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-required-hid-top-level-collections
 *   - Linux drivers/hid/hid-multitouch.c (parsing side)
 *   - Linux libinput src/evdev-mt-touchpad.c (gesture / classification side)
 *
 * Layout (top-level):
 *   - Touch Pad collection (Digitizer / TouchPad usage)
 *       - Per-contact collection x N (Tip Switch, Confidence,
 *         Contact ID, X, Y)
 *       - Contact Count, Scan Time, Button
 *   - Configuration / capabilities feature reports (PTPHQA, max
 *     contact count, button type, latency mode)
 *
 * Coordinate space: 0..PTP_LOGICAL_MAX_X / Y. Magic Trackpad 2 USB raw
 * range is approximately +-4096 (13-bit signed) per axis, mapped to
 * the pad's physical 16 cm x 11 cm. We rescale to PTP's range in the
 * report-fill code (separate file, future commit).
 */
#pragma once

#include <stdint.h>

#ifdef DH_PATH_P

/* Report IDs for the touchpad interface. Keep these distinct from the
   existing report IDs in usb_descriptors.c (REPORT_ID_KEYBOARD,
   REPORT_ID_MOUSE, REPORT_ID_CONSUMER, REPORT_ID_SYSTEM,
   REPORT_ID_RELMOUSE, REPORT_ID_VENDOR). Microsoft's PTP requires a
   specific set of feature report IDs (0x07 PTPHQA, 0x08 max contact
   count, 0x09 button type, 0x0A latency mode); we honor that and
   pick our touchpad input report ID just outside that range. */
#define PTP_REPORT_ID_TOUCH         0x10  /* input: contact data */
#define PTP_REPORT_ID_FEATURE_PTPHQA 0x07
#define PTP_REPORT_ID_FEATURE_MAX   0x08  /* feature: max contact count */
#define PTP_REPORT_ID_FEATURE_BTN   0x09  /* feature: button type (clickpad/pressure-pad) */
#define PTP_REPORT_ID_FEATURE_MODE  0x0A  /* feature: input mode: 0=mouse, 3=multi-touch */
#define PTP_REPORT_ID_FEATURE_LAT   0x0B  /* feature: latency mode: 0=normal, 1=low-latency */

/* Pad dimensions in 0.01 mm. Magic Trackpad 2 is approximately
   16.0 cm x 11.5 cm (wider variant). Hardcoded for now; if we ever
   support other Apple trackpads we'll either query at runtime or pick
   the union of dimensions. */
#define PTP_PHYSICAL_WIDTH_TENTHS_MM   1600  /* 16.0 cm */
#define PTP_PHYSICAL_HEIGHT_TENTHS_MM  1150  /* 11.5 cm */

/* Logical coordinate range. 1 logical unit = 0.05 mm. Physical mapping
   in the descriptor: X 0..3200 -> 0..16.00 cm, Y 0..2252 -> 0..11.26 cm.
   These have to match the descriptor body byte-for-byte. */
#define PTP_LOGICAL_MAX_X  3200
#define PTP_LOGICAL_MAX_Y  2252

/* Maximum simultaneous contacts. Magic Trackpad supports 11; libinput
   is happy with up to 5 for gesture detection on a touchpad and the
   per-contact byte cost adds up. Five is also the most-tested PTP
   value across OSes. */
#define PTP_MAX_CONTACTS  5

/* The PTP HID report descriptor.
 *
 * Defined inline in this header as `static const` so its size
 * (sizeof(ptp_descriptor)) is a compile-time constant at every TU that
 * includes it. The USB descriptor macros (TUD_HID_DESCRIPTOR) need the
 * length as a compile-time integer, which an extern declaration would
 * not give us. The flash duplication is small (only two TUs include
 * this header: the descriptor source-of-truth and usb_descriptors.c).
 *
 * Layout per contact (input report 0x10):
 *   1 byte: confidence (1 bit) | tip switch (1 bit) | reserved (6 bits)
 *   1 byte: contact ID (low 7 bits, range 0..127)
 *   2 bytes: X position
 *   2 bytes: Y position
 * Total per-contact: 6 bytes.
 *
 * After the 5 contact records:
 *   2 bytes: scan time (in 100us units)
 *   1 byte: contact count
 *   1 byte: button (BTN1)
 *
 * Total input report = 1 (report ID) + 6 * 5 + 4 = 35 bytes.
 *
 * NOTE: This descriptor is the canonical Microsoft PTP shape but has
 * NOT been validated end-to-end against Linux/macOS/Windows yet. */
/* PTP descriptor — 5 contacts, no feature reports yet (Step P.3
   minimal). The single-contact version of this descriptor was
   validated to bind to hid-multitouch on Linux as a touchpad with
   gesture capability; this expands to 5 contacts using the same
   item shapes that worked. */

/* Per-contact bytes shared across all 5 contacts. Identical structure
   each time. Wrapped in a macro so the body lives once.
   Includes Unit (cm) + Unit Exponent (-2) + Physical Min/Max so udev
   computes ID_INPUT_WIDTH_MM / HEIGHT_MM correctly and libinput's
   gesture math is calibrated to real-world distances. Without these,
   libinput logs "no resolution or size hints" and may silently drop
   events whose deltas don't pass its motion-noise threshold. */
#define PTP_CONTACT_BLOCK \
    0x05, 0x0D,            /* Usage Page (Digitizer)             */ \
    0x09, 0x22,            /* Usage (Finger)                     */ \
    0xA1, 0x02,            /* Collection (Logical)               */ \
    0x09, 0x42,            /*   Usage (Tip Switch)               */ \
    0x15, 0x00,            /*   Logical Min (0)                  */ \
    0x25, 0x01,            /*   Logical Max (1)                  */ \
    0x75, 0x01,            /*   Report Size (1)                  */ \
    0x95, 0x01,            /*   Report Count (1)                 */ \
    0x81, 0x02,            /*   Input                            */ \
    0x95, 0x07,            /*   Report Count (7)                 */ \
    0x81, 0x03,            /*   Input padding                    */ \
    0x09, 0x51,            /*   Usage (Contact ID)               */ \
    0x25, 0x7F,            /*   Logical Max (127)                */ \
    0x75, 0x08,            /*   Report Size (8)                  */ \
    0x95, 0x01,            /*   Report Count (1)                 */ \
    0x81, 0x02,            /*   Input                            */ \
    0x05, 0x01,            /*   Usage Page (Generic Desktop)     */ \
    0x09, 0x30,            /*   Usage (X)                        */ \
    0x16, 0x00, 0x00,      /*   Logical Min (0)                  */ \
    0x26, 0x80, 0x0C,      /*   Logical Max (3200)               */ \
    0x36, 0x00, 0x00,      /*   Physical Min (0)                 */ \
    0x46, 0x40, 0x06,      /*   Physical Max (1600 = 16.00 cm)   */ \
    0x65, 0x11,            /*   Unit (SI Linear * Length)        */ \
    0x55, 0x0E,            /*   Unit Exponent (-2 -> 0.01 cm)    */ \
    0x75, 0x10,            /*   Report Size (16)                 */ \
    0x95, 0x01,            /*   Report Count (1)                 */ \
    0x81, 0x02,            /*   Input                            */ \
    0x09, 0x31,            /*   Usage (Y)                        */ \
    0x26, 0xCC, 0x08,      /*   Logical Max (2252)               */ \
    0x46, 0x66, 0x04,      /*   Physical Max (1126 = 11.26 cm)   */ \
    0x81, 0x02,            /*   Input                            */ \
    0x05, 0x0D,            /*   Usage Page (Digitizer) -- reset  */ \
    0x55, 0x00,            /*   Unit Exponent (0)                */ \
    0x65, 0x00,            /*   Unit (None)                      */ \
    0xC0                   /* End Collection (Logical)           */

static const uint8_t ptp_descriptor[] = {
    0x05, 0x0D,                  /* Usage Page (Digitizer)            */
    0x09, 0x05,                  /* Usage (Touch Pad)                 */
    0xA1, 0x01,                  /* Collection (Application)          */
    0x85, PTP_REPORT_ID_TOUCH,   /*   Report ID                       */

    PTP_CONTACT_BLOCK,           /* Contact 0 */
    PTP_CONTACT_BLOCK,           /* Contact 1 */
    PTP_CONTACT_BLOCK,           /* Contact 2 */
    PTP_CONTACT_BLOCK,           /* Contact 3 */
    PTP_CONTACT_BLOCK,           /* Contact 4 */

    0x05, 0x0D,                  /*   Usage Page (Digitizer)          */
    0x09, 0x54,                  /*   Usage (Contact Count)           */
    0x25, PTP_MAX_CONTACTS,      /*   Logical Max (5)                 */
    0x75, 0x08,                  /*   Report Size (8)                 */
    0x95, 0x01,                  /*   Report Count (1)                */
    0x81, 0x02,                  /*   Input                           */

    0x05, 0x09,                  /*   Usage Page (Button)             */
    0x09, 0x01,                  /*   Usage (Button 1)                */
    0x25, 0x01,                  /*   Logical Max (1)                 */
    0x75, 0x01,                  /*   Report Size (1)                 */
    0x95, 0x01,                  /*   Report Count (1)                */
    0x81, 0x02,                  /*   Input                           */
    0x95, 0x07,                  /*   Report Count (7) padding        */
    0x81, 0x03,                  /*   Input                           */

    /* ---------- Required Microsoft PTP feature reports ----------
       Without these, libinput / hid-multitouch may silently drop
       events because the host can't switch the device into
       multi-touch mode. */

    /* Max contact count -- read by the host during enumeration. */
    0x05, 0x0D,                  /*   Usage Page (Digitizer)          */
    0x09, 0x55,                  /*   Usage (Contact Count Maximum)   */
    0x85, PTP_REPORT_ID_FEATURE_MAX,
    0x15, 0x00,                  /*   Logical Min (0)                 */
    0x25, 0x0F,                  /*   Logical Max (15)                */
    0x75, 0x08,                  /*   Report Size (8)                 */
    0x95, 0x01,                  /*   Report Count (1)                */
    0xB1, 0x02,                  /*   Feature (Data,Var,Abs)          */

    /* Input mode -- host WRITES this to switch the touchpad between
       mouse-emulation (0) and multi-touch (3). hid-multitouch sets
       it to 3 when binding so the device knows to send MT reports. */
    0x06, 0x00, 0xFF,            /*   Usage Page (Vendor 0xFF00)      */
    0x09, 0xC5,                  /*   Usage (Vendor 0xC5)             */
    0x85, PTP_REPORT_ID_FEATURE_MODE,
    0x15, 0x00,                  /*   Logical Min (0)                 */
    0x25, 0xFF,                  /*   Logical Max (255)               */
    0x75, 0x08,                  /*   Report Size (8)                 */
    0x95, 0x01,                  /*   Report Count (1)                */
    0xB1, 0x02,                  /*   Feature                         */

    0xC0                         /* End Collection                    */
};

/* Layout of a single contact in the input report. Matches the
   descriptor block PTP_CONTACT_BLOCK byte-for-byte:
     - 1 byte: tip switch (bit 0) + 7 bits padding
     - 1 byte: contact ID (range 0..127)
     - 2 bytes: X
     - 2 bytes: Y
   Total: 6 bytes per contact. */
typedef struct __attribute__((packed)) {
    uint8_t  tip_switch;        /* bit 0 set = finger touching */
    uint8_t  contact_id;        /* 0..127 */
    int16_t  x;                 /* 0..PTP_LOGICAL_MAX_X */
    int16_t  y;                 /* 0..PTP_LOGICAL_MAX_Y */
} ptp_contact_t;

/* Input-report payload (excludes report ID -- TinyUSB's
   tud_hid_n_report prepends the report ID byte from its own
   argument). Total: 5 * 6 + 1 + 1 = 32 bytes. */
typedef struct __attribute__((packed)) {
    ptp_contact_t contacts[PTP_MAX_CONTACTS];
    uint8_t       contact_count;              /* 0..PTP_MAX_CONTACTS */
    uint8_t       buttons;                    /* bit 0 = BTN1 (clickpad press) */
} ptp_input_report_t;

/* Feature report: max contact count. Read by Windows during enumeration
   to size its internal contact arrays. */
typedef struct __attribute__((packed)) {
    uint8_t report_id;                        /* PTP_REPORT_ID_FEATURE_MAX */
    uint8_t max_contacts;                     /* PTP_MAX_CONTACTS */
} ptp_feature_max_contacts_t;

/* Feature report: input mode. The host writes this to switch deskhop's
   touchpad behavior between mouse-emulation (0) and multi-touch (3)
   modes. We emit MT contacts only when mode == 3. */
typedef struct __attribute__((packed)) {
    uint8_t report_id;                        /* PTP_REPORT_ID_FEATURE_MODE */
    uint8_t mode;                             /* 0 = mouse, 3 = multi-touch */
} ptp_feature_mode_t;

/* Send a PTP input report through the touchpad HID interface.
   Returns true if queued for transmission, false if the endpoint
   was busy. Defined in usb_descriptors.c. */
#include <stdbool.h>
bool tud_touchpad_report(const ptp_input_report_t *report);

#endif /* DH_PATH_P */
