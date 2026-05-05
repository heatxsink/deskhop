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

/* Logical coordinate range. Picked to give roughly 1 logical unit per
   0.01 mm at the pad's native resolution; the host will scale this to
   pixels via the reported physical dimensions. */
#define PTP_LOGICAL_MAX_X  3200
#define PTP_LOGICAL_MAX_Y  2300

/* Maximum simultaneous contacts. Magic Trackpad supports 11; libinput
   is happy with up to 5 for gesture detection on a touchpad and the
   per-contact byte cost adds up. Five is also the most-tested PTP
   value across OSes. */
#define PTP_MAX_CONTACTS  5

/* The PTP HID report descriptor.
 *
 * Layout per contact (for input report 0x10):
 *   1 byte: confidence (1 bit) | tip switch (1 bit) | contact ID (4 bits) | reserved (2 bits)
 *   2 bytes: X position
 *   2 bytes: Y position
 *
 * After the contact array:
 *   2 bytes: scan time (in 100us units)
 *   1 byte: contact count
 *   1 byte: button (BTN1)
 *
 * Total input report = 1 (report ID) + 5 * MAX_CONTACTS + 4 = 30 bytes.
 *
 * NOTE: This descriptor is the canonical Microsoft PTP shape but has
 * NOT been validated end-to-end against Linux/macOS/Windows yet. It's
 * the Step P.1 deliverable -- bytes on disk, ready for review.
 */
extern const uint8_t ptp_descriptor[];
extern const uint16_t ptp_descriptor_len;

/* Layout of a single contact in the input report. Matches the
   descriptor above byte-for-byte. */
typedef struct __attribute__((packed)) {
    uint8_t  confidence_tip_id; /* bit 0 = confidence, bit 1 = tip, bits 2-5 = contact ID */
    int16_t  x;
    int16_t  y;
} ptp_contact_t;

/* Full input report structure. Sent on every multi-touch frame change
   while in PTP mode. */
typedef struct __attribute__((packed)) {
    uint8_t       report_id;                  /* PTP_REPORT_ID_TOUCH */
    ptp_contact_t contacts[PTP_MAX_CONTACTS];
    uint16_t      scan_time;                  /* 100us units */
    uint8_t       contact_count;              /* number of valid contacts in this report */
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

#endif /* DH_PATH_P */
