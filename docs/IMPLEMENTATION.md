# Magic Trackpad Support — Implementation

Companion to [DESIGN.md](DESIGN.md). Each phase is self-contained and shippable.

---

## Phase 1 — Multi-touch decode + scroll synthesis

Detect Magic Trackpad by VID/PID, decode the Apple multi-touch payload, emit standard wheel/pan deltas through the existing mouse pipeline. No device-side descriptor changes.

### Step 1.1 — Descriptor and report logging (validation prerequisite)

Before writing any decoder, confirm what Pico-PIO-USB actually enumerates and what the trackpad sends.

- In `tuh_hid_mount_cb` (`src/usb.c:145`), add a debug printf dumping `dev_addr`, `instance`, `desc_len`, `vid`, `pid`, and the descriptor bytes as hex.
- In `tuh_hid_report_received_cb` (`src/usb.c:212`), gate a hex dump of the raw report behind a VID match to `0x05AC` so we don't drown the log with non-Apple traffic.
- Build and flash the source-side board. Plug in the trackpad. Capture logs over the debug CDC if `DH_DEBUG` is on, otherwise temporary `printf` to a known UART.
- Confirm:
  1. Which of the four advertised HID interfaces actually mount on Pico-PIO-USB.
  2. Report ID `0x02` is sent and carries multi-touch data (not standard mouse X/Y).
  3. Report length matches what the Linux `hid-magicmouse.c` driver expects for the trackpad-2 USB variant.

This step ships no behavior. It produces a one-page note that locks down the report layout for Step 1.3.

### Step 1.1 — Findings (recorded)

VID 0x05AC / PID 0x0265 (Magic Trackpad 2 USB) enumerated through deskhop:

| Instance | itf_protocol | Descriptor size | Role |
|---|---|---|---|
| 0 | 0 (none) | 83 B | Power management (Report ID 0x90), vendor 0xE0/0x9A |
| 1 | 2 (mouse) | 110 B | **Standard mouse (Report ID 0x02), vendor 0x3F (16 B), vendor 0x44 (1387 B feature)** |
| 2 | 0 (none) | 36 B | **Multi-touch vendor: Report ID 0x3F IN (15 B), Report ID 0x53 OUT (63 B)** |
| 3 | 0 (none) | 27 B | Vendor 0xC0 (firmware update?) |

Pico-PIO-USB enumerates all four interfaces — the biggest risk in DESIGN.md is eliminated.

In the as-shipped state (no driver activation), only Instance 1 sends data. Reports are 8 bytes on Report ID 0x02 in classic mouse format (`02 BB XX YY 00 00 00 NN` — buttons / X delta / Y delta / 4 padding). Instance 2 (the multi-touch interface) is completely silent. The 110-byte descriptor on Instance 1 also advertises Report ID 0x3F (16 B input) and Report ID 0x44 (1387 B feature) — neither is sent in this state.

The trackpad needs a multi-touch activation feature report before it switches Report ID 0x02 from mouse-emulation mode to multi-touch mode. Linux (`drivers/hid/hid-magicmouse.c`) sends this for `USB_DEVICE_ID_APPLE_MAGICTRACKPAD2`:

```c
static const u8 feature_mt_trackpad2_usb[] = { 0x02, 0x01 };
hid_hw_raw_request(hdev, 0x02, buf, 2, HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
```

Translated to TinyUSB host: `SET_REPORT(Feature)` to Instance 1, Report ID `0x02`, **payload `{0x02, 0x01}` (2 bytes including the report ID prefix)**. The trackpad expects the report ID byte at the start of the wire payload — Linux's `usbhid_set_raw_report` sends the buffer as-is (`{0x02, 0x01}`, 2 bytes), but TinyUSB's `tuh_hid_set_report` only encodes the report ID in `wValue` and ships `len` bytes verbatim. So the report ID prefix has to be included in the buffer. Sending only `{0x01}` (1 byte) causes the trackpad to STALL the control transfer (`set_report_complete_cb` fires with `len=0` per `hid_host.c:281`). Once enabled, Report ID 0x02 reports grow significantly and carry multi-touch frames.

### Step 1.2 — Multi-touch activation

