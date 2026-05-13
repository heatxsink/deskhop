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
#ifdef DH_PASSTHROUGH
#include "passthrough.h"
#include "passthrough_uart.h"
#endif

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

                                        // https://github.com/raspberrypi/usb-pid
tusb_desc_device_t const desc_device_config = DEVICE_DESCRIPTOR(0x2e8a, 0x107c);

                                        // https://pid.codes/1209/C000/
tusb_desc_device_t const desc_device = DEVICE_DESCRIPTOR(0x1209, 0xc000);

#ifdef DH_PASSTHROUGH
/* Apple Inc. / Magic Trackpad 2 USB. Spoofed when DH_PASSTHROUGH is on
   and a Magic Trackpad is attached on the host port -- so the host PC's
   hid-magicmouse driver claims the device by VID/PID match. */
tusb_desc_device_t const desc_device_apple = DEVICE_DESCRIPTOR(0x05ac, 0x0265);

/* Apple-spoof predicate. True when a trackpad is attached locally OR
   the other Pico has one and shipped us the descriptor. The
   descriptor-ready check is belt-and-suspenders -- the cache must be
   filled before we present Apple, otherwise the host would request a
   descriptor we can't provide. */
static inline bool passthrough_should_spoof(void) {
    return (global_state.trackpad_attached
            || global_state.trackpad_remote_attached)
           && passthrough_descriptor_ready();
}
#endif

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
#ifdef DH_PASSTHROUGH
    if (passthrough_should_spoof())
        return (uint8_t const *)&desc_device_apple;
#endif
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

uint8_t const desc_hid_report_relmouse[] = {TUD_HID_REPORT_DESC_MOUSEHELP(HID_REPORT_ID(REPORT_ID_RELMOUSE))};

uint8_t const desc_hid_report_vendor[] = {TUD_HID_REPORT_DESC_VENDOR_CTRL(HID_REPORT_ID(REPORT_ID_VENDOR))};


// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
#ifdef DH_PASSTHROUGH
    /* Passthrough: only one HID interface (instance 0) and we serve
       the trackpad's own report descriptor verbatim. */
    if (passthrough_should_spoof()) {
        if (instance == 0)
            return passthrough_apple_hid_descriptor(NULL);
        return desc_hid_report;  /* should be unreachable */
    }
#endif

    if (global_state.config_mode_active)
        if (instance == ITF_NUM_HID_VENDOR)
            return desc_hid_report_vendor;

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

/* desc_configuration  (normal mode)
     interfaces (in order): HID main, HID relmouse, [CDC ctrl, CDC data]?
     interface numbers:     0,        1,            next slots (DEBUG)    */
#define ITF_NUM_TOTAL        (2 + DH_DEBUG_ITF_COUNT)
#define CONFIG_TOTAL_LEN     (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + DH_DEBUG_DESC_LEN)

/* desc_configuration_config  (config mode)
     interfaces (in order): HID main, HID relmouse, HID vendor, MSC, [CDC]?
     interface numbers:     0, 1, 2, 3, 4, 5                            */
#define ITF_NUM_TOTAL_CONFIG (4 + DH_DEBUG_ITF_COUNT)
#define CONFIG_TOTAL_LEN_CFG (TUD_CONFIG_DESC_LEN + 3 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN + DH_DEBUG_DESC_LEN)

/* CDC's bInterfaceNumber sits AFTER everything else in each config.
   Values are pinned per-config so the descriptor's interface numbers
   stay contiguous from 0..bNumInterfaces-1 (USB 2.0 spec 9.6.3). */
#define DH_NORMAL_ITF_CDC            (2)
#define DH_CONFIG_ITF_CDC            (4)


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
#ifdef DH_DEBUG
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(
        DH_CONFIG_ITF_CDC, STRID_DEBUG, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
#endif
};

#ifdef DH_PASSTHROUGH
/* Passthrough mode: store the trackpad's HID descriptor on host-port
   mount and re-emit it on our device port so the host PC's
   hid-magicmouse claims us by VID/PID match.

   Layout (single HID interface, no CDC, no other interfaces):
     [0..8]   configuration descriptor (9 bytes)
     [9..17]  interface descriptor      (9 bytes)
     [18..26] HID class descriptor      (9 bytes, includes report length)
     [27..33] endpoint descriptor       (7 bytes)
     total: 34 bytes

   Built once on mount and held in static RAM. Cleared on unmount. */
#define APPLE_HID_DESC_MAX        1024
#define PASSTHROUGH_CONFIG_LEN    (9 + 9 + 9 + 7)

