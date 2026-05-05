/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 */

#include "usb_descriptors.h"
#include "main.h"
#include "tusb.h"
#ifdef DH_PATH_P
#include "ptp_descriptor.h"
#endif

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

                                        // https://github.com/raspberrypi/usb-pid
tusb_desc_device_t const desc_device_config = DEVICE_DESCRIPTOR(0x2e8a, 0x107c);

                                        // https://pid.codes/1209/C000/
tusb_desc_device_t const desc_device = DEVICE_DESCRIPTOR(0x1209, 0xc000);

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
    if (global_state.config_mode_active)
        return (uint8_t const *)&desc_device_config;
    else
        return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

// Relative mouse is used to overcome limitations of multiple desktops on MacOS and Windows

uint8_t const desc_hid_report[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
                                   TUD_HID_REPORT_DESC_ABS_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
                                   TUD_HID_REPORT_DESC_CONSUMER_CTRL(HID_REPORT_ID(REPORT_ID_CONSUMER)),
                                   TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(REPORT_ID_SYSTEM))
                                   };

#ifdef DH_PATH_P
/* Alternate main HID report descriptor used in trackpad-attached mode:
   no abs-mouse collection. The abs-mouse path is what triggers libinput
   to classify a competing pointer device on the same seat, which then
   activates dwtp suppression on the touchpad. With the abs-mouse gone
   from the main interface (and the relmouse interface omitted from the
   configuration descriptor), only the touchpad remains as a pointing
   device and libinput stops filtering its events. */
uint8_t const desc_hid_report_trackpad[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER_CTRL(HID_REPORT_ID(REPORT_ID_CONSUMER)),
    TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(REPORT_ID_SYSTEM))
};
#endif

uint8_t const desc_hid_report_relmouse[] = {TUD_HID_REPORT_DESC_MOUSEHELP(HID_REPORT_ID(REPORT_ID_RELMOUSE))};

uint8_t const desc_hid_report_vendor[] = {TUD_HID_REPORT_DESC_VENDOR_CTRL(HID_REPORT_ID(REPORT_ID_VENDOR))};


// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (global_state.config_mode_active)
        if (instance == ITF_NUM_HID_VENDOR)
            return desc_hid_report_vendor;

#ifdef DH_PATH_P
    /* In trackpad-attached mode the configuration has only 2 HID
       interfaces: main (keyboard / consumer / system) at instance 0
       and touchpad at instance 1. In every other mode, the touchpad
       is at HID instance 2 (normal) or 3 (config mode). */
    if (global_state.trackpad_attached) {
        if (instance == 0) return desc_hid_report_trackpad;
        if (instance == 1) return ptp_descriptor;
    } else {
        uint8_t touchpad_hid_instance = global_state.config_mode_active ? 3 : 2;
        if (instance == touchpad_hid_instance)
            return ptp_descriptor;
    }
#endif

    switch(instance) {
        case ITF_NUM_HID:
            return desc_hid_report;
        case ITF_NUM_HID_REL_M:
            return desc_hid_report_relmouse;
        default:
            return desc_hid_report;
    }
}

bool tud_mouse_report(uint8_t mode, uint8_t buttons, int16_t x, int16_t y, int8_t wheel, int8_t pan) {
    mouse_report_t report = {.buttons = buttons, .wheel = wheel, .x = x, .y = y, .mode = mode, .pan = pan};
    uint8_t instance = ITF_NUM_HID;
    uint8_t report_id = REPORT_ID_MOUSE;

    if (mode == RELATIVE) {
        instance = ITF_NUM_HID_REL_M;
        report_id = REPORT_ID_RELMOUSE;
    }

    return tud_hid_n_report(instance, report_id, &report, sizeof(report));
}

#ifdef DH_PATH_P
/* Send a PTP input report to the host. The HID instance index where
   the touchpad lives depends on which configuration descriptor is
   currently active (see tud_hid_descriptor_report_cb above):
     - trackpad-attached mode:  2 HID interfaces, touchpad at instance 1
     - normal mode (no pad):    3 HID interfaces, touchpad at instance 2
     - config mode:             4 HID interfaces, touchpad at instance 3
   The report ID itself is the same in every mode. */