At mount, after parsing the descriptor, if `is_apple_magic_trackpad(iface)` is true and `itf_protocol == HID_ITF_PROTOCOL_MOUSE` (i.e., Instance 1, the standard mouse interface), send the activation feature report:

```c
static const uint8_t magic_trackpad_activate[] = { 0x01 };
tuh_hid_set_report(dev_addr, instance, 0x02, HID_REPORT_TYPE_FEATURE,
                   (void *)magic_trackpad_activate, sizeof(magic_trackpad_activate));
```

Implement `tuh_hid_set_report_complete_cb` to log success/failure (only useful with `DH_DEBUG_TRACKPAD`).

While we lack the multi-touch decoder, sending activation produces reports that the existing mouse pipeline cannot interpret — the user's cursor will go haywire. So:

- **Gate activation behind `DH_DEBUG_TRACKPAD`.** Production builds remain unaffected.
- **Under `DH_DEBUG_TRACKPAD`, also bypass the existing mouse pipeline for trackpad reports** (drop them after logging) so the cursor stays still rather than thrashing. The user loses trackpad input on debug builds — expected, plug a regular mouse alongside.

### Step 1.3 — Device identification (DONE)

`hid_interface_t` now carries `vendor_id` and `product_id`, populated at mount via `tuh_vid_pid_get` and read on every report. Phase 1 trackpad logic no longer queries TinyUSB on the hot path.

The activation feature report, the multi-touch decoder, the gesture state machine, and the pipeline integration are no longer gated on `DH_DEBUG_TRACKPAD` — they ship in default (production) builds. `DH_DEBUG_TRACKPAD` retains its original purpose as a verbose-logging flag (HID descriptor hex dumps at mount, per-frame 3-finger gesture state) for future work.

#### Step 1.3 — Device identification (original plan)

Extend the per-interface state to remember VID and PID so the report path can branch on them without re-querying TinyUSB on every report.

- In `src/include/structs.h`, add `uint16_t vendor_id; uint16_t product_id;` to `hid_interface_t`.
- In `tuh_hid_mount_cb` (`src/usb.c:145`), call `tuh_vid_pid_get(dev_addr, &vid, &pid)` and store on `iface`.
- In `src/utils.c`, add:
  ```c
  static inline bool is_apple_magic_trackpad(hid_interface_t *iface) {
      return iface->vendor_id == 0x05AC && iface->product_id == 0x0265;
  }
  ```
  Declare in `src/include/utils.h` (or `misc.h` — match existing convention).

### Step 1.4 — Apple multi-touch decoder

**Wire format locked down** from post-activation captures (see `docs/captures/trackpad-multitouch-frames.txt`):

- Report ID `0x02` on Instance 1.
- 12-byte header: `[02][...legacy padding 7B][0x31][ts_lo][ts_hi][0x23]`.
- 9 bytes per finger; finger count = `(len - 12) / 9`.
- X = signed 13-bit packed in finger bytes 0..1: `((t[1]<<27) | (t[0]<<19)) >> 19`.
- Y = signed 13-bit packed in finger bytes 1..3, negated: `-(((t[3]<<30) | (t[2]<<22) | (t[1]<<14)) >> 19)`.
- Finger byte 4 = touch_major, 5 = touch_minor, 6 = size, 7 = pressure/state, 8 packs orientation (top 3 bits +32) and finger id (low 4 bits).

Note that finger byte 1 participates in **both** X and Y — the bit-packing crosses byte boundaries.

New module `src/magic_trackpad.c` + `src/include/magic_trackpad.h`. Reference: Linux `drivers/hid/hid-magicmouse.c`, specifically the trackpad-2 USB report handler.

```c
#define MT_MAX_FINGERS 11

typedef struct {
    int16_t  x;
    int16_t  y;
    uint8_t  state;        // 0 = not present, 1 = touching, 2 = lifted, etc.
    uint8_t  finger_id;
} mt_touch_t;

typedef struct {
    uint8_t     button;
    uint8_t     finger_count;
    mt_touch_t  fingers[MT_MAX_FINGERS];
} mt_frame_t;

bool decode_magic_trackpad_report(const uint8_t *raw, int len, mt_frame_t *out);
```

