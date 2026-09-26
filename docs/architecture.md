# Architecture

## Processes

```
                 ┌──────────────────────────── session (systemd user) ───────────────────────────┐
                 │                                                                               │
 kernel DRM/KMS ─┤ umbriel ──────────── Wayland (standard + private protocol) ─────── noctalia    │
 evdev, logind   │  wlroots 0.20, umbrielfx                                          shell, settings UI,
                 │  outputs, input, layouts,                                         system services,
                 │  motion, effects                                                  Settings portal backend
                 │     │                                                                  │     │
                 │  xdg-desktop-portal-umbriel (ScreenCast, Screenshot) ◄── PipeWire      │     │
                 └─────┼──────────────────────────────────────────────────────────────────┼─────┼──┘
                       │                                                    system D-Bus ─┘     └─ session D-Bus
                                       timedate1, locale1, UDisks2, NetworkManager, BlueZ, UPower, CUPS
```

Three long-running processes: compositor, shell, portal. There is no settings daemon: the settings UI is a Noctalia
window, and it applies each change by calling the owner of that setting directly.

## Ownership

| State | Owner (applies it) | Persisted in | Shell reaches it through |
|---|---|---|---|
| Output modes, position, scale, VRR | Umbriel | Umbriel config | `zwlr_output_manager_v1` (standard) |
| Mirroring, hotplug profiles | Umbriel | `displays.toml` (`mirror` key) | private protocol, `output` interface |
| Casting to wireless displays (Miracast, Chromecast) | Casting service (planned) | none | PipeWire stream from xdg-desktop-portal ScreenCast; see below |
| Input devices, keyboard layout | Umbriel | Umbriel config | private protocol, `input` interface |
| Surface motion (shell animations) | Umbriel | nothing (runtime) | private protocol, `motion` interface |
| Layout per workspace | Umbriel | Umbriel config | Umbriel IPC (existing) |
| Theme, colors, app templates | Noctalia | Noctalia config | in-process |
| Appearance for apps (dark mode, accent) | Noctalia | Noctalia config | Noctalia serves `org.freedesktop.impl.portal.Settings` |
| Time, timezone, NTP | systemd-timedated | `/etc` | `org.freedesktop.timedate1` |
| System locale, console/X11 keymap | systemd-localed | `/etc` | `org.freedesktop.locale1` |
| Drives, mounts | UDisks2 | none | `org.freedesktop.UDisks2` |
| Printers | cupsd | `/etc/cups` | IPP via libcups |
| Default apps | XDG spec | `~/.config/mimeapps.list` | the file, watched with inotify |
| Network, Bluetooth, power, audio | existing daemons | theirs | already in Noctalia |

The compositor never reads shell config. The shell never writes compositor config. A settings change travels as a
protocol request; the compositor applies it, persists it, and answers with the resulting state as events.

## Why a private Wayland protocol, not a socket

The shell already holds a Wayland connection polled by its main loop. A private protocol on that connection costs no
new socket, no new thread, no JSON parsing, and ties each request to the client that sent it. Events arrive in order
with the output and surface events they refer to. Umbriel's existing JSON IPC (`umbriel msg`) stays for scripts and
the test harness. See [protocol.md](protocol.md).

## Window model

Every workspace runs one layout: scrolling, dwindle, master (all upstream), or floating (fork). Any window can float
above any layout (upstream `src/view/floating.cpp`). A floating-first workspace is a new layout paradigm, which
upstream `SCOPE.md` declines, so it lives in the fork behind the same layout interface as the other three.

## Motion

One clock and one curve model for everything that moves:

- Clock: the compositor's presentation clock. `wp_presentation` exposes it to clients.
- Curves: Umbriel's `AnimationCurve` (bezier, named easings, spring with damping, stiffness, mass). The shell uses the
  same parameters, so a panel and a window with the same curve name move the same way.
- Surface-level motion (translate, scale, opacity, corner radius, clip) of shell surfaces runs inside the compositor
  through the private `motion` interface. The shell commits its content once; the compositor animates the texture.
  The shell process stays asleep for the whole animation.
- Content motion inside a surface (a slider knob, a list scroll) is still drawn by the shell. That is correct: only
  the owner of the pixels can redraw them.

## Efficiency model

The compositor commits a frame only when something changed, which lets Intel PSR and VRR idle the panel on their own.
Details and budgets are in [performance.md](performance.md). Work the fork adds on top of upstream:

1. Assign fullscreen and video surfaces to KMS overlay planes through `wlr_output_layer`, so video plays without GPU
   composition.
2. Pick the idle strategy per power source by measurement: PSR (the panel refreshes itself from its own buffer) or
   VRR (the panel drops to its minimum refresh). i915 may refuse PSR while VRR is on, so the two are compared with
   `tools/measure-idle.sh` on this machine before a default is chosen.
3. Scale effects to the power source: on battery, blur uses its cached result and skips re-blurring unchanged
   regions.

## Mirroring (built)

A mirroring output leaves the desktop and the `wl_output` globals (wlroots removes the global with the layout entry,
and it asserts if a client asks for `xdg_output` of an output outside the layout), stays powered, and draws each
committed source frame letterboxed with both transforms applied. It costs one texture draw per source frame and
nothing at idle. The source switches to a software cursor while mirrored so the pointer shows on both.

## Casting (planned)

Wireless displays behave like a mirror whose target is a network sink instead of a connector:

1. Capture: the compositor's ScreenCast portal already produces a PipeWire stream of an output
   (`xdg-desktop-portal-umbriel`).
2. Encode: VA-API H.264 through GStreamer (`vah264enc`), hardware encoded on the Intel GPU.
3. Transport, one per sink family:
   - Miracast: Wi-Fi P2P through `wpa_supplicant`'s P2P D-Bus interface (`fi.w1.wpa_supplicant1.Interface.P2PDevice`),
     then RTSP (WFD) plus RTP/MPEG-TS.
   - Chromecast: mDNS discovery (`_googlecast._tcp` via Avahi), the Cast v2 protocol over TLS, and a WebRTC or
     mirroring session.
4. UI: sinks appear in the Super+P switcher next to wired displays.

Every step is a native API listed in [native-apis.md](native-apis.md) before it is built. GNOME Network Displays
implements the same pipeline; reading its failure modes on this machine comes first.

## Fork strategy

| Fork | Carries | Upstream candidate |
|---|---|---|
| umbriel | private protocol server, mirroring, hotplug profiles, floating layout, output layers, battery policy | output layers, mirroring (ask first, per SCOPE.md) |
| noctalia | settings pages (displays, input, date and time, language, drives, printers, default apps), motion client, Settings portal | displays page (Noctalia already reads `zwlr_output_manager_v1`) |

## Open decisions

- **XWayland scaling.** Upstream runs X11 clients through xwayland-satellite, where fractionally scaled X11 apps are
  blurry. Decide after measuring which X11 apps are still in daily use.
- **Project name.** `desktop` is a working name.
