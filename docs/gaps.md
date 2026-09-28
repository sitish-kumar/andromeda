# Gaps

What stands between upstream Umbriel + Noctalia (plus the fork work so far) and the desktop in `standards.md` and
`power.md`. Inventory taken 2026-09-26 from both source trees. Each row names its owner: **C** compositor, **S**
shell, **P** portal, **X** protocol, **N** session. **U** marks work that fits upstream `SCOPE.md` and goes there
first.

## Tier 0: efficiency and structure (blocks everything else)

| # | Gap | Owner | Evidence | Fix |
|---|---|---|---|---|
| 0.1 | 10 Hz background frame timer always armed; forced `wl_surface.frame` on every app on a hidden workspace | C U | `src/server/server_events.cpp` | **Fixed**: hidden windows are suspended and get no frames; games (content type or `background_frames` rule) keep 10 Hz; a hidden window with a pending configure gets ticks until it commits; timer disarmed otherwise. Views behind the lock screen or on a powered-off output are suspended too. Harness checks 637, 642; a static screen with a hidden game commits nothing (check 643) |
| 0.2 | xdg-shell capped at v3 | C U | `server.cpp:461` | **Fixed**: v6 (`suspended`, `wm_capabilities`, `configure_bounds`) |
| 0.3a | Bar committed a frame every second on a static screen (unguarded `Flex::setPadding` in the per-second relayout; gauges repainting sub-pixel changes), keeping PSR from staying on | S | `shell/bar/bar.cpp:1507`, `ui/controls/flex.cpp`, `sysmon_widget.cpp` | **Fixed**; guarded by `tests/e2e/idle_commits.sh` |
| 0.3 | Shell idle wakeups: lock keys polled every 200 ms, clock 1 Hz, system monitor 2/3/10 s even when nothing shows it, icon theme 60 s | S | `system/lock_keys_service.cpp`, `time/time_service.cpp`, `system_monitor_service.cpp` | **Lock keys fixed**: compositor reports them over `dsk_shell_v1.lock_keys`, the poll stops (headless idle shell 27 → 6 wakeups/s). Clock rounds its timeout up (one wakeup per second, not 1.2; idle clock-only shell 3.4 → 2.3 wakeups/s, E2E `idle_wakeups.sh`). A minute-aligned tick would save the remaining ~1/s at ~0.4 ms each and needs every per-second consumer to declare its granularity; not worth the regression risk until a battery bench says otherwise. System monitor samples only while something shows stats (E2E `sysmon_demand.sh`). The icon theme follows inotify on the icon roots, the active theme, and the GTK and dconf directories, with one check per burst, instead of a 60 s poll (E2E `icon_theme_events.sh`) |
| 0.4 | Always-on shell threads (system monitor sampler, brightness worker without ddcutil, secret store, template worker, 2-4 thumbnail, 2-4 texture, 4+2 script pools, calendar) | S | report sites in `system_monitor_service.cpp:1202`, `brightness_service.cpp:705`, `render/core/*` | **Per-call `runAsync` helper thread fixed**: children are reaped from the main loop over pidfds (E2E `process_async_callback.sh`). Brightness worker starts only with ddcutil detection; secret store, template-apply, thumbnail, and texture workers start with work and exit after 30 s idle; the system monitor sampler runs only while something shows stats. Calendar (vdir, CalDAV parse) and wallpaper-scan workers start with work and exit after 30 s idle; the Luau script pools were already created on first plugin use. Idle shell: 16 → 1 unnamed thread (the main one), 32 → 16 in total (E2E `idle_threads.sh`, `idle_workers_on_demand.sh`, `sysmon_demand.sh`). **Done** |
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
| 1.9 | Screen recording | S P | **Done** (video only): `capture/screen_recorder.cpp` takes a ScreenCast portal stream (our picker on first use per shell session) through GStreamer `pipewiresrc ! vapostproc ! vah264enc ! mp4mux` to `~/Videos`, dmabuf to the GPU encoder with no CPU copy; control-center toggle, `screen-record-toggle`/`screen-record-status` IPC. E2E `screen_record.sh` (test source, real VA-API encode); portal path checked live at 2880×1800. **Audio done**: the default output's monitor through `fdkaacenc` into the same MP4, video only when PipeWire refuses the stream; the E2E runs a private PipeWire with a null sink playing a tone and checks the AAC track's loudness. **Region done**: `screen-record-region` takes slurp's `X,Y WxH` or opens the screenshot region picker; the region is clipped to the portal stream's logical rect and cropped in buffer pixels once caps are known, from a system-memory copy since `vapostproc` ignores crop metadata on VA surfaces (full-screen recording stays zero-copy). E2E `screen_record_region.sh` compares the first frame against the cropped test pattern. The picker's drag is not driven in the E2E (no pointer injector in the harness) |
| 1.10 | Airplane mode (all radios), hotspot | S | **Airplane done**: one `RFKILL_OP_CHANGE_ALL` / `RFKILL_TYPE_ALL` write, control-center toggle and `airplane-toggle`/`airplane-status` IPC; E2E `airplane.sh` (file stand-in for /dev/rfkill). **Hotspot done**: `dbus/network/nm_hotspot.cpp` activates the first AP-mode NetworkManager profile or adds one (shared IPv4, random WPA2 key); control-center toggle, `hotspot-toggle`/`hotspot-status` IPC; E2E `hotspot.sh` with a mock NetworkManager |
| 1.11 | Portal covers only ScreenCast and Screenshot; everything else falls to GTK | P | **Done**: Settings, Inhibit (logind `idle`/`sleep` block inhibitors per request; E2E `inhibit_portal.sh`), and GlobalShortcuts (apps register ids, the user binds keys with `shell:global-shortcut <app-id> <id>`; no app grabs a key; Activated and Deactivated fire together, so hold-to-talk is not supported; E2E `global_shortcuts.sh`). FileChooser stays GTK |
| 1.12 | Third-party session daemons (`kded6`, `kdeconnect`, `gvfs-*`) | N | **Audited 2026-09-27**: identity is ours (`XDG_CURRENT_DESKTOP=umbriel`, `umbriel-portals.conf`). Every daemon running is activated by an app that uses it (kded6 by Dolphin, gvfs by GIO apps, at-spi by toolkits, dconf by the GTK 3 colour-scheme key, portal-gtk for FileChooser) except `kdeconnectd`, an XDG autostart entry; its autostart is hidden in the user's config (`~/.config/autostart/org.kde.kdeconnect.daemon.desktop`, `Hidden=true`) |
| 1.13 | `fc-list` and `xdg-open` spawns | S | **Fixed**: URLs open through GIO (E2E `native_spawns.sh`); the font catalog uses `FcFontList` in-process (E2E `font_catalog.sh`: 724 vs 731 families, no spawn, flat memory) |