bool tud_touchpad_report(const ptp_input_report_t *report) {
    uint8_t hid_instance;
    if (global_state.trackpad_attached)
        hid_instance = 1;
    else if (global_state.config_mode_active)
        hid_instance = 3;
    else
        hid_instance = 2;
    return tud_hid_n_report(hid_instance, PTP_REPORT_ID_TOUCH, report, sizeof(*report));
}
#endif


//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "heatxsink",                // 1: Manufacturer
    "Keyboard Mouse Switch",    // 2: Product
    "0",                        // 3: Serials, should use chip ID
    "DeskHop Helper",           // 4: Mouse Helper Interface
    "DeskHop Config",           // 5: Vendor Interface
    "DeskHop Disk",             // 6: Disk Interface
#ifdef DH_DEBUG
    "DeskHop Debug",            // 7: Debug Interface
#endif
#ifdef DH_PATH_P
    "DeskHop Trackpad",         // 8: Path P precision touchpad
#endif
};

// String Descriptor Index
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MOUSE,
    STRID_VENDOR,
    STRID_DISK,
    STRID_DEBUG,
    STRID_TOUCHPAD,
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to
// complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count;

    // 2 (hex) characters for every byte + 1 '\0' for string end
    static char serial_number[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1] = {0};

    if (!serial_number[0]) {
       pico_get_unique_board_id_string(serial_number, sizeof(serial_number));
    }

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char *str = (index == STRID_SERIAL) ? serial_number : string_desc_arr[index];

        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

    return _desc_str;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#define EPNUM_HID          0x81
#define EPNUM_HID_REL_M    0x82
#define EPNUM_HID_VENDOR   0x83

#define EPNUM_MSC_OUT      0x04
#define EPNUM_MSC_IN       0x84

#define EPNUM_HID_TOUCHPAD 0x87  /* Path P precision touchpad input */

/* Path P adds one HID interface (touchpad) to both configurations.
   When DH_PATH_P is off, these expand to 0 / nothing. */
#ifdef DH_PATH_P
#define DH_PATH_P_ITF_COUNT  1
#define DH_PATH_P_DESC_LEN   TUD_HID_DESC_LEN
#else
#define DH_PATH_P_ITF_COUNT  0
#define DH_PATH_P_DESC_LEN   0
#endif

/* CDC contributes 2 interfaces (control + data) when DH_DEBUG is on. */
#ifdef DH_DEBUG
#define DH_DEBUG_ITF_COUNT   2
#define DH_DEBUG_DESC_LEN    TUD_CDC_DESC_LEN
#define EPNUM_CDC_NOTIF      0x85
#define EPNUM_CDC_OUT        0x06
#define EPNUM_CDC_IN         0x86
#else
#define DH_DEBUG_ITF_COUNT   0
#define DH_DEBUG_DESC_LEN    0
#endif

/* desc_configuration  (normal mode, no trackpad attached)
     interfaces (in order): HID main, HID relmouse,
                            [HID touchpad]?, [CDC ctrl, CDC data]?
     interface numbers:     0,        1,
                            2 (PATH_P),  next slots (DEBUG)        */
#define ITF_NUM_TOTAL        (2 + DH_PATH_P_ITF_COUNT + DH_DEBUG_ITF_COUNT)
#define CONFIG_TOTAL_LEN     (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN \
                              + DH_PATH_P_DESC_LEN + DH_DEBUG_DESC_LEN)

/* desc_configuration_config  (config mode)
     interfaces (in order): HID main, HID relmouse, HID vendor,
                            MSC, [HID touchpad]?, [CDC]?
     interface numbers:     0, 1, 2, 3, 4, 5, 6                  */
#define ITF_NUM_TOTAL_CONFIG (4 + DH_PATH_P_ITF_COUNT + DH_DEBUG_ITF_COUNT)
#define CONFIG_TOTAL_LEN_CFG (TUD_CONFIG_DESC_LEN + 3 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN \
                              + DH_PATH_P_DESC_LEN + DH_DEBUG_DESC_LEN)

/* CDC's bInterfaceNumber sits AFTER everything else in each config.
   Values are pinned per-config so the descriptor's interface numbers
   stay contiguous from 0..bNumInterfaces-1 (USB 2.0 spec 9.6.3). */
#define DH_NORMAL_ITF_HID_TOUCHPAD   (2)
#define DH_NORMAL_ITF_CDC            (2 + DH_PATH_P_ITF_COUNT)

