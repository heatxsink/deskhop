/*
 * This file is part of DeskHop (https://github.com/heatxsink/deskhop).
 *
 * Microsoft Precision Touchpad HID report descriptor blob. Step P.1
 * checkpoint: bytes on disk, no integration yet. The descriptor is
 * compiled in only when DH_PATH_P is defined; nothing at runtime
 * references it until later steps wire it into the device-side
 * configuration.
 *
 * Microsoft PTP spec is hardware-rigid -- this descriptor matches the
 * canonical shape Windows expects to recognize a device as a Precision
 * Touchpad. Linux/macOS will accept it as a generic multi-touch
 * digitizer with absolute axes, which is enough for libinput to
 * classify it as a touchpad and run its gesture state machine.
 *
 * Per-contact size: 5 bytes (1 status + 2 X + 2 Y).
 * Input report 0x10 size: 1 (report ID) + 5 * PTP_MAX_CONTACTS
 *                        + 2 (scan time) + 1 (contact count)
 *                        + 1 (buttons) = 30 bytes for MAX_CONTACTS = 5.
 */

#include "ptp_descriptor.h"

#ifdef DH_PATH_P

/* HID descriptor bytes. Hand-assembled (not generated from a TinyUSB
   macro) because TinyUSB's stock TUD_HID_REPORT_DESC_* macros don't
   include a Precision Touchpad shape -- this is custom for our use.
   Layout per Microsoft's PTP spec:
     1. Touchpad collection (Digitizer / TouchPad)
        - Per-contact collection x PTP_MAX_CONTACTS
        - Scan time, contact count, button
     2. Configuration / capability feature reports

   Numbers in this descriptor are little-endian when multi-byte. */
