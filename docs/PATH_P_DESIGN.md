# Path P — Design

## Problem

deskhop's device side enumerates as a generic HID **mouse**. When a Magic Trackpad is plugged into the host port, deskhop translates trackpad gestures into mouse events (relative dx/dy + wheel + buttons + AC pan). This works for cursor and scroll, but it permanently throws away the gesture-ness of the input. The host PC sees a mouse, not a trackpad, so:

- GNOME (mutter) doesn't expose its native swipe/pinch/zoom gestures because libinput hasn't classified the device as a touchpad
- macOS shows it as a regular mouse in System Settings — no Trackpad pref pane
- Windows treats it as a generic mouse — no Precision Touchpad gestures
- Configurable in user preferences as a mouse, not a trackpad

The user-visible gap: 3-finger swipe is implemented as a deskhop-side keyboard-shortcut translation (`Ctrl+Alt+Arrow`). Pinch isn't possible because there's no `Ctrl+Wheel` equivalent that's universally correct. macOS-native Mission Control / App Exposé / Force Touch are entirely unreachable.

## Root cause

OSes classify HID devices as mouse vs touchpad by **capability**, not name:

- libinput / udev look for **absolute** X/Y multi-touch axes (`EV_ABS` + `MT_*`) plus the Digitizer / TouchPad usage codes in the HID descriptor.
- Microsoft's Precision Touchpad (PTP) spec defines a HID report layout with: contact ID, absolute X/Y, tip switch, confidence, contact count, button state, scan time, and a feature report carrying device capabilities (size in mm, max contacts, button type).
- Apple's macOS recognizes its own multi-touch protocol (the Apple proprietary one we already decode) when the device advertises Apple's vendor-defined HID interface.

deskhop emits **relative dx/dy mouse data**. No absolute axes, no contact reports, no PTP capability descriptor. So no host classifies it as a trackpad regardless of how cleverly we translate the gestures internally.

## Constraints

- Whatever Path P emits has to **not break deskhop's screen-edge cursor switching**. Edge switching depends on knowing the cursor's coordinates so we can detect it crossing into a screen edge that maps to another output. With relative mouse data, deskhop tracks cursor position by integrating deltas. With absolute touchpad contacts, the host owns the cursor and deskhop has no knowledge of where it is.
- Whatever Path P emits has to **not break the existing keyboard / mouse / configuration interfaces**. deskhop today emits a composite device with: Boot Keyboard, Mouse, Vendor (config), Disk (filesystem), and optionally Debug. Adding a touchpad interface or replacing the mouse interface has to play nicely with all of these.
- Anyone using deskhop without a trackpad must see no behavioral change. The default user case is keyboard + mouse.
- Phase-2-style tricks (spoofing Apple VID/PID, sending raw Apple multi-touch frames over UART) are out of bounds. We tried; `hid-magicmouse` claims all interfaces of any Apple-VID device and breaks the keyboard. The whole design here assumes a **non-Apple VID** with a **standard PTP descriptor**.

## Goals

- **Native trackpad classification** on Linux (GNOME / mutter), macOS, Windows — at least Linux primary.
- **Native gestures** the OS handles in its own way. We stop translating 3F swipe to keyboard shortcuts in firmware; GNOME / macOS handle their own swipe-up = overview, etc.
- **Pinch and rotate** become possible (currently impossible with mouse-only HID).
- **Edge switching survives** in some form — either keeps working for a non-trackpad pointer, or has a documented degraded behavior for trackpad input with a clear escape hatch (keyboard shortcut, which already works).

## Non-goals

- Identifying as Apple. We tried; it broke the keyboard. Vendor stays generic (1209 / pid.codes community VID) or whatever non-Apple VID we pick.
- Replicating macOS-only gestures (Force Touch, Mission Control swipe-up) bit-perfectly. If the OS exposes them via PTP, great; if not, we don't synthesize them.
- Custom user-configurable gesture remapping. That's libinput / mutter / macOS settings territory. We only need to give the OS the data.

## Approach options

Four shapes of Path P. Each has tradeoffs. Pick one before writing code.

### Option A — Composite device: mouse + touchpad interfaces

deskhop's device-side descriptor adds a third HID interface alongside the existing keyboard and mouse: a Precision Touchpad. When a Magic Trackpad is plugged in, contact data is forwarded to the touchpad interface; the existing mouse interface still receives translated cursor deltas (because edge switching needs them). When a regular mouse is plugged in, only the mouse interface emits.

