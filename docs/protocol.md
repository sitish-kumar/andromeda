# Private protocol

One Wayland protocol, `desktop-unstable-v1`, served by the Umbriel fork and used by the Noctalia fork. It carries only
what no standard protocol covers. Output modes, positions, scale, and VRR stay on `zwlr_output_manager_v1`.

The XML lands in `protocols/desktop-unstable-v1.xml` in both forks when the first interface is implemented. Interfaces
are added one at a time, with the module that needs them.

## Access

The globals are advertised only to clients without a `wp_security_context_v1` (sandboxed clients never see them).

## `dsk_motion_manager_v1` / `dsk_motion_v1`

Compositor-driven motion for the shell's own surfaces.

| Message | Kind | Args | Meaning |
|---|---|---|---|
| `get_motion` | request | `new_id dsk_motion_v1`, `object wl_surface` | Surface must be a layer surface of this client; error `invalid_surface` otherwise |
| `animate` | request | `uint property`, `fixed target`, `string curve` | Retarget `property` from its current value to `target` with the compositor's named curve (Umbriel `CurveRegistry`). Starts on the next output frame |
| `set` | request | `uint property`, `fixed value` | Snap without animating |
| `settled` | event | `uint property`, `uint tv_sec_hi`, `uint tv_sec_lo`, `uint tv_nsec` | The property reached its target at this presentation time |
| `destroy` | request | | Motion values reset to identity |

`property` enum: `translate_x`, `translate_y` (surface-local logical px), `scale` (1.0 = identity), `opacity`
(0.0 to 1.0), `corner_radius` (logical px).

Requests take effect immediately, not on `wl_surface.commit`. To open a panel, the shell commits its content once,
then sends `animate`. Retargeting mid-flight continues from the current value and velocity, the same as Umbriel's
`AnimatedValue::retarget`, so there is no cancel request.

Curves are referenced by name so their parameters have one owner. Noctalia reads the same names from Umbriel through
the `curve` event below and uses them for content animations it still draws itself.

| Message | Kind | Args | Meaning |
|---|---|---|---|
| `curve` | event (manager) | `string name`, `uint easing`, `fixed a`, `fixed b`, `fixed c`, `fixed d` | Sent on bind and on config reload. Bezier: `a..d` = x1, y1, x2, y2. Spring: `a..c` = damping, stiffness, mass |
| `curves_done` | event (manager) | | End of a curve batch |

## `dsk_output_manager_v1`

Mirroring. Everything else about outputs uses `zwlr_output_manager_v1`.

| Message | Kind | Args | Meaning |
|---|---|---|---|
| `set_mirror` | request | `string target`, `string source` | `target` shows `source`'s contents, scaled to fit with letterboxing. Connector names as in `zwlr_output_head_v1.name` |
| `clear_mirror` | request | `string target` | `target` returns to extended mode |
| `mirror` | event | `string target`, `string source` | Current state, sent on bind and on every change. Empty `source` means not mirrored |
| `failed` | event | `string target`, `string reason` | Request rejected (unknown connector, mirror chain, disabled source) |

Hotplug profiles need no protocol: the compositor keys each saved arrangement on the set of connected
`make`/`model`/`serial_number` triples and restores it when the same set reappears.

## `dsk_input_manager_v1` / `dsk_input_device_v1`

One object per libinput device. Requests map 1:1 onto the libinput config call listed in
[native-apis.md](native-apis.md#input); a request for an option the device lacks raises `unsupported`.

| Message | Kind | Args |
|---|---|---|
| `device` | event (manager) | `new_id dsk_input_device_v1` |
| `info` | event | `string name`, `uint vendor`, `uint product`, `uint kind` (keyboard, pointer, touchpad, touch, tablet), `uint supported` (bitmask of the options below) |
| `state` | event | same fields as the setters, sent after `info` and after every change |
| `removed` | event | device unplugged; client destroys the object |
| `set_tap`, `set_tap_drag`, `set_natural_scroll`, `set_dwt`, `set_left_handed`, `set_middle_emulation` | request | `uint enabled` |
| `set_accel` | request | `uint profile` (flat, adaptive), `fixed speed` (-1.0 to 1.0) |
| `set_scroll_method` | request | `uint method` (none, two_finger, edge, on_button) |
| `set_click_method` | request | `uint method` (none, button_areas, clickfinger) |
| `set_keymap` | request (keyboards) | `string layout`, `string variant`, `string options` (xkb names, comma separated) |
| `set_repeat` | request (keyboards) | `int rate` (Hz), `int delay` (ms) |

The compositor persists each change keyed by device name plus vendor and product id, so a replugged mouse gets its
settings back.

## Versioning

Every interface starts at version 1. Additions bump the version. A breaking change gets a new interface name.
