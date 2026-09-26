# Gaps

What stands between upstream Umbriel + Noctalia (plus the fork work so far) and the desktop in `standards.md` and
`power.md`. Inventory taken 2026-09-26 from both source trees. Each row names its owner: **C** compositor, **S**
shell, **P** portal, **X** protocol, **N** session. **U** marks work that fits upstream `SCOPE.md` and goes there
first.

## Tier 0: efficiency and structure (blocks everything else)

| # | Gap | Owner | Evidence | Fix |
|---|---|---|---|---|
| 0.1 | 10 Hz background frame timer always armed; forced `wl_surface.frame` on every app on a hidden workspace | C U | `src/server/server_events.cpp` | **Fixed**: hidden windows are suspended and get no frames; games (content type or `background_frames` rule) keep 10 Hz; a hidden window with a pending configure gets ticks until it commits; timer disarmed otherwise. Harness check 637 |
| 0.2 | xdg-shell capped at v3 | C U | `server.cpp:461` | **Fixed**: v6 (`suspended`, `wm_capabilities`, `configure_bounds`) |
| 0.3a | Bar committed a frame every second on a static screen (unguarded `Flex::setPadding` in the per-second relayout; gauges repainting sub-pixel changes), keeping PSR from staying on | S | `shell/bar/bar.cpp:1507`, `ui/controls/flex.cpp`, `sysmon_widget.cpp` | **Fixed**; guarded by `tests/e2e/idle_commits.sh` |
| 0.3 | Shell idle wakeups: lock keys polled every 200 ms, clock 1 Hz, system monitor 2/3/10 s even when nothing shows it, icon theme 60 s | S | `system/lock_keys_service.cpp`, `time/time_service.cpp`, `system_monitor_service.cpp` | **Lock keys fixed**: compositor reports them over `dsk_shell_v1.lock_keys`, the poll stops (headless idle shell 27 → 6 wakeups/s). Clock, monitor, icon theme remain |
| 0.4 | Always-on shell threads (system monitor sampler, brightness worker without ddcutil, secret store, template worker, 2-4 thumbnail, 2-4 texture, 4+2 script pools, calendar) | S | report sites in `system_monitor_service.cpp:1202`, `brightness_service.cpp:705`, `render/core/*` | Create on first use, join when idle; script pools only when a plugin is enabled |
| 0.5 | Mirror re-rendered at the refresh rate on a static source (61 commits/s) | C | `src/output/output.cpp` | **Fixed**: draws only on a new source frame; check 634. The texture import per source frame remains |
| 0.6 | Protocol XML duplicated in both forks, synced by hand | X | `protocols/desktop-unstable-v1.xml` in both | Monorepo `protocol/` as the only copy (`standards.md`) |
| 0.7 | Keybinds spawned a `noctalia msg` process per keypress | C S X | `desktop_shell.cpp`, `wayland_connection.cpp` | **Fixed**: `dsk_shell_v1.action` + compositor `shell:<cmd>` action run the command in the shell over its existing connection. E2E `shell_protocol.sh`. User keybinds still need migrating from `spawn:noctalia msg …` to `shell:…` |
| 0.8 | Shell started by compositor autostart; no restart on crash | N | `general.autostart = ["noctalia"]` | `noctalia.service` under `umbriel-session.target` |
| 0.9 | Power actions spawned `systemctl`, falling back through eleven commands down to `sudo -n sh -c "echo mem > /sys/power/state"` | S | `shell/session/session_action_runner.cpp` | **Fixed**: logind `Manager.Suspend/Reboot/PowerOff` on a per-action system-bus connection; a configured override command still wins. E2E `power_actions.sh` with a mock logind |
| 0.10 | Dark mode pushed by spawning `gsettings`; no Settings portal | S P | `app/application_services.cpp:127-141` | Shell serves `org.freedesktop.impl.portal.Settings` (`color-scheme`, `accent-color`, `contrast`); our portals.conf routes it |
| 0.11 | Output settings persisted in two places (`config.toml` rules and `displays.toml`) | C | `output/display_store.cpp` | One owner, lock error on conflict, like `input.toml` |
| 0.12 | Umbriel lacked `wlr-output-power-management`; shell idle-off used IPC (and `wlr-randr` on generic compositors) | C S | `server_events.cpp`, `wayland_connection.cpp` | **Fixed**: compositor serves the protocol (check 638); shell powers outputs through it on Umbriel and generic compositors, falling back to the old paths only when absent (E2E `screen_power.sh`) |