**Pros:**
- Both work simultaneously. Native gestures via touchpad interface; edge switching via mouse interface.
- Existing mouse infrastructure unchanged; touchpad interface is purely additive.
- Standard composite HID — no platform-specific tricks.

**Cons:**
- Host sees **two pointer devices** when a trackpad is plugged in. Some OSes may show two cursors briefly; GNOME tends to merge them but Windows doesn't.
- Settings split: user has to configure trackpad sensitivity in trackpad settings AND mouse sensitivity in mouse settings.
- Trackpad data is sent twice (once as raw contacts, once as translated deltas) — bandwidth and timing concerns.
- libinput sees both interfaces and may assign them to the same seat with confusing pointer arbitration.

### Option B — Mode switch: trackpad plugged in → touchpad, no trackpad → mouse

deskhop's device-side descriptor is dynamically chosen at boot or at runtime based on what's attached. When a Magic Trackpad is present, deskhop re-enumerates as a touchpad-only device. When the trackpad is removed, it re-enumerates as a mouse-only device.

**Pros:**
- Cleanest UX per mode. One pointer device. Settings live where the user expects.
- Native gestures work naturally when trackpad is connected.
- Mouse-only path is byte-for-byte unchanged for existing users.

**Cons:**
- USB **re-enumeration mid-use is jarring** to most OSes. The host loses the device for a beat, then re-adds it. Cursor may freeze briefly. Some OSes log warnings.
- Edge switching breaks while trackpad is plugged in. User must use the keyboard shortcut to switch outputs. Documented limitation.
- Re-enumeration means OS-saved per-device settings (acceleration, scrolling speed) are reset each time the trackpad is plugged or unplugged. Annoying.

### Option C — Touchpad-only build flag

A new build flag, e.g. `DH_DEVICE_AS_TOUCHPAD=ON`, makes deskhop emit as a touchpad permanently — even when the user has a regular mouse plugged in. The user takes the tradeoff explicitly at build time.

**Pros:**
- No runtime mode switching, no composite-device weirdness.
- Build flag is transparent — user knows what they're getting.

**Cons:**
- Regular mouse input gets squeezed through the touchpad interface as well, which is wrong for actual mice (no MT contacts).
- Edge switching is broken whether or not a trackpad is plugged in.
- Forces a binary user-time choice instead of letting the device be smart.

### Option D — Single interface, dual report IDs

Same HID interface advertises both mouse-style reports (Report ID 1: relative dx/dy) and touchpad-style reports (Report ID 2: PTP contacts). deskhop sends mouse reports for cursor motion (so edge switching keeps working) and PTP reports for gestures.

**Pros:**
- One interface, one cursor device on the host.
- Edge switching keeps working via mouse-style reports.
- Native gestures via PTP reports.

**Cons:**
- libinput's device classification is per-interface. An interface that advertises both mouse and touchpad capabilities is an ambiguous device — libinput will probably pick one classification (likely mouse, since relative axes are present) and ignore the touchpad reports as undefined.
- Microsoft's PTP certification requires a specific descriptor shape; mixing in mouse usage codes likely fails.
- Realistically not how the spec works. We'd be trying to be clever in a place where the spec is rigid.

## Why this order

Option B (mode switch) is the cleanest pure design but has the worst real-world UX (re-enumeration jarring).

Option A (composite) is the right answer if we can tolerate the dual-pointer weirdness — which on Linux/GNOME is mostly fine because libinput merges devices on the same seat reasonably well.

Option C is the escape hatch — easy to implement but pushes the tradeoff onto the user.

Option D would be elegant if it worked, but specs don't really allow it.

**Initial recommendation:** Option A (composite). Investigate whether GNOME / macOS / Windows actually show two cursors or merge them sensibly before committing. If two cursors is unavoidable, fall back to Option B (mode switch) and accept the re-enumeration jitter.

## Risks and open questions

