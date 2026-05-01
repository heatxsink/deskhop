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

## Phase 2 — Apple VID/PID spoof + raw multi-touch passthrough (ABANDONED)

> **Status: abandoned.** The descriptor swap worked at the binding layer but `hid-magicmouse` claims all HID interfaces of any Apple-VID device, breaking deskhop's keyboard. No userspace workaround possible (`hid-generic` refuses to bind when a more-specific driver matches). All firmware changes were reverted in commits `5c747ab` and `8303bd0`. Source captures and protocol research stay because the same HID descriptors and activation payload feed Path A. See DESIGN.md "Status" for the abandonment narrative.

The original Phase 2 plan is preserved verbatim below as a record. Do not implement.



Make macOS see a real Apple Magic Trackpad. macOS binds its native driver. Tap-to-click, gestures, the System Settings pane all work natively. Deskhop becomes a transport layer for the trackpad's proprietary HID reports — no decoding, no synthesis.

### What we already have (carried from Phase 1 work)

- Magic Trackpad detection by VID/PID (`hid_interface_t.vendor_id` / `product_id` cached at mount).
- Activation feature report code (`send_magic_trackpad_activation`) — gated behind `DH_TRACKPAD_PHASE1` today; Phase 2 pulls it out so it always runs when a trackpad is detected.
- All four trackpad HID descriptors captured verbatim in `docs/captures/trackpad-mount-and-baseline.txt`.
- Multi-touch wire format documented in `docs/captures/trackpad-multitouch-frames.txt` (informational only — Phase 2 doesn't decode).
- `DH_DEBUG_TRACKPAD` and `DH_DEBUG_TRACKPAD_LED` diagnostic flags.

### What we know about deskhop's existing internals

- `PACKET_DATA_LENGTH = 8` bytes per UART packet (`src/include/packet.h:37`). Trackpad reports are 15-39 bytes IN, up to 63 bytes OUT — does not fit a single existing packet, so Phase 2 needs either fragmentation across multiple packets OR a new wider packet type.
- Production builds advertise 2 HID interfaces; debug builds 3 (adds CDC). Config mode swaps to a 4-interface descriptor (adds vendor + MSC). Phase 2 adds a 4th non-config-mode interface for the trackpad.
- `tud_descriptor_device_cb` and `tud_descriptor_configuration_cb` already do conditional descriptor selection based on `config_mode_active`. Trackpad mode adds a third arm.

### Open questions to settle before code

1. **Mirror which trackpad interfaces?** The trackpad exposes 4. The minimum macOS likely needs is the standard mouse interface (Instance 1, descriptor 110 B) and the multi-touch vendor interface (Instance 2, descriptor 36 B). Instances 0 (vendor power management) and 3 (vendor firmware update) are probably skippable. **Default plan: mirror Instances 1 and 2; revisit if macOS refuses to bind the trackpad driver.**

2. **UART packet format for raw HID reports.** Two options:
   - **(A) Fragmentation across existing 8-byte packets.** Reports up to 63 B → up to 8 packets per report. Need a small header (sequence + offset) so the receiver can reassemble. Smaller blast radius — no changes to `PACKET_DATA_LENGTH` or any other packet type.
   - **(B) Bump `PACKET_DATA_LENGTH` to 64 bytes globally.** Simpler code but every existing packet type now wastes 56 bytes per UART transfer. UART is 1 Mbps — wasted bandwidth becomes noticeable for keyboard/mouse latency.
   - **Default plan: option A (fragmentation).** Implement at the UART transport layer so the trackpad code only sees full reports.

3. **Composite device strangeness on Apple VID/PID.** When trackpad mode is active, the device descriptor advertises Apple VID `0x05AC` / PID `0x0265`. But the deskhop also presents its own keyboard/mouse interfaces under that VID/PID. macOS may or may not balk at "Apple Magic Trackpad 2 with extra keyboard interface". **Risk; needs in-situ test. Mitigation if it fails: present trackpad-only mode (keyboard goes through deskhop's other Pico).** Bringup step 2.1 verifies.

4. **What happens to the keyboard/mouse on the active output during trackpad-mode enumeration cycling?** When the trackpad mounts, the device-side board does `tud_disconnect()` → 100 ms → `tud_connect()`. The host PC briefly loses its keyboard. Acceptable as a one-time cost on plug-in; needs to be flagged in the README.

5. **Feature-report probing during driver bind.** Confirmed (via Linux kernel source) that the only feature-report traffic is the activation `SET_REPORT` we already issue, plus optional battery `GET_REPORT` polling. **OUT-from-host forwarding still needed for the activation request the host sends to the spoofed trackpad** — but no other traffic. macOS may differ; out of scope for now.

### Step 2.1 — Protocol research (DONE)

Originally planned as a macOS USB capture session. Replaced by reading the Linux kernel driver `drivers/hid/hid-magicmouse.c` directly — the user's target is Linux GNOME, and the kernel source is authoritative.

Findings (cross-referenced against captures already on disk):

| Question | Answer | Source |
|---|---|---|
| Activation feature report payload | `{0x02, 0x01}` | `hid-magicmouse.c:749` (already matches our `magic_trackpad_activate[]`) |
| Activation transport | `SET_REPORT(Feature)`, Report ID `0x02` | `hid-magicmouse.c:782` |
| Bind-time control transfers | `hid_parse` + `hid_hw_start` only | `magicmouse_probe`, `hid-magicmouse.c:855` |
| Periodic OUT reports during use | None | No write paths in driver outside activation |
| Battery polling | Optional `GET_REPORT` for battery report ID | `hid-magicmouse.c:815` (non-fatal if we ignore) |
| IN report IDs Linux expects | `0x02` (TRACKPAD2_USB_REPORT_ID) | `hid-magicmouse.c:923` |

**No additional captures required.** The trackpad-side behavior was captured in Phase 1 (`docs/captures/`); the host-side behavior is in the kernel source. We have everything to design the transport layer.

### Step 2.2 — Trackpad device descriptor

In `src/usb_descriptors.c`:

```c
tusb_desc_device_t const desc_device_trackpad = DEVICE_DESCRIPTOR(0x05AC, 0x0265);
```

Update `tud_descriptor_device_cb` to a 3-way select with priority `config > trackpad > default`:
```c
if (global_state.config_mode_active) return desc_device_config;
if (global_state.trackpad_mode_active) return desc_device_trackpad;
return desc_device;
```

Add `trackpad_mode_active` field to `device_t` (`src/include/structs.h`).

String descriptors: keep `iManufacturer` and `iProduct` deskhop-branded. macOS uses VID/PID for binding decisions; strings are display only and macOS will show "Apple Magic Trackpad 2" in the trackpad pref pane regardless. Mismatched strings are cosmetic. (If Step 2.1 finds macOS rejects bind on string mismatch, swap to "Apple"/"Magic Trackpad 2" strings under trackpad mode.)

### Step 2.3 — Trackpad configuration descriptor

New report descriptors in `src/usb_descriptors.c`:
```c
// Mirror Instance 1 of trackpad (110 bytes; standard mouse + vendor 0x3F input + vendor 0x44 feature)
uint8_t const desc_hid_report_trackpad_mouse[] = { /* paste from docs/captures/ */ };

// Mirror Instance 2 of trackpad (36 bytes; vendor multi-touch, Report ID 0x3F IN, 0x53 OUT)
uint8_t const desc_hid_report_trackpad_mt[] = { /* paste from docs/captures/ */ };
```

New configuration descriptor `desc_configuration_trackpad[]` with interfaces:
1. Existing deskhop keyboard/mouse/consumer/system (`ITF_NUM_HID`, `STRID_PRODUCT`, `desc_hid_report`)
2. Existing relative-mouse helper (`ITF_NUM_HID_REL_M`, `STRID_MOUSE`, `desc_hid_report_relmouse`)
3. New trackpad mouse (`ITF_NUM_HID_TRACKPAD_MOUSE`, dedicated string, `desc_hid_report_trackpad_mouse`)
4. New trackpad multi-touch (`ITF_NUM_HID_TRACKPAD_MT`, dedicated string, `desc_hid_report_trackpad_mt`)

Define new endpoints. Current normal mode uses `EPNUM_HID = 0x81`, `EPNUM_HID_REL_M = 0x82`. Trackpad mode adds `EPNUM_HID_TRACKPAD_MOUSE = 0x83` and `EPNUM_HID_TRACKPAD_MT = 0x84`. The multi-touch interface needs OUT too (Report ID 0x53): `EPNUM_HID_TRACKPAD_MT_OUT = 0x04`.

Update `tud_hid_descriptor_report_cb` to dispatch trackpad instances to the right report descriptor.

Update `tud_descriptor_configuration_cb` with the 3-way select. Bump `CFG_TUD_HID` in `tusb_config.h` to accommodate the additional HID interfaces (currently 3, will need 5 in trackpad mode).

### Step 2.4 — Always-on activation

Move `send_magic_trackpad_activation` out from under `#ifdef DH_TRACKPAD_PHASE1` in `src/usb.c`. Activation is needed whenever a trackpad is plugged in — Phase 2's whole premise is the trackpad sending multi-touch frames, which it only does in activated state.

Same for `tuh_hid_set_report_complete_cb` (currently Phase-1-gated).

### Step 2.5 — UART transport: fragmented raw HID reports

New packet types in `src/include/protocol.h`:
```c
TRACKPAD_REPORT_IN_MSG  = 23,  // Source -> Device, fragmented Apple HID reports
TRACKPAD_REPORT_OUT_MSG = 24,  // Device -> Source, fragmented Apple HID OUT/Feature reports
```

Each packet's 8-byte data field carries:
- byte 0: report ID (`0x3F`, `0x53`, etc. — pass through whatever the trackpad/host sent)
- byte 1: total report length (1-255)
- byte 2: fragment offset within report (0, 6, 12, ...)
- bytes 3-7: 5 bytes of payload

Reassembly logic in `src/protocol.c`:
- Maintain a small per-direction buffer (~64 bytes max — covers the largest expected report).
- On first fragment (offset 0): record report ID and total length, start collecting.
- On subsequent fragments: copy into buffer at `offset`.
- When `offset + 5 >= total_length`: report complete, dispatch.

Compute: 63-byte report → ceil(63/5) = 13 packets × 12 bytes UART each = 156 bytes per report. At 90 Hz peak = 14 KB/s. UART runs at 1 Mbps = 125 KB/s. **~11% utilization at peak — fine.**

Add `validate_packet` cases for the new types.

### Step 2.6 — Source-side: forward IN reports

In `tuh_hid_report_received_cb` (`src/usb.c`), when the iface is the Magic Trackpad's mouse-class instance (Instance 1) or multi-touch-class instance (Instance 2):
- If this board is the active output: queue directly to local device-side via the dedicated trackpad HID interface (`tud_hid_n_report(ITF_NUM_HID_TRACKPAD_MOUSE, report_id, ...)` or `ITF_NUM_HID_TRACKPAD_MT`).
- Otherwise: fragment and queue UART packets of type `TRACKPAD_REPORT_IN_MSG`.

Routing logic mirrors `output_mouse_report` (`src/mouse.c:125`).

### Step 2.7 — Device-side: deliver IN reports

When `TRACKPAD_REPORT_IN_MSG` reassembles into a full report, call `tud_hid_n_report` on the appropriate trackpad interface. May need a queue if the device stack isn't ready (`tud_hid_n_ready` returns false) — pattern matches `process_mouse_queue_task`.

### Step 2.8 — Device-side: capture OUT reports

In `tud_hid_set_report_cb` (`src/usb.c`), add cases for `instance == ITF_NUM_HID_TRACKPAD_MT && report_id == 0x53 && report_type == HID_REPORT_TYPE_OUTPUT`. Fragment and queue `TRACKPAD_REPORT_OUT_MSG`.

For `tud_hid_get_report_cb` (Feature requests from macOS): if Step 2.1 confirms macOS does this, add a request/response loop. Probably uncommon enough that synchronous round-trip latency is acceptable.

### Step 2.9 — Source-side: deliver OUT reports

Reassemble `TRACKPAD_REPORT_OUT_MSG`, then `tuh_hid_set_report(trackpad_dev_addr, trackpad_instance, report_id, HID_REPORT_TYPE_OUTPUT, payload, len)`.

### Step 2.10 — Mode lifecycle

New UART message `TRACKPAD_MODE_MSG` carries one byte: 1 = enter trackpad mode, 0 = exit.

Source-side board:
- On Magic Trackpad mount → send `TRACKPAD_MODE_MSG(1)` to device-side board.
- On Magic Trackpad unmount → send `TRACKPAD_MODE_MSG(0)`.

Device-side board, on receipt:
1. Update `global_state.trackpad_mode_active`.
2. Call `tud_disconnect()`.
3. Wait ~100 ms (per TinyUSB recommendations for clean re-enumeration).
4. Call `tud_connect()`.
5. Host PC re-enumerates.

This must happen on both boards, but the *re-enumeration* affects only the device-side board (the one connected to the host PC). The source-side board's state change is informational only (so it knows whether to forward reports to local-device-stack vs UART).

Config mode coexistence: when the user enters config mode while trackpad mode is active, config mode wins. The config-mode-entry path already does a tud_disconnect/connect cycle; just ensure it picks up the config descriptor (priority order in 2.2 handles this). On config-mode exit, if trackpad still mounted, swap back to trackpad descriptor.

### Step 2.11 — Cursor switching policy

In `do_screen_switch` (`src/mouse.c:241`), early-return if `state->trackpad_mode_active`:

```c
if (state->trackpad_mode_active) return;
```

Cursor-edge switching is meaningless under raw passthrough — deskhop no longer sees absolute X/Y coordinates because reports go straight through. Switching falls back to the keyboard shortcut. README needs a note.

### Step 2.12 — Phase 1 removal

Once Steps 2.1-2.11 are validated end-to-end:
1. Delete `src/magic_trackpad.c`, `src/include/magic_trackpad.h`.
2. Delete `DH_TRACKPAD_PHASE1` flag from `CMakeLists.txt`.
3. Delete the `#ifdef DH_TRACKPAD_PHASE1` blocks from `src/usb.c`.
4. Update `docs/IMPLEMENTATION.md` Phase 1 section to mark "removed in commit X".
5. Single commit, ~300 lines deleted.

### Step 2.13 — Validation

| Test | Expected |
|---|---|
| macOS: System Settings → Trackpad pane | Visible, all options present, model identified as Magic Trackpad 2 |
| macOS: Tap-to-click toggle | Works when enabled; physical click works regardless |
| macOS: Two-finger scroll | Native macOS scroll behavior, indistinguishable from direct connect |
| macOS: Three-finger swipe between Spaces | Works |
| macOS: Four-finger Mission Control swipe-up | Works |
| macOS: Force Touch (if hardware supports) | Works to whatever degree macOS exposes it |
| Switch outputs via keyboard shortcut | Trackpad continues to work on new output (re-enumerates on the new one if needed) |
| Mouse-drag cursor switching | Disabled while trackpad is connected (documented limitation) |
| Regular USB mouse plugged alongside trackpad | Mouse works on both outputs; mouse-drag switching still works for the regular mouse |
| Linux output: hid-magicmouse | Binds; gestures work natively |
| Windows output | Basic mouse functionality only; advanced gestures not expected (documented) |
| Plug trackpad → unplug trackpad | Device re-enumerates back to default deskhop; keyboard/mouse still work |
| Enter config mode with trackpad attached | Config UI works; on exit, trackpad re-binds |
| Long stress: 30 minutes of mixed keyboard + trackpad use | No watchdog reboots, no UART desync, no crashes |

### Phase 2 file delta

- New: `desc_device_trackpad`, `desc_configuration_trackpad`, `desc_hid_report_trackpad_mouse`, `desc_hid_report_trackpad_mt` in `src/usb_descriptors.c`.
- New UART packet types and reassembly logic in `src/include/protocol.h`, `src/protocol.c`.
- Modified: `src/usb.c` (descriptor selection, raw report routing, `set_report` capture, removed Phase 1 gating from activation), `src/include/structs.h` (`trackpad_mode_active` flag), `src/mouse.c` (`do_screen_switch` early-return), `src/include/usb_descriptors.h` (new ITF / EPNUM constants), `src/include/tusb_config.h` (`CFG_TUD_HID = 5`).
- Source captures `docs/captures/*.txt` are reference-only — Phase 2 reads bytes from the trackpad live, no compile-time descriptor coding required.

### Effort estimate

- Step 2.1 (capture): half a day, no code.
- Steps 2.2-2.4 (descriptors and activation un-gating): half a day, ~150 lines.
- Steps 2.5-2.9 (UART transport + bidirectional forwarding): 2 days, ~400 lines (the fragmentation logic is the bulk).
- Step 2.10 (mode lifecycle + re-enumeration): half a day, ~80 lines.
- Step 2.11 (cursor switching): trivial, 5 lines.
- Step 2.12 (Phase 1 removal): trivial, single delete commit.
- Step 2.13 (validation across OSes): half a day with hardware.

**Total: ~4-5 days of focused work.** Two commit-worthy milestones: "trackpad descriptor + IN report passthrough working" and "OUT/feature reports + lifecycle complete".

---

## Path A — port libinput's tap state machine

> **Status: landed (opt-in).** All 30 libinput tap states ported. Tap-to-click (1F), 2F right-click tap, 3F middle-click tap, tap-and-drag, double-tap all working. Drag-lock implemented but defaults off (matches libinput). Palm/thumb rejection and tap-disabled-while-typing intentionally deferred — both are most relevant to laptop pads, less applicable to a separate desk trackpad. Gated behind `DH_TRACKPAD_TAP_TO_CLICK` (OFF by default); default builds are byte-for-byte unchanged.

Faithfully translate `src/evdev-mt-touchpad-tap.c` from [libinput](https://gitlab.freedesktop.org/libinput/libinput) into the firmware. The state machine is ~1700 lines, 30+ states, MIT licensed. Done well, this gives us tap-to-click, tap-and-drag, drag-lock, double-tap, multi-finger tap recognition, and the dozens of edge cases libinput has accumulated over a decade — without us inventing any of them.

### Hard-won learning: Magic Trackpad 2 USB byte layout

The contact-down condition for **Magic Trackpad 2 USB** (PID `0x0265`) is in **byte 3 mask `0xC0`, with `down = (state == 0x80)`** — *not* byte 8 mask `0xF0` with the `0x30/0x40` lifecycle that older Magic Mouse / Trackpad 1 devices use. Byte 8 holds `id (low 4 bits) | orientation (top 3 bits)`, which is easy to mistake for state because the high nibble fluctuates as orientation changes during a contact.

The authoritative reference is `drivers/hid/hid-magicmouse.c` in the Linux kernel, specifically the `magicmouse_emit_touch` function's `USB_DEVICE_ID_APPLE_MAGICTRACKPAD2` branch:

```c
state = tdata[3] & 0xC0;
down  = state == 0x80;
```

If you're touching this code: **read that function directly**, don't rely on inferences from the older devices' branches. We burned hours guessing against the wrong byte before checking the source.

### Source files to translate

| libinput source | Our target | Purpose |
|---|---|---|
| `src/evdev-mt-touchpad-tap.c` | `src/magic_trackpad_tap.c` | Core state machine and event handlers |
| `src/evdev-mt-touchpad.h` (tap-related fields) | `src/include/magic_trackpad_tap.h` | State enum, event enum, per-touch tap state |

### Translation rules

1. **State names verbatim.** `TAP_STATE_IDLE`, `TAP_STATE_TOUCH`, `TAP_STATE_1FGTAP_DRAGGING`, etc. Don't rename, don't collapse. Anyone debugging will be reading libinput's source side-by-side.
2. **Event names verbatim.** `TAP_EVENT_TOUCH`, `TAP_EVENT_MOTION`, `TAP_EVENT_RELEASE`, `TAP_EVENT_BUTTON`, `TAP_EVENT_TIMEOUT`, `TAP_EVENT_THUMB`, `TAP_EVENT_PALM`, `TAP_EVENT_PALM_UP`. Same.
3. **Handler functions verbatim.** One handler per state: `tp_tap_idle_handle_event`, `tp_tap_touch_handle_event`, etc. Don't merge, don't simplify. Internal state transitions follow libinput exactly.
4. **Replace `struct tp_dispatch *tp` with our `mt_gesture_state_t *s`.** Add the libinput-tap-related fields to our state struct (or a sub-struct).
5. **Replace `struct tp_touch *t` with `mt_finger_t *finger` plus per-touch tap state.** The tap state machine tracks per-touch flags like `is_thumb`, `is_palm`, `state`. Add a small parallel structure indexed by finger ID or slot.
6. **Replace `tp_tap_set_timer(tp, time + DEFAULT_TAP_TIMEOUT_PERIOD)` with `s->tap.timer_us = now_us + DEFAULT_TAP_TIMEOUT_PERIOD_US`.** Polling-based. The caller of the gesture step checks `now_us >= s->tap.timer_us` and synthesizes a `TAP_EVENT_TIMEOUT` when applicable.
7. **Replace `tp_tap_clear_timer(tp)` with `s->tap.timer_us = 0`** (or `UINT32_MAX`, sentinel).
8. **Replace `tp_tap_post_button(tp, button, time, pressed)` with our queue path.** Buttons go through `output_mouse_report` with the button bit set.
9. **No new logic.** If a corner case feels wrong, look at libinput's source first. Bring our port closer, not further.

### Step A.1 — Skeleton

- New header `src/include/magic_trackpad_tap.h` with `enum tp_tap_state`, `enum tap_event`, the `tp_tap` sub-struct (members: `state`, `timer_us`, `tap_count`, `enabled`, etc., translated from libinput's), and a per-touch tap state struct `tp_touch_tap_state` (members: `state`, `is_thumb`, `is_palm`, etc.).
- New stub `src/magic_trackpad_tap.c` with the dispatcher `tp_tap_handle_event` and stub bodies for each state's `tp_tap_*_handle_event`.
- Compiles clean with `DH_TRACKPAD_TAP_TO_CLICK=ON`. State machine compiles but does nothing useful.

### Step A.2 — IDLE / TOUCH / HOLD

Translate the basic touch flow. After this, single-finger tap → left click works.

### Step A.3 — TAPPED / DRAGGING_OR_DOUBLETAP / DRAGGING

Tap-and-drag: a tap immediately followed by motion holds the button down during the motion. Translates the entire double-tap-vs-drag-disambiguation tree.

### Step A.4 — Drag lock

`DRAGGING_WAIT` and `DRAGGING_OR_TAP`. Brief lift during a drag preserves the held button if a new touch arrives within the lock window.

### Step A.5 — Multi-finger tap (TOUCH_2 / TOUCH_3 family)

Two-finger tap → right click. Three-finger tap → middle click (configurable later).

### Step A.6 — DEAD state and palm/thumb handling

Most of the cleanup logic. Required for proper palm rejection.

### Step A.7 — Wire to gesture step

In `mt_gesture_step`, drive the state machine each frame:
- New touch (finger appears in current frame, not in previous): `tp_tap_handle_event(TAP_EVENT_TOUCH)`
- Motion past threshold: `tp_tap_handle_event(TAP_EVENT_MOTION)`
- Touch lift (finger gone in current frame, was in previous): `tp_tap_handle_event(TAP_EVENT_RELEASE)`
- Physical click rising edge: `tp_tap_handle_event(TAP_EVENT_BUTTON)` with pressed=true
- Physical click falling edge: `TAP_EVENT_BUTTON` with pressed=false
- `now_us >= s->tap.timer_us`: `TAP_EVENT_TIMEOUT`

### Step A.8 — Validation

Test each behavior libinput documents:
- Single tap → left click
- Two-finger tap → right click
- Three-finger tap → middle click
- Tap-then-drag (no lift between) → button held during motion
- Double-tap → two click events, OS recognizes as double
- Drag with lift-and-return within lock window → continues drag
- Hold (touch + no motion + > tap timeout) → no tap, palm/intentional rest
- Movement during touch → no tap

Each should match libinput's behavior on a reference Linux machine.

### Effort estimate

Step A.1: ~1 hour (skeleton, all stubs, builds).
Step A.2: ~1 hour (basic tap working).
Step A.3: ~1.5 hours (drag complexity).
Step A.4-A.6: ~2 hours combined.
Step A.7-A.8: ~1 hour.

**Total: 6-8 hours**, broken across multiple sessions. Each step is committable progress.

### Flash budget concern

Default Phase 1 build: 188 KB / 188 KB (100% of partition). Path A code estimated 6-12 KB compiled. Default builds (with `DH_TRACKPAD_TAP_TO_CLICK=OFF`) pay zero. Builds with the flag on may exceed the partition; mitigations if so:
- Strip unused libinput-port states (e.g., 3-finger family if not needed)
- Increase `FLASH` partition in `misc/memory_map.ld` (shrinks `FW_STAGING`; loses dual-bank firmware-update guarantee)
- Compile-time strip palm/thumb handling (acceptable on Apple trackpad which doesn't report palm/thumb anyway)

---

## Path G — port libinput's gesture state machine (scoped subset)

> **Status: slice 1 landed (opt-in).** Skeleton + first-pass implementations of NONE / UNKNOWN / POINTER_MOTION / SCROLL_START / SCROLL / SWIPE_START / SWIPE. Replaces Phase 1's hand-rolled scroll/swipe paths in `mt_gesture_step` when `DH_TRACKPAD_GESTURES=ON`. Default builds untouched. Pinch / hold / 3FG-drag / edge-scroll states intentionally deferred.

Faithful (but scope-trimmed) port of `src/evdev-mt-touchpad-gestures.c`. The 7 states above are the ones reachable for Magic Trackpad's user-facing scroll + swipe behavior; the omitted states are present in the enum (state numbering preserved) but unreachable in our port.

### Source files to translate

| libinput source | Our target | Purpose |
|---|---|---|
| `src/evdev-mt-touchpad-gestures.c` | `src/magic_trackpad_gestures.c` | Gesture state machine, scoped subset |
| `src/evdev-mt-touchpad.h` (gesture-related fields) | `src/include/magic_trackpad_gestures.h` | State enum, event enum, gesture state struct |

### Translation rules

Same as Path A (state names verbatim, event names verbatim, no new logic), with two scope-trimming notes:
1. **State enum numbering preserved.** All 17 libinput states are listed in our enum so debugging side-by-side against libinput's source isn't off-by-N. Skipped states are marked `/* skipped */` and unreachable.
2. **`tp_dispatch.gesture` -> `tp_gesture_t`.** Carry only the fields the in-scope states actually touch (initial position, prev position, scroll/pointer remainders, swipe accumulator). Each new field is annotated with the libinput equivalent it replaces.

### Slice 1 (this PR) — basic gesture flow

- NONE -> UNKNOWN on FINGER_DETECTED
- UNKNOWN -> POINTER_MOTION / SCROLL_START / SWIPE_START on direction-threshold breach
- POINTER_MOTION emits cursor delta with sub-pixel remainder accounting
- SCROLL emits wheel/pan with remainder + natural-scroll convention
- SWIPE accumulates X distance and emits one direction per gesture (LEFT/RIGHT)

### Slice 2 (next) — refinement

- Faithful libinput motion-smoothing on pointer (probably not needed for Magic Trackpad's higher native rate)
- SCROLL direction-lock (libinput locks vertical-vs-horizontal once threshold is crossed; we currently emit both axes simultaneously)
- FINGER_SWITCH_TIMEOUT to handle finger swaps mid-gesture
- Verify SWIPE_TIMEOUT semantics (currently we accumulate without a timeout)

### Out of scope (deferred indefinitely)

- HOLD / HOLD_AND_MOTION: mostly relevant to laptop pads where you might rest fingers. Magic Trackpad on a desk doesn't really need it.
- PINCH: standard mouse HID can't represent pinch. Translation to `Ctrl+Wheel` (zoom) is OS-conditional and ugly.
- 3FG_DRAG_*: 3-finger drag is currently bound to workspace swipe in Phase 1; 3FG-drag would need its own button and conflicts.
- Edge-scroll (single-finger edge zone): Magic Trackpad has no buttons or detents; edge scrolling on a smooth surface is jarring.

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