#define DH_CONFIG_ITF_HID_TOUCHPAD   (4)
#define DH_CONFIG_ITF_CDC            (4 + DH_PATH_P_ITF_COUNT)


uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID,
                       STRID_PRODUCT,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report),
                       EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID_REL_M,
                       STRID_MOUSE,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_relmouse),
                       EPNUM_HID_REL_M,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),
#ifdef DH_PATH_P
    /* Path P precision touchpad. Reports MT contacts as PTP input
       reports; sized for a faster polling interval than the regular
       HID interfaces because gestures need low-latency updates. */
    TUD_HID_DESCRIPTOR(DH_NORMAL_ITF_HID_TOUCHPAD,
                       STRID_TOUCHPAD,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(ptp_descriptor),
                       EPNUM_HID_TOUCHPAD,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),
#endif
#ifdef DH_DEBUG
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(
        DH_NORMAL_ITF_CDC, STRID_DEBUG, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
#endif
};

uint8_t const desc_configuration_config[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_CONFIG, 0, CONFIG_TOTAL_LEN_CFG, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID,
                       STRID_PRODUCT,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report),
                       EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID_REL_M,
                       STRID_MOUSE,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_relmouse),
                       EPNUM_HID_REL_M,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID_VENDOR,
                       STRID_VENDOR,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_vendor),
                       EPNUM_HID_VENDOR,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),

    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC,
                       STRID_DISK,
                       EPNUM_MSC_OUT,
                       EPNUM_MSC_IN,
                       64),
#ifdef DH_PATH_P
    TUD_HID_DESCRIPTOR(DH_CONFIG_ITF_HID_TOUCHPAD,
                       STRID_TOUCHPAD,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(ptp_descriptor),
                       EPNUM_HID_TOUCHPAD,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),
#endif
#ifdef DH_DEBUG
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(
        DH_CONFIG_ITF_CDC, STRID_DEBUG, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
#endif
};

#ifdef DH_PATH_P
/* Trackpad-attached configuration: keyboard interface (no abs mouse
   in its report descriptor) + touchpad interface. The standard mouse
   and relmouse interfaces are omitted entirely so libinput sees only
   one pointer device on this seat (the touchpad), eliminating the
   dwtp-on suppression that hides the touchpad whenever a "trackpoint"
   is present.

   Interface numbers (contiguous, USB 2.0 9.6.3 requirement):
     0: HID main (keyboard + consumer + system)
     1: HID touchpad
     2,3: CDC (control + data) when DH_DEBUG=ON */
#define DH_TRACKPAD_ITF_HID            0
#define DH_TRACKPAD_ITF_HID_TOUCHPAD   1
#define DH_TRACKPAD_ITF_CDC            2
#define ITF_NUM_TOTAL_TRACKPAD         (2 + DH_DEBUG_ITF_COUNT)
#define CONFIG_TOTAL_LEN_TRACKPAD      (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + DH_DEBUG_DESC_LEN)

uint8_t const desc_configuration_trackpad[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_TRACKPAD, 0, CONFIG_TOTAL_LEN_TRACKPAD,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    /* Main HID interface: keyboard + consumer + system (no abs mouse) */
    TUD_HID_DESCRIPTOR(DH_TRACKPAD_ITF_HID,
                       STRID_PRODUCT,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_trackpad),
                       EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),

    /* Touchpad interface */
    TUD_HID_DESCRIPTOR(DH_TRACKPAD_ITF_HID_TOUCHPAD,
                       STRID_TOUCHPAD,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(ptp_descriptor),
                       EPNUM_HID_TOUCHPAD,
                       CFG_TUD_HID_EP_BUFSIZE,
                       1),
#ifdef DH_DEBUG
    TUD_CDC_DESCRIPTOR(
        DH_TRACKPAD_ITF_CDC, STRID_DEBUG, EPNUM_CDC_NOTIF, 8,
        EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
#endif
};
#endif /* DH_PATH_P */

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; // for multiple configurations

#ifdef DH_PATH_P
    /* Trackpad-attached takes precedence over config mode -- if both
       happen at once, the user wants their trackpad to work. */
    if (global_state.trackpad_attached)
        return desc_configuration_trackpad;
#endif

    if (global_state.config_mode_active)
        return desc_configuration_config;
    else
        return desc_configuration;
}