static uint8_t  apple_hid_descriptor[APPLE_HID_DESC_MAX];
static uint16_t apple_hid_descriptor_len = 0;
static uint8_t  passthrough_config_descriptor[PASSTHROUGH_CONFIG_LEN];

void passthrough_cache_apple_descriptor(uint8_t const *desc, uint16_t len) {
    if (len == 0 || len > APPLE_HID_DESC_MAX) return;
    memcpy(apple_hid_descriptor, desc, len);

    /* Build the config descriptor BEFORE flipping descriptor-ready true,
       so passthrough_should_spoof() never returns true on a half-built
       buffer. Without this, core0's tud_descriptor_*_cb could read a
       mid-rebuild passthrough_config_descriptor if it polled descriptors
       outside the 50 ms re-enumeration window. */
    uint8_t *p = passthrough_config_descriptor;
    uint16_t total_len = PASSTHROUGH_CONFIG_LEN;

    /* Configuration descriptor (USB 2.0 9.6.3) */
    *p++ = 9; *p++ = TUSB_DESC_CONFIGURATION;
    *p++ = (uint8_t)(total_len & 0xFF);
    *p++ = (uint8_t)(total_len >> 8);
    *p++ = 1;     /* bNumInterfaces */
    *p++ = 1;     /* bConfigurationValue */
    *p++ = 0;     /* iConfiguration */
    *p++ = TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP | 0x80;
    *p++ = 250;   /* bMaxPower (500 mA / 2) */

    /* Interface descriptor */
    *p++ = 9; *p++ = TUSB_DESC_INTERFACE;
    *p++ = 0;     /* bInterfaceNumber */
    *p++ = 0;     /* bAlternateSetting */
    *p++ = 1;     /* bNumEndpoints */
    *p++ = TUSB_CLASS_HID;
    *p++ = 0;     /* bInterfaceSubClass: no boot */
    *p++ = 0;     /* bInterfaceProtocol: none */
    *p++ = 0;     /* iInterface */

    /* HID class descriptor (HID 1.11 6.2.1) */
    *p++ = 9; *p++ = HID_DESC_TYPE_HID;
    *p++ = 0x11; *p++ = 0x01;          /* bcdHID 1.11 */
    *p++ = 0;                           /* bCountryCode */
    *p++ = 1;                           /* bNumDescriptors */
    *p++ = HID_DESC_TYPE_REPORT;
    *p++ = (uint8_t)(len & 0xFF);
    *p++ = (uint8_t)(len >> 8);

    /* Endpoint descriptor (interrupt IN, 1 ms poll, 64-byte max packet
       -- enough for any Apple report). EP address 0x81 to match what
       TinyUSB allocates for HID instance 0. */
    *p++ = 7; *p++ = TUSB_DESC_ENDPOINT;
    *p++ = 0x81;
    *p++ = TUSB_XFER_INTERRUPT;
    *p++ = 64; *p++ = 0;
    *p++ = 1;

    /* All static buffers are now fully populated. Flip descriptor-ready
       true LAST so the spoof predicate can't transition true on a
       half-built passthrough_config_descriptor. */
    apple_hid_descriptor_len = len;
}

void passthrough_clear_apple_descriptor(void) {
    apple_hid_descriptor_len = 0;
}

bool passthrough_descriptor_ready(void) {
    return apple_hid_descriptor_len > 0;
}

/* Debounce state for trackpad mount/unmount. Set non-zero by
   tuh_hid_umount_cb when the trackpad's mouse-class interface
   disappears; cleared by tuh_hid_mount_cb if the trackpad reappears
   within the window; committed by passthrough_tick_unmount_debounce
   if the window expires without a remount. */
uint64_t passthrough_unmount_at_us = 0;

/* Once we've gone Apple-spoofed, stay Apple-spoofed until the firmware
   reboots. PIO-USB on RP2040 drops the Magic Trackpad for arbitrary
   sub-tens-of-seconds windows that no static debounce reliably swallows
   (we tried 500 ms and 2 s; both bounced). Sticky mode trades the
   ability to auto-revert to deskhop on real unplug for a stable host-
   facing identity. The host PC sees one Apple trackpad until deskhop
   is power-cycled, which matches the user's mental model better than
   "the trackpad keeps disappearing for 200 ms every few seconds." */
#define PASSTHROUGH_UNMOUNT_DEBOUNCE_US  0xFFFFFFFFFFFFFFFFULL

/* Polled from usb_host_task. Commits a pending unmount only when the
   debounce window has elapsed, so the host PC isn't subjected to a
   re-enumeration storm every time PIO-USB blinks. */