## Tier 2: platform depth

| # | Gap | Owner |
|---|---|---|
| 2.1 | KMS overlay planes (`wlr_output_layer`) for video and cursor | C U |
| 2.2 | Missing protocols apps use: xdg-dialog, xdg-toplevel-drag, single-pixel-buffer, alpha-modifier (consumer code exists, no global), fifo, commit-timing, xdg-toplevel-icon, xdg-system-bell. **Done**: all eight, each with a harness check (745 to 752); the bell reaches the shell as `dsk_shell_v1.bell` (E2E `system_bell.sh`); fifo, commit-timing, and toplevel-drag are Umbriel's own, since wlroots 0.20 has no helper (`native-apis.md`) | C U |
| 2.3 | Accessibility: AT-SPI, screen zoom (compositor), screen reader path | C S: **screen zoom done**: `zoom-in`/`zoom-out`/`zoom-reset` actions (Mod+Alt+=/-) magnify the output under the pointer in 1.25x steps up to 16x, following the pointer and pushed in at the edges; the scene frame is scaled up in a second pass into its own swapchain, the cursor is drawn in software while zoomed, direct scanout is off; `umbriel zoom` reports factor and view. Harness check 760. Normal-transform outputs only. AT-SPI and the screen reader path remain |
| 2.4 | Location from `api.noctalia.dev` HTTP (privacy, network wakeups); external IP lookup on the same host | S: **Done**: auto-locate asks GeoClue over the system bus when it is present (DesktopId `dev.noctalia.Noctalia`, city accuracy; later moves are pushed, not polled) and falls back to the IP lookup only without it; auto-locate and the external IP lookup stay off by default; `location-status` IPC. E2E `location_geoclue.sh` against a stand-in GeoClue. `geoclue` itself is not installed on this machine yet |
| 2.5 | Luau plugins unsandboxed with shell-wide file and process access | S: plugins off by default; sandbox before enabling any store |
| 2.6 | Printers | S: CUPS via libcups/IPP once cupsd is installed |
| 2.7 | drm-lease (VR) | C U |

## Tier 3: every user-facing setting is in Settings

A desktop is configured from Settings; config files store what Settings saved and stay for power users. Where a
hand-written value and a Settings value meet, Settings wins, in both processes: the shell overlays `settings.toml`
on its config, and the compositor loads the file it writes after `config.toml`.