## Tier 1: a complete session

| # | Gap | Owner | Fix |
|---|---|---|---|
| 1.1 | Lid: compositor runs user commands, shell ignores `LidIsClosed`, logind also acts | N S | One owner: logind handles the lid; shell holds the sleep-delay inhibitor and locks before sleep (`PrepareForSleep`) |
| 1.2 | Hibernate and suspend-then-hibernate missing from the session menu | S | logind `Hibernate`, `SuspendThenHibernate`, gated on `CanHibernate` |
| 1.3 | Idle chain is off by default and has no media inhibit | S | Defaults on: dim, lock, output power off, suspend; MPRIS `Playing` inhibits |
| 1.4 | Lock screen PAM service hard-coded to `login` | S | Own `/etc/pam.d/<name>-lock` shipped in `session/` |
| 1.5 | Input settings: compositor side on `wip/input-settings`; shell client, XKB catalog, page missing | C S X | Shell `InputControl` client bound while the page is open, XKB catalog from `/usr/share/X11/xkb/rules/evdev.xml` (libxml2), the page; rebase against 0.11 (both touch `display_store.cpp`); move `[input.*]` out of `config.toml` into `input.toml` |
| 1.6 | Date and time, language pages | S | `timedate1`, `locale1` (signatures in `native-apis.md`) |
| 1.7 | Default apps page | S | `mimeapps.list` + inotify; desktop entries already indexed |
| 1.8 | Drives: no UDisks2, no automount | S | `org.freedesktop.UDisks2` ObjectManager, `Filesystem.Mount`, notifications on insert |
| 1.9 | Screen recording | S P | PipeWire stream from our ScreenCast portal, VA-API encode, region select reused from screenshots |
| 1.10 | Airplane mode (all radios), hotspot | S | `/dev/rfkill` for all types (writer exists for Wi-Fi); NM `AddAndActivateConnection` with `mode=ap` |
| 1.11 | Portal covers only ScreenCast and Screenshot; everything else falls to GTK | P | Implement Settings (0.10), Inhibit (maps to idle inhibit), GlobalShortcuts (maps to keybinds); FileChooser stays GTK |
| 1.12 | Third-party session daemons (`kded6`, `kdeconnect`, `gvfs-*`) | N | Keep only what a daily app needs; the rest leaves the session |
| 1.13 | `fc-list` and `xdg-open` spawns | S | fontconfig API; OpenURI portal |

## Tier 2: platform depth

| # | Gap | Owner |
|---|---|---|
| 2.1 | KMS overlay planes (`wlr_output_layer`) for video and cursor | C U |
| 2.2 | Missing protocols apps use: xdg-dialog, xdg-toplevel-drag, single-pixel-buffer, alpha-modifier (consumer code exists, no global), fifo, commit-timing, xdg-toplevel-icon, xdg-system-bell | C U |
| 2.3 | Accessibility: AT-SPI, screen zoom (compositor), screen reader path | C S |
| 2.4 | Location from `api.noctalia.dev` HTTP (privacy, network wakeups); external IP lookup on the same host | S: geoclue D-Bus, IP lookup off by default |
| 2.5 | Luau plugins unsandboxed with shell-wide file and process access | S: plugins off by default; sandbox before enabling any store |
| 2.6 | Printers | S: CUPS via libcups/IPP once cupsd is installed |
| 2.7 | drm-lease (VR) | C U |

## Frozen until a measurement or the gap log asks

- `dsk_motion_manager_v1`. Noctalia already animates panels inside fixed-size surfaces, so the surface-resize cost
  the protocol was meant to remove may not exist. Measure shell wakeups and frame time during a panel animation first.
- Casting (Miracast, Chromecast), floating-first layout, battery-driven effect scaling beyond 0.3.

## Order of work

1. Monorepo layout (`standards.md`), protocol in one place, session units. (0.6, 0.8)
2. Baseline on battery per `power.md`.
3. Tier 0 efficiency fixes, each with a bench row: 0.1, 0.2, 0.3, 0.4, 0.5.
4. Tier 0 structure: 0.7, 0.9, 0.10, 0.11, 0.12.
5. Tier 1, in the order the user hits the gaps.