Implementation notes:
- The trackpad packs a header followed by N per-finger records (~9 bytes each on USB MT2). Decode all records, populate `fingers[]`, set `finger_count`.
- `state` encodes lifecycle (new touch, hover, contact, leaving). We need the contact/leaving distinction to drive the gesture state machine.
- Sign-extend signed 12/13-bit X/Y fields correctly. Get this wrong and scroll direction inverts intermittently.
- Pure decode only. No gesture logic in this file.

### Step 1.5 — Gesture state machine

New module `src/magic_trackpad_gestures.c` + header. Owns per-trackpad gesture state. Persists across reports.

```c
typedef struct {
    int16_t  last_x[MT_MAX_FINGERS];
    int16_t  last_y[MT_MAX_FINGERS];
    bool     active[MT_MAX_FINGERS];
    uint8_t  prev_finger_count;
} mt_gesture_state_t;

void mt_gesture_init(mt_gesture_state_t *s);
void mt_gesture_step(mt_gesture_state_t *s, mt_frame_t *frame, mouse_values_t *out);
```

`mt_gesture_step` rules:
- `finger_count == 1`: emit pointer delta in `out->move_x`/`out->move_y` (delta vs last position of that finger).
- `finger_count == 2`: emit `out->wheel` (vertical, averaged Y delta of the two fingers) and `out->pan` (horizontal, averaged X delta). Sign convention matches macOS natural-scroll-off default; user-facing direction can be flipped later in config.
- `finger_count >= 3`: drop in Phase 1. Reserved for Phase 2.
- `prev_finger_count == 0 && finger_count > 0`: don't emit a delta for the first frame (no prior position; would produce a giant jump).
- `finger_count` change: reset deltas, do not emit.

Store `mt_gesture_state_t` inside `hid_interface_t` (anonymous union with the existing mouse parser state, or a separate field — match existing structure conventions). Reset on disconnect in `tuh_hid_umount_cb`.

### Step 1.6 — Route Apple reports

In `tuh_hid_report_received_cb` (`src/usb.c:212`), before the standard report-ID dispatch:

```c
if (is_apple_magic_trackpad(iface) && report_id == 0x02) {
    mt_frame_t frame;
    if (decode_magic_trackpad_report(report, len, &frame)) {
        mouse_values_t values = {0};
        mt_gesture_step(&iface->mt_state, &frame, &values);
        values.buttons = frame.button;

        device_t *state = &global_state;
        enum screen_pos_e dir = update_mouse_position(state, &values);
        mouse_report_t mr = create_mouse_report(state, &values);
        output_mouse_report(&mr, state);
        if (dir != NONE) do_screen_switch(state, dir);
    }
    tuh_hid_receive_report(dev_addr, instance);
    return;
}
```

This bypasses `extract_report_values()` (which would misread the descriptor) and routes through the same downstream as `process_mouse_report` (`src/mouse.c:321`).

### Step 1.7 — Validation

| Test | Expected |
|---|---|
| macOS Safari, two-finger scroll on long page | Smooth, monotonic, no cursor jitter |
| Linux Firefox, two-finger scroll | Same |
| Windows Edge, two-finger scroll | Same |
| Mouse-drag cursor across screen edge | Switches output, as today |
| Plug regular USB mouse alongside trackpad | Both work, no regression |
| Unplug + replug trackpad | Re-detected, scroll still works, no stale gesture state |
| Boot mode (`force_mouse_boot_mode`) | Falls back to deskhop's existing mouse path; scroll won't work but cursor still moves (documented limitation) |

### Phase 1 file delta

- New: `src/magic_trackpad.c`, `src/magic_trackpad_gestures.c`, `src/include/magic_trackpad.h`.
- Modified: `src/include/structs.h` (2 new fields), `src/usb.c` (mount + report dispatch), `src/utils.c` (1 helper), `CMakeLists.txt` (compile new sources).
- Unchanged: `src/usb_descriptors.c`, all device-side descriptors, UART protocol.

---

## Phase 2 — Apple VID/PID spoof + raw multi-touch passthrough

Builds on Phase 1. Adds a dynamic device descriptor, mirrors the trackpad's vendor multi-touch interface, forwards multi-touch reports verbatim across the UART. macOS binds its real trackpad driver.

