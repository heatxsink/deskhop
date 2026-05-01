# Magic Trackpad Support — Design

Tracks GitHub issue [#207](https://github.com/hrvach/deskhop/issues/207).

## Problem

A USB Magic Trackpad behind deskhop loses two-finger scrolling, swipe gestures, and tap-to-click on macOS. Cursor movement and physical clicks still work, but the device behaves like a generic mouse.

## Root cause

Deskhop is an HID *interpreter*, not a USB *passthrough*. On the host side it parses the source device's report descriptor with its own limited parser (`src/hid_parser.c`), extracts X/Y/buttons/wheel/pan, and re-synthesizes a generic absolute mouse on the device side (`desc_hid_report` in `src/usb_descriptors.c:41`). Vendor-defined reports are dropped on the floor — the parser only matches `HID_USAGE_PAGE_BUTTON`, `HID_USAGE_PAGE_DESKTOP` (X/Y/wheel), and `HID_USAGE_PAGE_CONSUMER` (AC Pan).

Apple Magic Trackpad 2 (VID `0x05AC`, PID `0x0265`) sends multi-touch frames on Report ID `0x02` in an Apple-proprietary layout. The descriptor advertises a standard mouse, but the actual report bytes encode multi-finger touch records, not X/Y deltas. Linux handles this with `drivers/hid/hid-magicmouse.c`, which intercepts reports for the Apple VID/PID and reinterprets them. macOS uses a private kext that binds only when it sees the Apple VID/PID and the proprietary FF00 vendor interface.

Deskhop's synthesized generic-mouse interface defeats both bind paths:
- macOS sees a generic absolute mouse (deskhop's synthetic descriptor) and never offers the trackpad pref pane.
- The proprietary multi-touch payload is misread as X/Y deltas, producing the noisy cursor movement that currently passes for "the trackpad working."

## Constraints

- Two RP2040s connected by 1 Mbps full-duplex UART.
- HID parser is descriptor-driven; no path for vendor-defined reports.
- Cursor switching ("drag to other screen") relies on absolute X/Y position tracking on the source-side board — anything that breaks position tracking breaks that feature.
- Device-side TinyUSB config has 2 HID interfaces in normal mode (`ITF_NUM_HID`, `ITF_NUM_HID_REL_M`) and adds vendor + MSC in config mode.
- Pico-PIO-USB enumerates the trackpad on the host side; we have not yet confirmed it sees all four interfaces the trackpad exposes. Phase 1 will validate.

## Goals

- **Phase 1:** Two-finger scroll on a Magic Trackpad works through deskhop on all three OS outputs (macOS, Linux, Windows).
- **Phase 2:** macOS recognizes the trackpad as a real Apple device — tap-to-click, the System Settings trackpad pane, two-finger scroll, three-finger swipe between desktops, and Mission Control gestures all work.

## Non-goals

- Force Touch / haptic feedback passthrough.
- Gesture decoding for non-Apple touchpads (Logitech, Synaptics, etc.).
- Full passthrough KVM mode for arbitrary HID devices (issue #207 option 2 — separate effort with broader scope).

## Status

**Phase 1 ships.** The `DH_TRACKPAD_PHASE1` build flag is ON by default. Default builds give Magic Trackpad users:

- Cursor + physical click + two-finger right-click + click-drag with second finger
- Two-finger vertical and horizontal scroll
- Three-finger horizontal swipe → workspace switch (Ctrl+Alt+Left/Right, GNOME default)
- Truncation-remainder accounting on slow finger movements (smooth window-resize feel)

A watchdog reboot bug surfaced during Phase 1 bringup when `output_mouse_report` was called at the trackpad's native rate (~100-250 Hz). Bisection identified the call site but not the underlying cause — same call works fine for regular mice at similar rates. Pragmatic fix: the Phase 1 emit path throttles to 100 Hz with motion accumulation in between. Cursor feel is unaffected (within typical perception threshold). Root cause investigation is deferred and tracked separately.

**Phase 2 attempted, abandoned.** We tried spoofing the Apple device descriptor so Linux's `hid-magicmouse` would bind and handle gestures natively. It worked at the binding layer but broke the keyboard: `hid-magicmouse` claims **all** HID interfaces of any Apple-VID device by design, with no per-interface filter. We tried udev rebind workarounds; Linux's `hid-generic` explicitly refuses to bind when a more-specific driver matches, so no workaround was viable. A USB hub (firmware or hardware) would solve it but is significant new work. Phase 2 source, scripts, and udev rules were reverted; design notes are kept here as a record.

**Path A working, opt-in.** All 30 libinput tap states ported. Tap-to-click, 2-finger right-click tap, 3-finger middle-click tap, and tap-and-drag all work. Drag-lock is implemented but defaults off (matches libinput). Palm/thumb rejection and tap-disabled-while-typing intentionally deferred — both are most relevant to laptop pads where palms rest on the surface; less applicable to a separate desk trackpad. Path A is gated behind `DH_TRACKPAD_TAP_TO_CLICK` (OFF by default) so default builds remain unchanged.

The breakthrough during port: the touch state byte for Magic Trackpad 2 USB lives in **byte 3 (mask 0xC0, down = `state == 0x80`)**, not byte 8 as for older Magic Mouse / Trackpad 1. Initial guesses against an assumed byte 8 / mask 0xF0 / state values 0x30-0x40 lifecycle wasted hours. The kernel `hid-magicmouse.c` `magicmouse_emit_touch` function (`USB_DEVICE_ID_APPLE_MAGICTRACKPAD2` branch) is the authoritative reference for Magic Trackpad 2 USB byte layout — read it directly rather than relying on Magic Mouse / Trackpad 1 inferences.

## Approach

### Phase 1 — Translate

Detect VID `0x05AC` / PID `0x0265` at mount. Install a custom report handler for Report ID `0x02` that decodes the Apple multi-touch payload. Run a small finger-tracking state machine. When two fingers move together, emit standard `wheel`/`pan` events through the existing mouse pipeline. When one finger is down, emit pointer deltas. Physical clicks are already handled by the standard mouse interface and are left alone.

The device-side USB descriptor does not change. From the host PC's point of view, deskhop is still a generic mouse — but a generic mouse that scrolls correctly when the user uses two fingers on the trackpad. Cursor switching via screen-edge drag continues to work.

### Phase 2 — Spoof

When a Magic Trackpad is connected, switch the device-side descriptor to advertise:
- Apple VID `0x05AC` / PID `0x0265`.
- The trackpad's vendor-defined multi-touch HID interface (usage page `0xFF00`, usage `0x0D`, Report IDs `0x3F` IN and `0x53` OUT).

Forward the proprietary multi-touch reports verbatim from source-side host to device-side host PC over UART. macOS binds its trackpad driver; tap-to-click, gestures, and the pref pane all light up.

While the trackpad is the active pointer, screen-edge cursor switching is disabled (cursor coordinates are opaque to deskhop under raw passthrough). Switching falls back to the keyboard shortcut. This is the explicit tradeoff in issue #207 option 2.

### Path A — port libinput's tap state machine

Phase 1's gesture state machine handles 1-finger pointer, 2-finger scroll, and 3-finger swipe. Adding tap-to-click, tap-and-drag, drag-lock, and double-tap recognition by hand means designing each as a new state machine with its own corner cases. Linux's [`libinput`](https://gitlab.freedesktop.org/libinput/libinput) has already done this work in `src/evdev-mt-touchpad-tap.c` (~1700 lines, 30+ states, ten years of testing across every weird trackpad someone has plugged into a Linux box). MIT licensed, compatible with deskhop's GPL3.

Faithful port. Translate libinput's state machine state-for-state into the firmware. Same enum names, same transitions, same event vocabulary. We provide the same inputs the state machine expects (touch / motion / release / button / timeout events) and the same output mechanism (post_button hooks → our queue path in `usb.c`). We use `time_us_32()` polling instead of kernel timers; the timer abstraction is preserved as `tp_tap_set_timer(when_us)` → "fire `tp_tap_handle_timeout` if `now_us >= when_us`."

We do **not** add new logic we think makes sense. If a corner case feels wrong, we look at libinput's source first and bring our port closer to it, not further away.

The state machine produces standard mouse button events. Existing Phase 1 motion handling is unaffected; only the click/tap path changes. Cross-screen edge switching keeps working.

**Trade-offs:**
- Flash budget. Default builds are at 188 KB / 188 KB partition. Path A adds ~6-12 KB of compiled state machine code. So Path A is gated behind `DH_TRACKPAD_TAP_TO_CLICK` (OFF by default); default builds don't pay the cost.
- Effort. Real port is several hours. Tested behavior is the payoff.
- Naming consistency with libinput is a feature, not a bug. Future contributors can read libinput's code as the canonical reference.

## Why this order?

- Phase 1 is the smallest change that fixes the loudest pain point. It validates our decode of the Apple protocol and ships scrolling in days.
- Phase 2 reuses Phase 1's protocol knowledge and adds the descriptor/transport surgery on top. If Phase 2 had to come first, every protocol bug would compound with descriptor and UART bugs at the same time.
- Phase 1 is reversible per-device (gated by VID/PID); it does not affect non-Apple devices and does not change the device-side descriptor seen by any host PC.

## Risks and open questions

- **Pico-PIO-USB interface coverage.** The trackpad exposes four HID interfaces. We need to confirm host-side enumeration sees what we need. Phase 1 step 1 is descriptor logging precisely to answer this.
- **Apple multi-touch payload layout.** The Linux kernel driver is the reference. The USB Magic Trackpad 2 variant differs from BT and from Magic Mouse. Verify with a USB capture before committing to a layout.
- **UART bandwidth (Phase 2).** 15-byte multi-touch reports at 90 Hz = ~1.4 KB/s. Trivial against 1 Mbps. OUT reports (host → trackpad) are rare. No concern.
- **macOS feature-report probing (Phase 2).** macOS may issue feature reports during driver bind. If so, we need bidirectional report forwarding, not just IN. To be confirmed during Phase 2 step 1.
- **Windows / Linux outputs in Phase 2.** Apple VID/PID may surprise non-macOS hosts. Linux has `hid-magicmouse` and should be fine. Windows lacks first-class Apple drivers; expect basic mouse plus possibly nothing else. Document as known limitation, do not block Phase 2 on it.
- **Config mode coexistence.** `desc_configuration_config` must remain unchanged in Phase 2. When the user enters config mode, deskhop is a config UI, not a trackpad — descriptor selection priority is config > trackpad > default.
- **Hot-plug enumeration churn.** Phase 2 needs to disconnect/reconnect the device-side USB on trackpad mount/unmount. Validate that the host PCs handle the re-enumeration cleanly without spurious cursor jumps or disconnect dialogs.

## Validation

### Phase 1
- macOS: scroll a long page in Safari with two fingers — smooth, monotonic, no cursor jitter.
- Linux: scroll in a browser, same outcome.
- Windows: scroll in a browser, same outcome.
- Cursor still switches across screens via mouse drag.
- Plug a regular USB mouse alongside the trackpad — both work, no regression.

### Phase 2
- macOS System Settings shows Magic Trackpad with the full pref pane; tap-to-click is toggleable and works.
- Two-finger scroll, three-finger swipe between desktops, Mission Control four-finger swipe-up — all work on both outputs.
- Switching between outputs via keyboard shortcut: trackpad continues to work on the new output.
- A regular mouse plugged in alongside the trackpad still works; mouse-drag screen switching still works for the regular mouse.
- Linux output: trackpad enumerates, `hid-magicmouse` binds, gestures work.
- Windows output: documented as basic-mouse-only; verify scroll at minimum.

## Build flag landscape

| Flag | Default | What it does |
|---|---|---|
| `DH_TRACKPAD_PHASE1` | `ON` | Apple multi-touch decode + hand-rolled gesture state machine. Translates trackpad gestures to standard mouse / keyboard events. Disable with `OFF` to fall back to plain-mouse passthrough. |
| `DH_TRACKPAD_TAP_TO_CLICK` | `OFF` | Path A: libinput's tap state machine ported into firmware. Enables tap-to-click, tap-and-drag, drag-lock, etc. once the port lands. Mutually composable with Phase 1 — Phase 1 handles motion/scroll, Path A handles click semantics. Costs ~6-12 KB of flash. |
| `DH_DEBUG_TRACKPAD` | `OFF` | Verbose CDC logging of HID descriptors and Apple reports. Requires `DH_DEBUG=ON`. |
| `DH_DEBUG_TRACKPAD_LED` | `OFF` | Onboard LED toggles on every Apple-VID HID report received. No CDC required. Use for live presence diagnostics in production builds. |
| `DH_TRACKPAD_PHASE1_SKIP_EMIT` | `OFF` | Bisect aid. With Phase 1 on, run decode + gesture but skip mouse / keyboard emit. Used to isolate the unsolved high-rate `output_mouse_report` watchdog reboot. |
| `DH_TRACKPAD_PHASE1_NO_SCREEN_SWITCH` | `OFF` | Bisect aid. Emit mouse but skip `do_screen_switch` on edge crossings. |
| `DH_TRACKPAD_PHASE1_NO_OUTPUT_REPORT` | `OFF` | Bisect aid. Skip the actual `output_mouse_report` call but run everything else. |

Phase 2 (Apple VID/PID descriptor spoof) was attempted and reverted; no flag remains for it. See the Status section for the abandonment rationale.

## Out of scope (deliberately)

- Other Apple devices (Magic Mouse 2, Magic Keyboard with Touch ID).
- A general-purpose passthrough mode for arbitrary HID devices.
- Persisting trackpad-specific configuration in flash. The detection is VID/PID-based and stateless.