const uint8_t ptp_descriptor[] = {
    /* ---------- Top-level: Touchpad input collection ---------- */
    0x05, 0x0D,                  /* Usage Page (Digitizer)            */
    0x09, 0x05,                  /* Usage (Touch Pad)                 */
    0xA1, 0x01,                  /* Collection (Application)          */

    0x85, PTP_REPORT_ID_TOUCH,   /*   Report ID                       */

    /* ---------- Five per-contact collections ---------- */
    /* Each contact is 5 bytes: 1 status byte (confidence + tip + ID),
       2 X bytes, 2 Y bytes. Repeated PTP_MAX_CONTACTS times. */

    /* Helper macro idea: we manually unroll for clarity and to avoid
       a C-preprocessor mess. PTP_MAX_CONTACTS == 5 here; if you change
       the constant, also update the unroll below and the input-report
       byte count in size calculations. */

    /* --- Contact 0 --- */
    0x05, 0x0D,                  /*   Usage Page (Digitizer)          */
    0x09, 0x22,                  /*   Usage (Finger)                  */
    0xA1, 0x02,                  /*   Collection (Logical)            */
    0x09, 0x47,                  /*     Usage (Confidence)            */
    0x09, 0x42,                  /*     Usage (Tip Switch)            */
    0x95, 0x02,                  /*     Report Count (2)              */
    0x75, 0x01,                  /*     Report Size (1)               */
    0x15, 0x00,                  /*     Logical Minimum (0)           */
    0x25, 0x01,                  /*     Logical Maximum (1)           */
    0x81, 0x02,                  /*     Input (Data,Var,Abs)          */
    0x95, 0x06,                  /*     Report Count (6 padding bits) */
    0x81, 0x03,                  /*     Input (Cnst,Var,Abs)          */
    0x75, 0x08,                  /*     Report Size (8)               */
    0x09, 0x51,                  /*     Usage (Contact Identifier)    */
    0x95, 0x01,                  /*     Report Count (1)              */
    0x25, 0x7F,                  /*     Logical Maximum (127)         */
    0x81, 0x02,                  /*     Input (Data,Var,Abs)          */
    0x05, 0x01,                  /*     Usage Page (Generic Desktop)  */
    0x09, 0x30,                  /*     Usage (X)                     */
    0x09, 0x31,                  /*     Usage (Y)                     */
    0x16, 0x00, 0x00,            /*     Logical Minimum (0)           */
    0x26, 0x80, 0x0C,            /*     Logical Maximum (3200)        */
    0x36, 0x00, 0x00,            /*     Physical Minimum (0)          */
    0x46, 0x40, 0x06,            /*     Physical Maximum (1600 = 16cm)*/
    0x65, 0x11,                  /*     Unit (cm)                     */
    0x55, 0x0E,                  /*     Unit Exponent (-2)            */
    0x75, 0x10,                  /*     Report Size (16)              */
    0x95, 0x02,                  /*     Report Count (2)              */
    0x81, 0x02,                  /*     Input (Data,Var,Abs)          */
    0xC0,                        /*   End Collection                  */

    /* --- Contact 1 --- (identical inner block as Contact 0) */
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03,
    0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x16, 0x00, 0x00, 0x26, 0x80, 0x0C,
    0x36, 0x00, 0x00, 0x46, 0x40, 0x06,
    0x65, 0x11, 0x55, 0x0E,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0,

    /* --- Contact 2 --- */
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03,
    0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x16, 0x00, 0x00, 0x26, 0x80, 0x0C,
    0x36, 0x00, 0x00, 0x46, 0x40, 0x06,
    0x65, 0x11, 0x55, 0x0E,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0,

    /* --- Contact 3 --- */
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03,
    0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x16, 0x00, 0x00, 0x26, 0x80, 0x0C,
    0x36, 0x00, 0x00, 0x46, 0x40, 0x06,
    0x65, 0x11, 0x55, 0x0E,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0,

    /* --- Contact 4 --- */
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x47, 0x09, 0x42, 0x95, 0x02, 0x75, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x81, 0x02,
    0x95, 0x06, 0x81, 0x03,
    0x75, 0x08, 0x09, 0x51, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x16, 0x00, 0x00, 0x26, 0x80, 0x0C,
    0x36, 0x00, 0x00, 0x46, 0x40, 0x06,
    0x65, 0x11, 0x55, 0x0E,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0,

    /* ---------- Scan time, contact count, button ---------- */
    0x05, 0x0D,                  /*   Usage Page (Digitizer)          */
    0x55, 0x0C,                  /*   Unit Exponent (-4) [100us]      */
    0x66, 0x01, 0x10,            /*   Unit (Seconds)                  */
    0x47, 0xFF, 0xFF, 0x00, 0x00,/*   Physical Maximum (65535)        */
    0x27, 0xFF, 0xFF, 0x00, 0x00,/*   Logical Maximum (65535)         */
    0x75, 0x10,                  /*   Report Size (16)                */
    0x95, 0x01,                  /*   Report Count (1)                */
    0x09, 0x56,                  /*   Usage (Scan Time)               */
    0x81, 0x02,                  /*   Input (Data,Var,Abs)            */

    0x09, 0x54,                  /*   Usage (Contact Count)           */
    0x25, PTP_MAX_CONTACTS,      /*   Logical Maximum (max contacts)  */
    0x95, 0x01,                  /*   Report Count (1)                */
    0x75, 0x08,                  /*   Report Size (8)                 */
    0x81, 0x02,                  /*   Input (Data,Var,Abs)            */

    0x05, 0x09,                  /*   Usage Page (Button)             */
    0x09, 0x01,                  /*   Usage (Button 1)                */
    0x25, 0x01,                  /*   Logical Maximum (1)             */
    0x75, 0x01,                  /*   Report Size (1)                 */
    0x95, 0x01,                  /*   Report Count (1)                */
    0x81, 0x02,                  /*   Input (Data,Var,Abs)            */
    0x95, 0x07,                  /*   Report Count (7 padding)        */
    0x81, 0x03,                  /*   Input (Cnst,Var,Abs)            */

    /* ---------- Feature reports ---------- */

    /* Max contact count (used by Windows) */
    0x05, 0x0D,                  /*   Usage Page (Digitizer)          */
    0x85, PTP_REPORT_ID_FEATURE_MAX,
    0x09, 0x55,                  /*   Usage (Contact Count Maximum)   */
    0x25, PTP_MAX_CONTACTS,
    0x75, 0x08, 0x95, 0x01,
    0xB1, 0x02,                  /*   Feature (Data,Var,Abs)          */

    /* Input mode: host writes 0 (mouse) or 3 (multi-touch) */
    0x06, 0x00, 0xFF,            /*   Usage Page (Vendor Defined)     */
    0x85, PTP_REPORT_ID_FEATURE_MODE,
    0x09, 0xC5,                  /*   Usage (Vendor Usage 0xC5)       */
    0x15, 0x00, 0x25, 0xFF,
    0x75, 0x08, 0x95, 0x01,
    0xB1, 0x02,                  /*   Feature (Data,Var,Abs)          */

    0xC0                         /* End Collection                    */
};

const uint16_t ptp_descriptor_len = sizeof(ptp_descriptor);

#endif /* DH_PATH_P */