### Step 2.1 — macOS feature-report probing capture

Before changing descriptors, capture macOS's actual driver-bind behavior. Plug the trackpad directly into a Mac, run a USB capture (e.g., Wireshark with USBPcap or macOS USB Prober), and identify:
- Any feature reports (`GET_REPORT` / `SET_REPORT` on report types `0x03`) macOS sends during bind.
- Any periodic OUT reports during normal operation.

This determines whether Phase 2 needs unidirectional (host → device only) or bidirectional report forwarding. Plan assumes bidirectional; confirm before designing the UART transport.

### Step 2.2 — Trackpad device descriptor

In `src/usb_descriptors.c`:

```c
tusb_desc_device_t const desc_device_trackpad = DEVICE_DESCRIPTOR(0x05AC, 0x0265);
```

Update `tud_descriptor_device_cb` to a 3-way select:
1. `config_mode_active` → `desc_device_config` (config UI takes priority).
2. `global_state.trackpad_active` → `desc_device_trackpad`.
3. else → `desc_device`.

### Step 2.3 — Trackpad configuration descriptor

Add `desc_hid_report_trackpad[]` mirroring the trackpad's 36-byte vendor descriptor (Report ID `0x3F` IN, Report ID `0x53` OUT, usage page `0xFF00`, usage `0x0D`). Copy from Phase 1's captured descriptor — do not retype from the issue dump.

Add `desc_configuration_trackpad[]` containing:
- Existing keyboard/mouse/consumer/system HID interface (so non-trackpad input still flows).
- New trackpad HID interface with the vendor descriptor above.
- No MSC, no vendor config interface (this descriptor is only used in normal mode, not config mode).

Update `tud_hid_descriptor_report_cb` and `tud_descriptor_configuration_cb` with matching 3-way selects.

Define a new `ITF_NUM_HID_TRACKPAD` and a new endpoint number (`EPNUM_HID_TRACKPAD = 0x84` or next free).

### Step 2.4 — UART transport for raw HID reports

New packet types in `src/include/protocol.h`:
- `MOUSE_RAW_IN_MSG`: source-side board → device-side board. Carries Report ID `0x3F` payloads from the trackpad to forward to the device-side host PC.
- `MOUSE_RAW_OUT_MSG`: device-side board → source-side board. Carries Report ID `0x53` payloads from the host PC to forward to the trackpad.

Existing protocol uses fixed-length packets (`RAW_PACKET_LENGTH`). Trackpad reports are 15 bytes IN, 63 bytes OUT — fits comfortably, but verify against current packet payload size in `src/include/packet.h` and bump if needed. Add a length byte if reports turn out to be variable-length.

Update `validate_packet` and `process_packet` (`src/protocol.c`) to accept the new types.

### Step 2.5 — Source-side: forward IN reports

In `tuh_hid_report_received_cb` (`src/usb.c:212`), when `is_apple_magic_trackpad(iface)` and `report_id == 0x3F`:
- If we are also the active output: drop into Phase 1's decoder + emit synthesized scroll (so the local host PC, which sees the spoofed trackpad, gets the *raw* report; we don't need to also synthesize). Actually: under Phase 2, the active-output board sends the raw report to its own device-side stack via `tud_hid_n_report(ITF_NUM_HID_TRACKPAD, 0x3F, ...)`. The Phase 1 decoder is no longer in the active-output path for the trackpad.
- If we are not the active output: package as `MOUSE_RAW_IN_MSG` and send over UART.

Resolve the active-vs-remote split the same way `output_mouse_report` already does (`src/mouse.c:125`).

### Step 2.6 — Device-side: deliver IN reports

When the device-side board receives `MOUSE_RAW_IN_MSG`, call `tud_hid_n_report(ITF_NUM_HID_TRACKPAD, 0x3F, payload, len)`. Same queueing pattern as the existing mouse queue if needed (`process_mouse_queue_task` in `src/mouse.c:346`).

### Step 2.7 — Device-side: capture OUT reports

In `tud_hid_set_report_cb` (`src/usb.c:39`), match on `instance == ITF_NUM_HID_TRACKPAD && report_id == 0x53 && report_type == HID_REPORT_TYPE_OUTPUT`. Forward the buffer to the source-side board as `MOUSE_RAW_OUT_MSG`.

