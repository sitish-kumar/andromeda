# Private protocol

One Wayland protocol, `desktop-unstable-v1`, served by the Umbriel fork and used by the Noctalia fork. It carries only
what no standard protocol covers. Output modes, positions, scale, and VRR stay on `zwlr_output_manager_v1`.

The XML is `protocols/desktop-unstable-v1.xml` in both forks. `dsk_output_manager_v1` is implemented; the others are
added with the module that needs them.

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
| `done` | event | | End of the state batch sent on bind |
| `set_property` | request | `string target`, `string key`, `string value` | A display property `zwlr_output_manager_v1` lacks: `vrr` (disabled, always, fullscreen), `hdr`, `sdr_white`, `tearing`, `workspaces` (dynamic or 1 to 64), `min_workspaces`, `cyclic_workspaces`, `workspace_axis`. Saved in `displays.toml` under the display's descriptor; refused when `config.toml` sets that `[output]` table |
| `property` | event | `string target`, `string key`, `string value` | Every output's properties on bind and after each configuration change |
| `failed` | event | `string target`, `string reason` | Request rejected (unknown connector, mirror chain, disabled source) |

Hotplug profiles need no protocol: the compositor keys each saved arrangement on the set of connected
`make`/`model`/`serial_number` triples and restores it when the same set reappears.

## `dsk_settings_manager_v1`

Every compositor setting Settings may change, named by its config key (`input.touchpad.tap`,
`appearance.border_width`, `layout.mode`), plus the connected input devices. The allow-list, with each key's type
and range, is `compositor/src/config/managed_settings.cpp`; window and layer rules, keybinds, outputs, and session
tables stay file-only. It replaced `dsk_input_manager_v1`, which covered input keys only and refused keys the user's
`config.toml` set.

| Message | Kind | Args |
|---|---|---|
| `set` | request | `string key`, `string value`. Empty value unsets the key. Answered by `setting` events and `done`, or `failed` |
| `device_added` | event | `string name`, `uint kind` (keyboard, mouse, touchpad, touch, tablet); every device on bind, then on connect |
| `device_removed` | event | `string name` |
| `setting` | event | `string key`, `string value`, `uint customized`. Every key on bind and after every configuration change |
| `done` | event | End of a batch |
| `failed` | event | `string key`, `string reason` (unknown key, wrong type, out of range, write error) |
| `bind` | request | `string chord`, `string action` in config syntax. Empty action removes the settings file's entry; `none` unbinds the chord |
| `capture_chord` | request | The next key press goes to `chord_captured` instead of keybinds and clients; Escape cancels |
| `cancel_capture` | request | |
| `keybind` | event | `string chord`, `string action`, `uint customized`. Every effective bind, plus chords the settings file unbinds (`none`) |
| `action_spec` | event | `string name`, `string param`, `string summary`. Every bindable action, on bind |
| `chord_captured` | event | `string chord`, empty when cancelled |

The compositor validates each value against the same range its config reader enforces, writes it to
`settings.toml` next to `config.toml`, and reloads. It loads that file after `config.toml` and its includes, with no
include line needed, so a value set from Settings wins over one written by hand; `customized` is 1 while the file
holds the key, and unsetting it brings back the hand-written value or the default. A generated `input.toml` from
before is renamed to `settings.toml` on first start.

## Versioning

Every interface starts at version 1. Additions bump the version. A breaking change gets a new interface name.