- **Dual-cursor behavior across OSes.** Need to actually test composite-device behavior on GNOME, macOS, Windows. The literature is mixed.
- **PTP feature reports.** Windows is strict about feature report 0x85 (capabilities). macOS doesn't care. Linux doesn't care. We need to get the Windows-required descriptor blob right or PTP won't activate on Windows.
- **Coordinate scaling.** Magic Trackpad's native coordinate space isn't the same as PTP's expected range. We need a per-device calibration (probably hardcoded for Magic Trackpad 2 USB).
- **Scan time.** PTP requires a scan-time field. If we get this wrong, the host's gesture detection will misjudge velocity and direction.
- **Pico-PIO-USB host-side polling rate.** Magic Trackpad emits at 90-250 Hz native. PTP expects roughly continuous reporting. Need to confirm we can keep up.
- **Re-enumeration on trackpad hotplug** (if Option B). Test what happens to the mouse interface when we switch modes mid-session.
- **TinyUSB device-side composite limits.** Confirm we can add a third HID interface alongside keyboard and mouse without exceeding TinyUSB's endpoint limits or RP2040 USB controller resources.
- **Edge switching with no trackpad.** If a regular mouse is plugged in alongside the trackpad (Option A), the regular mouse drives edge switching as today. If only a trackpad is plugged in (Option B), edge switching is keyboard-shortcut-only. Documented behavior; not invisible.

## Approach: phased rollout

### Step P.1 — PTP descriptor research and prototype (no code)

Read Microsoft's PTP spec end-to-end. Study `hid-multitouch.c` in the Linux kernel for reference parsing. Pick a known-good open-source PTP implementation (e.g., QMK's PMW3360 with PTP) as a reference descriptor. Decide on:
- Number of contacts to advertise (Magic Trackpad supports 11; libinput typically caps at 5)
- Pad dimensions in 0.01 mm
- Button type (clickpad / pressure-pad bit)

Output: a PTP descriptor byte blob committed to a header, gated behind `DH_PATH_P`. No runtime use yet.

### Step P.2 — Composite device — add touchpad interface alongside mouse

Wire a new HID interface in `usb_descriptors.c` with the PTP descriptor. Behind `DH_PATH_P=ON`, deskhop enumerates as a composite mouse + keyboard + touchpad + vendor + disk device. Touchpad interface emits no data yet — just exists.

Validate: macOS / Linux / Windows all enumerate the device without errors. Existing keyboard, mouse, vendor, disk interfaces unaffected. lsusb shows the new interface.

### Step P.3 — Forward Magic Trackpad contacts to PTP reports

Translate decoded `mt_frame_t` data (already in our codebase via `mt_decode_report`) into PTP contact reports. Send via the new touchpad interface alongside the existing mouse-translated path.

Validate: GNOME shows the device in Settings → Mouse & Touchpad with trackpad-specific options. libinput's `libinput debug-events` shows `GESTURE_SWIPE_*` events firing when 3 fingers slide. Native pinch-to-zoom works in Firefox on Linux.

### Step P.4 — Decide on the dual-cursor issue

Test how each OS handles the simultaneous mouse + touchpad input. Two possibilities:
- **It's fine.** Both pointers merge to one cursor; settings are split but tolerable. Ship Option A.
- **It's bad.** OSes show two cursors or fight over them. Implement Option B (mode switch via re-enumeration) as a fallback. Document the edge-switching tradeoff.

### Step P.5 — Stop translating gestures in firmware

Once native gestures are working via PTP, the deskhop-side 3F-swipe-to-Ctrl+Alt+Arrow translation becomes redundant on PTP-enabled outputs. Make Path G's swipe emission conditional on `!DH_PATH_P` or on a runtime config field. Pinch becomes a no-op in firmware (the host handles it).

### Step P.6 — Documentation and migration

Update README, DESIGN.md, IMPLEMENTATION.md. Document:
- Build flag `DH_PATH_P`
- The dual-cursor / mode-switch behavior chosen in P.4
- The screen-edge tradeoff (works with regular mouse; degraded with trackpad)
- How to revert to the old mouse-translation path if PTP misbehaves

## Build flag landscape

| Flag | Default | What |
|---|---|---|
| `DH_PATH_P` | `OFF` | Add a Microsoft Precision Touchpad HID interface. When ON, the trackpad's contacts are forwarded as PTP reports and the host treats deskhop as a real touchpad. Pairs with `DH_MAGIC_TRACKPAD=ON` (provides the multi-touch decoder). |

`DH_MAGIC_TRACKPAD=OFF` + `DH_PATH_P=ON` is invalid (no decoder to drive PTP reports). CMake should error.

## Out of scope (deliberately)

- Microsoft Precision Touchpad **certification**. The PTP descriptor shape is enough for OSes to bind; we don't need a logoed certificate.
- **Per-output PTP**. The touchpad classification applies to the device as a whole, not per output. If you want a touchpad on output A and a mouse on output B, that's not what this is.
- **Apple-native protocols** (raw multi-touch frames, Force Touch). See Phase 2 abandonment.
- **Hand-rolled gesture detection on the host side**. We're letting the OS do this work.