If macOS sends feature reports (TBD by step 2.1), extend `tud_hid_get_report_cb` to bridge those too. Adds round-trip latency, but feature reports are typically infrequent.

### Step 2.8 — Source-side: deliver OUT reports

When the source-side board receives `MOUSE_RAW_OUT_MSG`, call `tuh_hid_set_report(dev_addr, instance, 0x53, HID_REPORT_TYPE_OUTPUT, payload, len)`.

### Step 2.9 — Cursor switching policy

Add `state->trackpad_active` (`src/include/structs.h`). Set on Apple Magic Trackpad mount, clear on unmount.

In `do_screen_switch` (`src/mouse.c:241`), early-return if `state->trackpad_active`. Cursor-edge switching is meaningless under raw passthrough because deskhop can no longer see absolute X/Y. Switching falls back to the existing keyboard shortcut (`Ctrl + Caps Lock`).

Document the behavior change in the README near the trackpad note.

### Step 2.10 — Enumeration lifecycle

On trackpad mount on the source-side board, signal the device-side board (new UART message, `TRACKPAD_MOUNT_MSG`). Device-side board:
1. Sets `trackpad_active = true`.
2. Calls `tud_disconnect()`.
3. Brief delay (~100 ms — match TinyUSB recommendations).
4. Calls `tud_connect()`.

Host PC re-enumerates and sees the spoofed Apple trackpad.

On trackpad unmount: reverse — `trackpad_active = false`, disconnect, reconnect. Host re-enumerates back to the default deskhop descriptor.

If the user enters config mode while the trackpad is connected: config mode wins. Disconnect, swap to config descriptor, reconnect. On exit, swap back to trackpad descriptor.

### Step 2.11 — Validation

| Test | Expected |
|---|---|
| macOS System Settings → Trackpad pane | Visible, all options present |
| Tap-to-click toggle | Works when enabled; physical click works regardless |
| Two-finger scroll | Native macOS scroll behavior, same as direct connection |
| Three-finger swipe between desktops | Works |
| Four-finger Mission Control | Works |
| Switch outputs via keyboard shortcut | Trackpad continues to work on new output |
| Mouse-drag cursor switching | Disabled while trackpad is the active pointer |
| Regular USB mouse plugged alongside trackpad | Mouse works; mouse-drag switching still works |
| Linux output | `hid-magicmouse` binds; gestures work |
| Windows output | Basic mouse functionality; advanced gestures NOT expected (document as known limitation) |
| Unplug trackpad | Device re-enumerates as default deskhop; mouse devices still work |
| Enter config mode with trackpad attached | Config UI works; on exit, trackpad re-binds |

### Phase 2 file delta

- New: `desc_device_trackpad`, `desc_configuration_trackpad`, `desc_hid_report_trackpad` in `src/usb_descriptors.c`.
- New UART packet types in `src/include/protocol.h`, handlers in `src/protocol.c`.
- Modified: `src/usb.c` (descriptor selection, report routing, set_report capture), `src/include/structs.h` (`trackpad_active` flag, possibly enlarged packet payload), `src/mouse.c` (`do_screen_switch` early-return), `src/include/usb_descriptors.h` (new ITF / EPNUM constants).
- Phase 1's gesture decoder is retained as a fallback for boot-mode and as documentation.

---

## Cross-cutting

### Logging / debug
Phase 1 step 1.1 leaves debug code behind. Gate it behind `DH_DEBUG_TRACKPAD` and remove the unconditional dumps before merge.

### Memory budget
RP2040 has tight RAM. `mt_gesture_state_t` is ~50 bytes per interface; trivial. Phase 2 raw-report buffers should be allocated statically, not on the stack.

### Test rigs
- macOS host on Output A, Linux host on Output B is the primary rig (the user's hardware).
- A Windows host plugged in temporarily for Phase 2 step 2.11 final validation.
- Regular USB mouse for non-regression testing.

### Out of scope (this design)
- A general HID passthrough mode (issue #207 option 2 broader interpretation). If pursued, it would obsolete Phase 2's per-device spoof in favor of a general framework — but Phase 2 stands on its own and is the right answer for the trackpad case specifically.
