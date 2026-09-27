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
| 0.4 | Always-on shell threads (system monitor sampler, brightness worker without ddcutil, secret store, template worker, 2-4 thumbnail, 2-4 texture, 4+2 script pools, calendar) | S | report sites in `system_monitor_service.cpp:1202`, `brightness_service.cpp:705`, `render/core/*` | **Per-call `runAsync` helper thread fixed**: children are reaped from the main loop over pidfds (E2E `process_async_callback.sh`). The rest remains: create on first use, join when idle; script pools only when a plugin is enabled |
| 0.5 | Mirror re-rendered at the refresh rate on a static source (61 commits/s) | C | `src/output/output.cpp` | **Fixed**: draws only on a new source frame; check 634. The texture import per source frame remains |
| 0.6 | Protocol XML duplicated in both forks, synced by hand | X | `protocols/desktop-unstable-v1.xml` in both | Monorepo `protocol/` as the only copy (`standards.md`) |
| 0.7 | Keybinds spawned a `noctalia msg` process per keypress | C S X | `desktop_shell.cpp`, `wayland_connection.cpp` | **Fixed**: `dsk_shell_v1.action` + compositor `shell:<cmd>` action run the command in the shell over its existing connection. E2E `shell_protocol.sh`. User keybinds still need migrating from `spawn:noctalia msg …` to `shell:…` |
| 0.8 | Shell started by compositor autostart; no restart on crash | N | `general.autostart = ["noctalia"]` | `noctalia.service` under `umbriel-session.target` |
| 0.9 | Power actions spawned `systemctl`, falling back through eleven commands down to `sudo -n sh -c "echo mem > /sys/power/state"` | S | `shell/session/session_action_runner.cpp` | **Fixed**: logind `Manager.Suspend/Reboot/PowerOff` on a per-action system-bus connection; a configured override command still wins. E2E `power_actions.sh` with a mock logind |
| 0.10 | Dark mode pushed by spawning `gsettings`; no Settings portal | S P | `dbus/portal/settings_portal.cpp` | **Fixed**: the shell serves `org.freedesktop.impl.portal.Settings` (`color-scheme`, SettingChanged) as `org.freedesktop.impl.portal.desktop.noctalia`, routed by `umbriel-portals.conf`; GTK 3's GSettings key is written in-process. E2E `settings_portal.sh` (no spawn, flat memory). Accent colour remains |
| 0.11 | Output settings persisted in two places (`config.toml` rules and `displays.toml`) | C | `output/display_store.cpp` | **Fixed**: `documentSetsOutput()` (direct table lookup, not `input.toml`'s dotted `at_path`, since an EDID descriptor name can hold the dots and spaces that would parse as a nested path) gates every `displays.toml` write in `server_events.cpp` and `server_outputs.cpp`; an output `config.toml` already has an `[output.<name>]` table for still applies live but is no longer persisted, with the reason logged. Harness check 639 |
| 0.12 | Umbriel lacked `wlr-output-power-management`; shell idle-off used IPC (and `wlr-randr` on generic compositors) | C S | `server_events.cpp`, `wayland_connection.cpp` | **Fixed**: compositor serves the protocol (check 638); shell powers outputs through it on Umbriel and generic compositors, falling back to the old paths only when absent (E2E `screen_power.sh`) |

## Tier 1: a complete session

| # | Gap | Owner | Fix |
|---|---|---|---|
| 1.1 | Lid ownership | N S | **Verified 2026-09-27**: logind is the only owner (`HandleLidSwitch=suspend`), the shell holds the "Lock before sleep" delay inhibitor, the compositor config has no lid command |
| 1.2 | Hibernate and suspend-then-hibernate missing from the session menu | S | **Fixed**: logind `Hibernate`, `SuspendThenHibernate`, gated at call time on `CanHibernate`/`CanSuspendThenHibernate`; a configured override command still wins. `noctalia msg session hibernate\|suspend-then-hibernate`, session-panel actions. E2E `power_actions.sh` |
| 1.3 | Idle chain defaults and media inhibit | S | **Decided**: behaviours stay user-enabled (an unasked auto-suspend is worse than none); no MPRIS inhibit, because players that must keep the screen on use idle-inhibit, which the compositor honours for visible surfaces |
| 1.4 | Lock screen PAM service is `login` | S | **Deferred**: `login` authenticates correctly; a dedicated stack only matters once fingerprint/2FA policy differs from TTY login |
| 1.5 | Input settings page | C S X | **Done**: shell `InputControl` client bound while the page is open, XKB catalog from `evdev.xml`; E2E `input_settings.sh` |
| 1.6 | Date and time, language pages | S | **Done**: `timedate1`, `locale1`; E2E `date_time_settings.sh`, `language_settings.sh` |
| 1.7 | Default apps page | S | **Done**: `mimeapps.list`, refreshed on an inotify watch while the page shows; E2E `default_apps.sh`, `default_apps_watch.sh` |
| 1.8 | Drives: no UDisks2, no automount | S | **Done**: `dbus/udisks/udisks_service.cpp` automounts hotplugged filesystems with HintAuto (not HintSystem/HintIgnore) over UDisks2 signals; a notification opens the drive on click and offers Eject (Unmount + Drive.PowerOff). `[shell] automount_drives`. E2E `drives.sh` |
| 1.9 | Screen recording | S P | PipeWire stream from our ScreenCast portal, VA-API encode, region select reused from screenshots |
| 1.10 | Airplane mode (all radios), hotspot | S | **Airplane done**: one `RFKILL_OP_CHANGE_ALL` / `RFKILL_TYPE_ALL` write, control-center toggle and `airplane-toggle`/`airplane-status` IPC; E2E `airplane.sh` (file stand-in for /dev/rfkill). Hotspot remains |
| 1.11 | Portal covers only ScreenCast and Screenshot; everything else falls to GTK | P | Implement Settings (0.10), Inhibit (maps to idle inhibit), GlobalShortcuts (maps to keybinds); FileChooser stays GTK |
| 1.12 | Third-party session daemons (`kded6`, `kdeconnect`, `gvfs-*`) | N | Keep only what a daily app needs; the rest leaves the session |
| 1.13 | `fc-list` and `xdg-open` spawns | S | **Fixed**: URLs open through GIO (E2E `native_spawns.sh`); the font catalog uses `FcFontList` in-process (E2E `font_catalog.sh`: 724 vs 731 families, no spawn, flat memory) |

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

## Spawn and thread audit (shell/src, 2026-09-27)

Every `process::run{Async,Sync}` call, every raw `fork`/`execv*`, and every `std::thread`/`std::jthread` in
`shell/src`, with what it's for and whether a native replacement applies. Grep used: `process::runAsync`,
`process::runSync`, `popen(`, `system(`, `fork(`, `execv`, `std::thread`, `std::jthread`.

### Process spawns

| Site | What | Verdict |
|---|---|---|
| `app/application_services.cpp` | `gsettings`/`dconf` fallback for dark mode | **Fixed** (0.10): Settings portal, GSettings written in-process |
| `app/application_services.cpp:1005`, `shell/wallpaper/wallpaper.cpp:296` | `dbus-send … org.kde.PlasmaShell.evaluateScript` (KDE-only wallpaper sync) | Native replacement exists (a direct sdbus-c++ call, same as every other D-Bus site in the shell) but `notifyKdePlasmaWallpaper` is a free function with no bus reference; needs one threaded through `Wallpaper`. Not default path (KDE only), not attempted |
| `app/application_ipc.cpp:790,798` | The `exec`/run-command IPC verbs | Legitimate: running an arbitrary command *is* the feature |
| `system/terminal_launch.cpp:155` | Launches the configured terminal emulator | Legitimate: launching an app is the feature |
| `system/desktop_entry_launch.cpp:267,271` | Launches a `.desktop` entry's `Exec=` | Legitimate; already tries D-Bus activation first when `dbusActivatable` |
| `system/brightness_service.cpp:478,499,509` | `ddcutil` for external-monitor brightness | Legitimate: optional dependency (`pkg/PKGBUILD` optdepends), no common native DDC/CI library linked |
| `launcher/dmenu_provider.cpp:125,263,270` | Launches the configured `dmenu`/`rofi`/`fuzzel` | Legitimate: launching the configured picker is the feature |
| `scripting/plugin_git.cpp:79` | `git` for plugin clone/pull/export | Legitimate: no `libgit2` linked, calling `git` is the standard approach for this feature |
| `scripting/luau_host.cpp:143,2042,2399` | A plugin's own configured shell command, run from its Luau sandbox | Legitimate: the plugin API's job |
| `shell/desktop/widgets/desktop_button_widget.cpp:309` | A desktop button widget's configured command | Legitimate, user-configured |
| `shell/session/session_action_runner.cpp:92,100,151` | A configured override command for suspend/reboot/shutdown/hibernate/suspend-then-hibernate | Legitimate, opt-in; the default path is already logind directly (0.9, and this session's hibernate work) |
| `shell/clipboard/clipboard_panel.cpp:1655` | A configured clipboard image action | Legitimate, user-configured |
| `shell/settings/font_family_catalog.cpp` | `fc-list` | **Fixed** (1.13): `FcFontList` in-process |
| `shell/greeter/greeter_appearance_sync.cpp:327,890,897,924` | Greeter theme sync through the configured escalator (`pkexec` by default) | Legitimate: privilege escalation needs a helper process, no native API for that |
| `shell/bar/widget_action_dispatcher.cpp:60` | A bar widget's configured gesture action | Legitimate, user-configured |
| `compositors/compositor_platform.cpp:131,137,1562` | `labwc --exit`/`-e`, a configured exit command | Legitimate: foreign-compositor compatibility, only reached when Umbriel isn't the compositor |
| `compositors/ext_workspace/ext_workspace_output_backend.cpp:7` | `wlr-randr --on/--off` | Already documented (0.12): fallback for generic compositors lacking `wlr-output-power-management` |
| `compositors/sway/sway_keyboard_backend.cpp:39,57,88`, `sway_output_backend.cpp:54,69` | `swaymsg`, raw `fork`+`execvp` for the layout switch | Legitimate: sway has no other control surface, only reached under sway. The raw fork+exec (not `process::runAsync`) is inconsistent style, not a spawn violation |
| `theme/hook_runner.cpp:121`, `theme/template_apply_service.cpp:453`, `theme/template_engine.cpp:1630,1645,1665` | User-configured theme hooks, dynamic-color commands, `apply.sh` | Explicitly kept per `standards.md`: "template `apply.sh` hooks stay, but only on explicit theme change" |
| `main.cpp:171,209` | `fork`+`execvp` to daemonize | Not a CLI spawn: this is the shell's own self-re-exec, the standard way to daemonize |
| `auth/pam_authenticator.cpp:344` | `fork` for a PAM conversation | Legitimate: standard PAM privilege-separation practice |
| `capture/screenshot_service.cpp:265` | Raw `fork`+`execv` piping the PNG to a configured screenshot action's stdin | Legitimate feature (needs stdin piping `process::runAsync` doesn't support), user-configured |

Net: every spawn left is user-configured, an optional dependency, or a foreign-compositor/privilege-escalation
shim with no native alternative.

### Always-running threads

| Site | What | Verdict |
|---|---|---|
| `security/secret_store.cpp:628` | Worker thread started in the constructor | Gap 0.4 ("secret store"); should start on first secret lookup |
| `system/system_monitor_service.cpp:1202` | Sampler thread | Gap 0.4 ("system monitor sampler") |
| `system/brightness_service.cpp:705` | Worker thread, started even without `ddcutil` | Gap 0.4 ("brightness worker without ddcutil") |
| `render/core/thumbnail_service.h:140`, `render/core/async_texture_cache.h:125` | 2-4 decode/upload workers each | Gap 0.4 ("2-4 thumbnail", "2-4 texture") |
| `scripting/script_worker_pool.h:30`, `scripting/script_io_pool.h:35` | 4+2 Luau script pools | Gap 0.4; fix already scoped there: only when a plugin is enabled |
| `theme/template_apply_service.h:85` | Template-apply worker | Gap 0.4 ("template worker") |
| `calendar/caldav_client.cpp:185`, `calendar/calendar_service.cpp:249` | `std::jthread` workers | Gap 0.4 ("calendar") |
| `shell/wallpaper/panel/wallpaper_scanner.cpp:152` | Background wallpaper directory scanner | Not in 0.4's list. Starts when the wallpaper panel builds its model; not audited for join-when-idle |
| `core/process/async_process_manager.h` | Callback-based `process::runAsync` | **Fixed**: no thread; pidfd and pipes polled by the main loop |
| `scripting/plugin_manager.cpp:413,476,783`, `scripting/plugin_file_cache.cpp:103`, `scripting/luau_host.cpp:141,2040,2125`, `shell/session/session_action_runner.cpp:134,146`, `shell/lockscreen/lock_screen.cpp:1108`, `shell/settings/settings_window_popups.cpp:2273`, `shell/settings/settings_window.cpp:872`, `capture/screenshot_service.cpp:250` | One-shot `std::thread` per call (plugin git ops, PAM auth, plugin export, power actions, screenshot save) so a blocking call doesn't stall the main loop | All would be subsumed by the `process.cpp:806` fix above where they're calling through `process::runAsync`; the PAM and plugin-export ones need their own async primitive. None touched this session (same reasoning) |

## Frozen until a measurement or the gap log asks

- `dsk_motion_manager_v1`. Noctalia already animates panels inside fixed-size surfaces, so the surface-resize cost
  the protocol was meant to remove may not exist. Measure shell wakeups and frame time during a panel animation first.
- Casting (Miracast, Chromecast), floating-first layout, battery-driven effect scaling beyond 0.3.

## Order of work

Done: monorepo and session units (0.6, 0.8), battery baseline, 0.1, 0.2, 0.5, 0.7, 0.9, 0.10, 0.11, 0.12, lock keys
(0.3), the `runAsync` helper thread (0.4), logout crash, AC/battery power profiles, 1.1, 1.2, 1.5-1.8, airplane
mode (1.10), 1.13.

Next, in this order (each lands with an E2E or harness proof and, for power items, a bench row):
1. Bar per-second redraw on vertical bars with sysmon gauges (0.3a follow-up), then the minute-aligned tick where
   every consumer on screen allows it (0.3 remainder), measured before deciding.
2. Radios: hotspot through NetworkManager (1.10 rest).
3. Screen recording through our ScreenCast portal with VA-API encode (1.9).
4. Portal: Inhibit and GlobalShortcuts backends mapped onto idle inhibit and keybinds (1.11 rest).
5. Session diet and identity: our own `XDG_CURRENT_DESKTOP`, portals.conf, and a daemon audit (1.12).
6. Tier 2 in the order apps need it: overlay planes (2.1), missing protocols (2.2), accessibility (2.3).