| # | Gap | Owner | Fix |
|---|---|---|---|
| 3.1 | Flat 28-section sidebar; appearance spread over Appearance, Wallpaper, Templates, Desktop, Dock, Panels, Bar, OSD, Umbriel | S | **Done**: sidebar tree of categories (Appearance, Desktop, Windows, Devices, System, Advanced), one open at a time, a one-page category is its own row; Templates into Style, OSD into Notifications, panel placements into Panels, window switcher and hot corners into Windows > Behavior; old ids (`templates`, `osd`, `umbriel`) still open the page that absorbed them. E2E `settings_navigation.sh`: all 374 settings paths kept, none on two pages |
| 3.2 | Group "tabs" are jump pills over collapsible cards | S | **Done**: real tabs, one group at a time (Plugins too); `settings_expand_all_groups` lays groups out untabbed |
| 3.3 | Override badge, "Overridden" filter, and "Reset Page" on every page; per-monitor bar copies as extra sidebar rows | S | **Done**: a reset glyph on rows Settings changed; Reset page in the ⋮ menu with an inline confirm; a display picker on the Bar page; `settings-open bar:<name>` |
| 3.4 | Compositor appearance, animation, layout, workspaces, overview, and focus are file-only | C S X | **Done**: `dsk_settings_manager_v1` replaces `dsk_input_manager_v1` over 85 keys (`managed_settings.cpp`), persisted to `settings.toml`, loaded after `config.toml` so Settings wins; Window Style, Motion > Windows, Layout, Overview, Behavior > Focus pages; Input page rows reset instead of locking. Harness check 636, E2E `compositor_settings.sh`. Umbriel hot corners stay file-only until they merge with the shell's (3.8) |
| 3.5 | Keybinds are file-only | C S X | **Done**: `bind`, `capture_chord`, and `keybind`/`action_spec` events on `dsk_settings_manager_v1`; the compositor records the chord itself so bound chords (Super+Q) can be captured; `"none"` unbinds a built-in; Devices > Keyboard Shortcuts page with add, move, remove, and reset, plus `general.mod_key`. **Built and passing**: harness check 641 |
| 3.6 | `input.toml` only applies when `config.toml` includes it, so Input page changes fail on configs without the include | C | **Done** with 3.4: `settings.toml` needs no include; a generated `input.toml` is renamed to it on first start |
| 3.7 | `settings-open <section>` does not switch pages when Settings is already open | S | **Done**: navigates on every call (E2E `settings_navigation.sh`) |
| 3.8 | Two hot-corner features: the shell's and Umbriel's `[hot_corners]` | C S | **Done** (compositor side proven by harness check 647: a corner set from settings persists, fires, and clears; the shell's one-time handover is not yet under a test): on Umbriel the compositor owns hot corners (`hot_corners.*` keys, Windows > Behavior); the shell stops detecting corners there and moves its configured ones to the compositor once. Other compositors keep the shell's |
| 3.9 | Display options zwlr_output_manager_v1 lacks: VRR fullscreen, HDR, SDR white, tearing, per-display workspaces | C S X | **Done** (harness check 646: every property reported, set, saved, applied to workspaces, refused when invalid, cleared; it still needs the `displays.toml` include in config.toml, unlike settings.toml, so a config without it cannot save them): `set_property`/`property` on `dsk_output_manager_v1`, saved to `displays.toml`; Displays page rows; an apply keeps a saved `vrr = "fullscreen"` |

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
| `scripting/script_worker_pool.h:30`, `scripting/script_io_pool.h:35` | 4+2 Luau script pools | Legitimate: function-local statics, created on the first plugin task |
| `theme/template_apply_service.h:85` | Template-apply worker | Gap 0.4 ("template worker") |
| `calendar/caldav_client.cpp`, `calendar/calendar_service.cpp` | Parse and vdir workers | Fixed (0.4): `IdleWorkerSlots`, start with work, exit after 30 s idle |
| `shell/wallpaper/panel/wallpaper_scanner.cpp` | Wallpaper directory scanner | Fixed (0.4): starts with a scan, exits after 30 s idle |
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
1. Continuity with a phone, designed in `continuity.md`, code in `link/` (`link/ARCHITECTURE.md`). **Phase 0 done**:
   protocol core, `umbriel-linkd` (sandboxed user service, `org.umbriel.Link1`), headless phone, mDNS plus
   last-known addresses, code and QR pairing with SPAKE2 bound to TLS, session resumption. E2E `link_pair.sh`.
   **Phase 1, slice A done**: live presence (10 s QUIC keep-alive and redial with backoff while the phone wants it;
   Android: in the foreground, or with "Stay connected" as a `connectedDevice` foreground service), a session actor
   per connection on both sides, text and link shares both ways (D-Bus `Share` and `Received`, shell notifications
   with Open and Copy, `link-share` IPC, Send clipboard in the Devices tab, the Android share target and
   notifications), and 4717/udp by default with a ufw profile. E2E `link_share.sh`, `link_share_shell.sh`,
   `link_android.sh`. **Slice C**: phone notifications on the desktop with actions, inline reply, and dismissal both
   ways (E2E `link_notifications.sh`, `link_android_features.sh`); media both ways, phone players as MPRIS players
   and desktop players on the phone (E2E `link_media.sh`); find my phone and find my desktop (E2E `link_ring.sh`); calls
   with mute and decline, pausing desktop media (E2E `link_calls.sh`); the app rebuilt on its own soft-tech design
   system without Material, with Home, a device page, pairing, Media, and a permission onboarding (screenshots of
   every screen in light and dark: `link_android_ui.sh`). Android 15 hides notifications it flags as sensitive (OTPs,
   some SMS) from listeners that are not trusted, so those arrive as "Sensitive notification content hidden".
   Remaining in phase 1: battery and network status, files over bulk streams (resumable, hashed),
   clipboard offers, per-feature grants and per-feature rate limits (a paired phone can flood shares today), the bar
   indicator and share sheet, Quick Share, the KDE Connect baseline, and redial on an Android network change instead
   of the next backoff step.
2. Tier 2 in the order apps need it: overlay planes (2.1), missing protocols (2.2), accessibility (2.3).