void passthrough_tick_unmount_debounce(void) {
    if (passthrough_unmount_at_us == 0) return;
    if (time_us_64() - passthrough_unmount_at_us < PASSTHROUGH_UNMOUNT_DEBOUNCE_US) return;
    passthrough_unmount_at_us = 0;
    if (global_state.trackpad_attached) {
        /* Tell the other Pico the trackpad is gone so it can drop its
           mirrored descriptor cache. No-op in sticky mode (this whole
           function is gated by an unreachable timer comparison there)
           but kept correct for the non-sticky path. */
        send_value(0, TRACKPAD_PRESENCE_MSG);

        passthrough_clear_apple_descriptor();
        global_state.trackpad_attached    = false;
        global_state.reenumerate_pending  = true;
    }
}

uint8_t const *passthrough_apple_hid_descriptor(uint16_t *out_len) {
    if (out_len) *out_len = apple_hid_descriptor_len;
    return apple_hid_descriptor;
}

/* Phase 2B receive-side hook. The host-port Pico chunks its trackpad's
   HID descriptor over UART when the trackpad mounts; the reassembled
   buffer lands here and we cache it locally. Phase 2C extends this to
   trigger device-side re-enumeration on receivers that don't have a
   local trackpad -- the spoof predicate transitions false -> true on
   the back of this cache fill, so we need the host to re-read our
   descriptors and bind hid-magicmouse. */
static void passthrough_on_remote_descriptor(const uint8_t *data, uint16_t len) {
#ifdef DH_DEBUG_TRACKPAD
    dh_debug_printf("PASSTHROUGH: remote descriptor received (%u bytes)\n", len);
#endif
    passthrough_cache_apple_descriptor(data, len);

    /* With a local trackpad we're already Apple-spoofed (Phase 1 sticky).
       Without one, this cache fill is what enables the spoof -- ask
       core0 to drop the USB pull-up so the host re-reads descriptors. */
    if (!global_state.trackpad_attached) {
        global_state.reenumerate_pending = true;
    }
}

/* Phase 2C receive-side frame emission. The host-port Pico chunks the
   raw Apple frame over UART when this side is the active output; we
   re-emit the bytes verbatim to our spoofed Apple HID interface.
   hid-magicmouse on the host decodes the proprietary frame format,
   exactly as if the trackpad were plugged in directly. */
static void passthrough_on_remote_frame(const uint8_t *data, uint16_t len) {
    /* Only emit when we're actually the active output AND our spoof is
       live. The sender already gates on its own non-active status to
       know to forward -- but the active output can flip mid-stream and
       leave a chunk in flight, so re-check on receive. */
    if (!CURRENT_BOARD_IS_ACTIVE_OUTPUT || !passthrough_descriptor_ready())
        return;
    if (len == 0) return;
    uint8_t report_id = data[0];
    bool emitted = tud_hid_n_report(0, report_id, &data[1], (uint16_t)(len - 1));
#ifdef DH_DEBUG_TRACKPAD
    /* Throttled drop-rate logger: 1 line/sec showing the running attempts
       and drops since boot. At 100-250 Hz steady-state with a healthy
       host EP, drops should stay near zero. A growing drop counter
       signals host-side back-pressure on the IN endpoint. */
    static uint32_t emit_attempts = 0, emit_drops = 0, last_log_us = 0;
    emit_attempts++;
    if (!emitted) emit_drops++;
    uint32_t now_us = time_us_32();
    if (now_us - last_log_us > 1000000) {
        last_log_us = now_us;
        dh_debug_printf("PASSTHROUGH rx: emit attempts=%lu drops=%lu\n",
                        (unsigned long)emit_attempts,
                        (unsigned long)emit_drops);
    }
#else
    (void)emitted;
#endif
}

void passthrough_init(void) {
    passthrough_uart_register(TRACKPAD_DESC_CHUNK_MSG,
                              passthrough_on_remote_descriptor);
    passthrough_uart_register(TRACKPAD_FRAME_CHUNK_MSG,
                              passthrough_on_remote_frame);
}
#endif /* DH_PASSTHROUGH */

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; // for multiple configurations

#ifdef DH_PASSTHROUGH
    /* Passthrough takes precedence over everything else -- when a
       trackpad exists anywhere on the deskhop and we've cached its
       descriptor, expose ourselves as that trackpad. */
    if (passthrough_should_spoof())
        return passthrough_config_descriptor;
#endif

    if (global_state.config_mode_active)
        return desc_configuration_config;
    else
        return desc_configuration;
}
