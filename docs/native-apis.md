# Native APIs

Every API a module calls, at the level it is called. Signatures marked **verified** were read on this machine from the
installed interface (`busctl introspect`, `/usr/include`, `/usr/share/dbus-1/interfaces`, the vendored protocol XML,
or the wlroots 0.20.2 headers). Rows marked **unverified** must be checked against the installed version before code
uses them. D-Bus signatures use the standard type codes.

## Displays

Standard protocol, client side in Noctalia, server side already in Umbriel (`wlr_output_configuration_v1_build_state`
in `src/server/server_events.cpp`). Protocol XML: `protocols/wlr-output-management-unstable-v1.xml`, manager version 4.
**Verified.**

| Object | Message | Use |
|---|---|---|
| `zwlr_output_manager_v1` | event `head(new_id zwlr_output_head_v1)` | One per connector |
| | event `done(uint serial)` | End of a state batch; keep `serial` for the next configuration |
| | request `create_configuration(new_id, uint serial)` | Start a change against the last `done` serial |
| `zwlr_output_head_v1` | events `name`, `description`, `physical_size`, `mode`, `enabled`, `current_mode`, `position`, `transform`, `scale`, `make`, `model`, `serial_number`, `adaptive_sync` | Full head state. `make`/`model`/`serial_number` form the identity used for hotplug profiles |
| `zwlr_output_mode_v1` | events `size(int w, int h)`, `refresh(int mHz)`, `preferred` | Mode list |
| `zwlr_output_configuration_v1` | requests `enable_head(new_id, head)`, `disable_head(head)`, `test`, `apply`; events `succeeded`, `failed`, `cancelled` | `test` before `apply` gives a preview without committing. `cancelled` means the state changed underneath; rebuild from the new serial |
| `zwlr_output_configuration_head_v1` | requests `set_mode`, `set_custom_mode(int w, int h, int mHz)`, `set_position(int x, int y)`, `set_transform(int)`, `set_scale(fixed)`, `set_adaptive_sync(uint)` | Per-head settings |

Noctalia already binds this manager in `src/wayland/wayland_connection.cpp`, but its mode listener discards `size` and
`refresh`, and it never creates configurations. The displays page extends that binding instead of opening a second one.

Mirroring (compositor side, wlroots 0.20.2 headers, **verified**): the target output gets its own `wlr_scene_output`
positioned on the source output's layout box, so the scene renders the same content into the target's buffer. The
output state is committed through `wlr_scene_output_build_state` and `wlr_output_commit_state`, as Umbriel already
does in `src/output/output.cpp`.

## Input

Compositor side, libinput 1.32 (`/usr/include/libinput.h`, **verified**). wlroots exposes the libinput handle through
`wlr_libinput_get_device_handle(struct wlr_input_device *)` (**unverified** for 0.20).

| Protocol request | libinput call |
|---|---|
| `set_tap` | `libinput_device_config_tap_set_enabled` |
| `set_tap_drag` | `libinput_device_config_tap_set_drag_enabled` |
| `set_natural_scroll` | `libinput_device_config_scroll_set_natural_scroll_enabled` |
| `set_dwt` | `libinput_device_config_dwt_set_enabled` |
| `set_left_handed` | `libinput_device_config_left_handed_set` |
| `set_middle_emulation` | `libinput_device_config_middle_emulation_set_enabled` |
| `set_accel` | `libinput_device_config_accel_set_profile`, `libinput_device_config_accel_set_speed` |
| `set_scroll_method` | `libinput_device_config_scroll_set_method` |
| `set_click_method` | `libinput_device_config_click_set_method` |

Each setter has a matching `libinput_device_config_*_is_available` or `*_get_methods` probe; `supported` in the
protocol's `info` event is built from those probes.

Keyboards: `xkb_keymap_new_from_names(ctx, &(struct xkb_rule_names){rules, model, layout, variant, options}, 0)` then
`wlr_keyboard_set_keymap`; repeat through `wlr_keyboard_set_repeat_info(kb, rate, delay)` (**unverified** for 0.20).
The same layout is written system-wide through `locale1.SetX11Keyboard` (below) so the greeter and TTY match.

## Date and time

`org.freedesktop.timedate1` at `/org/freedesktop/timedate1`, system bus. **Verified.**

| Member | Signature | Use |
|---|---|---|
| `SetTimezone` | `(s timezone, b interactive)` | IANA zone |
| `SetNTP` | `(b use_ntp, b interactive)` | |
| `SetTime` | `(x usec_utc, b relative, b interactive)` | Manual time when NTP is off |
| `SetLocalRTC` | `(b local_rtc, b fix_system, b interactive)` | Dual boot with Windows |
| `ListTimezones` | `() → as` | Zone picker |
| `Timezone` `s`, `NTP` `b`, `LocalRTC` `b` | properties, emit `PropertiesChanged` | Live state |
| `NTPSynchronized` `b`, `TimeUSec` `t` | properties, **no change signal** | Read when the page opens |

`interactive = true` lets polkit prompt; Noctalia's polkit agent (`src/dbus/polkit/`) shows the prompt.

## Language and keyboard (system)

`org.freedesktop.locale1` at `/org/freedesktop/locale1`, system bus. **Verified.**

| Member | Signature | Use |
|---|---|---|
| `SetLocale` | `(as locale, b interactive)` | `["LANG=…", "LC_TIME=…"]`, system-wide |
| `SetX11Keyboard` | `(s layout, s model, s variant, s options, b convert, b interactive)` | Greeter/X11 layout; `convert` also sets the console keymap |
| `SetVConsoleKeyboard` | `(s keymap, s keymap_toggle, b convert, b interactive)` | TTY keymap |
| `Locale` `as`, `X11Layout` `s`, `X11Model` `s`, `X11Variant` `s`, `X11Options` `s`, `VConsoleKeymap` `s` | properties, emit `PropertiesChanged` | |

Per-user language: `org.freedesktop.Accounts.User.SetLanguage(s)` on the user's AccountsService object
(**unverified**). Noctalia already talks to AccountsService in `src/dbus/accounts/`.

## Drives

`org.freedesktop.UDisks2`, system bus. **Verified** except where marked.

| Object / interface | Member | Signature | Use |
|---|---|---|---|
| `/org/freedesktop/UDisks2`, `org.freedesktop.DBus.ObjectManager` | `GetManagedObjects`, signals `InterfacesAdded`, `InterfacesRemoved` | standard | Initial state and hotplug. The only subscription needed (**unverified**: path of the ObjectManager root) |
| `org.freedesktop.UDisks2.Block` | `HintAuto` `b`, `HintIgnore` `b`, `HintSystem` `b`, `IdLabel` `s`, `Drive` `o` | properties | Automount when `HintAuto && !HintIgnore && !HintSystem` |
| `org.freedesktop.UDisks2.Filesystem` | `Mount` | `(a{sv} options) → s mount_path` | (**unverified** on this machine: no mountable device was present) |
| | `Unmount` | `(a{sv} options)` | |
| | `MountPoints` | property `aay` | |
| `org.freedesktop.UDisks2.Drive` | `Eject` | `(a{sv} options)` | |
| | `PowerOff` | `(a{sv} options)` | Safe removal for USB disks |
| | `Removable`, `Ejectable`, `CanPowerOff`, `MediaRemovable` | properties `b` | Which buttons to show |

Encrypted volumes: `org.freedesktop.UDisks2.Encrypted.Unlock(s passphrase, a{sv} options) → o cleartext_device`
(**unverified**).

## Printers

cupsd is not installed on this machine (only `libcups 2.4.19`), so this module is built last and every row is
**unverified**.

| API | Use |
|---|---|
| `cupsEnumDests(flags, msec, cancel, type, mask, cb, user_data)` | List local and network printers (DNS-SD included) |
| IPP `CUPS-Add-Modify-Printer` with `device-uri` and `ppd-name=everywhere` via `cupsDoRequest` | Add a driverless (IPP Everywhere) printer |
| IPP `Get-Jobs`, `Cancel-Job` | Queue view |
| cupsd D-Bus notifier (`org.cups.cupsd.Notifier` signals) | Printer and job state changes without polling |

Discovery before a printer is added: `org.freedesktop.Avahi.Server.ServiceBrowserNew` for `_ipp._tcp` and `_ipps._tcp`
(Avahi is running here).

## Default apps

No D-Bus service exists; the data is a file defined by the freedesktop "Association between MIME types and
applications" spec.

| API | Use |
|---|---|
| `$XDG_CONFIG_HOME/mimeapps.list`, groups `[Default Applications]`, `[Added Associations]`, `[Removed Associations]` | Read and write defaults |
| `MimeType=` key in `.desktop` files under `$XDG_DATA_DIRS/applications` | Candidate apps per type. Noctalia's desktop-entry scanner (`src/system/`) already indexes these |
| `inotify_add_watch(fd, dir, IN_CLOSE_WRITE \| IN_MOVED_TO)` on `$XDG_CONFIG_HOME` | Reload when another program edits the file. Watch the directory, because editors replace the file by rename |

Write with a temp file plus `rename(2)` so readers never see a half-written file.

## Appearance for apps (Settings portal backend)

Noctalia serves `org.freedesktop.impl.portal.Settings` on the session bus, and `portals.conf` routes `Settings` to it.
Interface XML: `/usr/share/dbus-1/interfaces/org.freedesktop.impl.portal.Settings.xml`. **Verified.**

| Member | Signature |
|---|---|
| `ReadAll` | `(as namespaces) → a{sa{sv}}` |
| `Read` | `(s namespace, s key) → v` |
| `SettingChanged` | signal `(s namespace, s key, v value)` |
| `version` | property `u` |

Keys served under `org.freedesktop.appearance`: `color-scheme` `u` (0 no preference, 1 dark, 2 light), `accent-color`
`(ddd)` (sRGB 0 to 1), `contrast` `u`, `reduced-motion` `u`. `reduced-motion` also switches the compositor's motion to
snaps, so apps and the desktop agree.

## Efficiency (compositor)

wlroots 0.20.2 headers. **Verified** unless marked.

| API | Use |
|---|---|
| `wlr_scene_output_needs_frame(scene_output)` | Commit only when the scene has damage. Umbriel already schedules frames only while animations run (`src/output/frame_schedule.h`) |
| `wlr_output_layer_create(output)`, `wlr_output_state_set_layers(state, layers, n)` | KMS overlay planes for video and fullscreen surfaces |
| `wlr_output_layer_state.accepted` | Set by the backend after test or commit; a rejected layer falls back to composition |
| `wlr_output_layer.events.feedback` (`wlr_output_layer_feedback_event`: `target_device`, `formats`) | Tell the client which formats would fit a plane, through linux-dmabuf feedback |
| Disable all layers during capture | Required by the header: capture needs one composited buffer |
| `wlr_output_state_set_adaptive_sync_enabled(state, bool)` | VRR (KMS `VRR_ENABLED`). **Verified** on this machine: i915 rejects turning it off on the eDP panel outside a modeset (`EINVAL`), so the commit must carry the current mode as a fallback |
| `/sys/kernel/debug/dri/<n>/i915_edp_psr_status` | Read PSR state while measuring (root, debugfs) |
| `/sys/class/power_supply/AC0/online`, `BAT0/power_now` | Power source for effect policy, and power draw for measurement. Change events come from UPower, which Noctalia already watches |

Timing protocols the shell uses for content animation: `wp_presentation` (stable; the shared clock),
`wp_commit_timing_v1` and `wp_fifo_v1` (staging, present in `/usr/share/wayland-protocols/staging` here).

## Link daemon (`umbriel-linkd`)

Rust, in `link/`. **Verified** against the crate sources in `~/.cargo/registry` and a sandboxed run of
`session/umbriel-linkd.service`.

| API | Use |
|---|---|
| `org.umbriel.Link1` (served, session bus) | `StartPairing() → (s, s)`, `CancelPairing()`, `Unpair(s)`, `Share(s, s, s)` (errors `org.umbriel.Link1.Error.NotConnected`, `.Rejected`, `.Failed`), properties `Devices a(ssb)` and `Pairing b` with `PropertiesChanged`, signals `PairingFinished(s, s)`, `PairingFailed(s)`, `Received(s, s, s)`. Contract: `protocol/link-v1/org.umbriel.Link1.xml` |
| `org.freedesktop.hostname1` property `PrettyHostname` (system bus) | The name phones see; the kernel hostname (`/proc/sys/kernel/hostname`) when hostnamed is absent |
| QUIC (`quinn` 0.11) over UDP, ALPN `umbriel-link/1` | The Link transport; one dual-stack socket on 4717/udp (a random port if taken), kept in `devices.json`. The phone sets `TransportConfig::keep_alive_interval` (10 s) only while present |
| TLS 1.3 raw public keys (`rustls` 0.23 `AlwaysResolves{Server,Client}RawPublicKeys`, `verify_tls13_signature_with_raw_key`) | Both sides authenticated by Ed25519 SPKI; the phone's pin rides in the TLS server name so one client config (and its session cache) serves every desktop |
| TLS exporter (`quinn::Connection::export_keying_material`, label `EXPORTER-umbriel-link-pair`) | Binds SPAKE2 to the TLS session |
| mDNS/DNS-SD (`mdns-sd` 0.21), `_umbriel-link._udp.local.` | Advertised only while a device is paired or a pairing window is open |
| `getifaddrs` via netlink (`if-addrs` 0.15) | Addresses for the pairing QR code and the desktop's `hello` |
| `$STATE_DIRECTORY` (systemd `StateDirectory=umbriel-link`) | `identity.pk8` (0600) and `devices.json` |
| `org.umbriel.Link1` file transfers | `SendFiles(s, a(hs)) → s` (descriptors of regular files; the sandbox cannot open the user's files), `AcceptTransfer(s)`, `DeclineTransfer(s)`, `CancelTransfer(s)`, `SetAutoAccept(s, b)`, property `AutoAccept as`, signals `TransferOffered(s, s, a(st))`, `TransferProgress(s, t, t)`, `TransferFinished(s, s, as)` |
| QUIC unidirectional streams (`Connection::open_uni`/`accept_uni`, `RecvStream::read_chunk`, `RecvStream::stop`, `SendStream::reset`) | One stream per file; `stop(5)` on bytes past the announced size; a dropped unfinished stream is reset, since quinn would finish it |
| `quinn::congestion::BbrConfig` | Congestion control; Cubic falls to about 130 KB/s at 5% random loss and 50 ms RTT |
| `statvfs` (`rustix::fs::statvfs`) on the download directory | `f_bavail * f_frsize` for no-space, `f_blocks * f_frsize` for too-large |
| `open(O_CREAT \| O_EXCL)`, `open(O_NOFOLLOW)`, `pwrite`, `fdatasync`, `ftruncate`, `link`, `unlink` (std) | Part files created exclusively, written at their offset, synced every 8 MiB, cut to the durable offset on resume, published by hard link to a free name, then unlinked |
| `ring::digest::Context` (SHA-256) | Hash streamed while writing and before offering; re-computed over the partial on resume |
| `org.umbriel.Link1` clipboard | `OfferClipboard(as, h)` (a memfd with the first type's bytes), `PullClipboard(s, t, s, h) → t` (the paste target's pipe, written through `tokio::net::unix::pipe::Sender` so a slow reader never blocks the runtime), `SetGrant(s, s, b)`, property `Grants a{sas}`, signal `ClipboardOffered(s, t, as, t)` |
| `org.umbriel.Link1` status | Property `DeviceStatus a{s(ubs)}` (connected device to battery, charging, network) |
| `$XDG_DOWNLOAD_DIR`, else `XDG_DOWNLOAD_DIR` in `$XDG_CONFIG_HOME/user-dirs.dirs`, else `~/Downloads` | Where received files land; the unit adds `ReadWritePaths=-%h/Downloads` |

## Link in the shell

The control center's Devices tab and the `link-*` IPC verbs (`src/dbus/link/`, `src/shell/control_center/tabs/`).
**Verified** against `protocol/link-v1/org.umbriel.Link1.xml`, `/usr/include/qrencode.h` (qrencode 4.1.1), and a run
against `umbriel-linkd` (`tests/e2e/link_devices.sh`).

| API | Use |
|---|---|
| `org.umbriel.Link1` (session bus, client) | `Properties.GetAll` and `PropertiesChanged` for `Devices` and `Pairing`; async `StartPairing`, `CancelPairing`, `Unpair`, `Share`; signals `PairingFinished`, `PairingFailed`, `Received` (posted as an internal notification through `NotificationManager::addOrReplace`; its action copies through `ClipboardService::copyText` or opens through `net::openInBrowser`) |
| `org.umbriel.Link1` transfers (client) | Signals `TransferOffered` (a notification with Accept and Decline), `TransferProgress` (a transient notification updated with `NotificationManager::updateBody`, with Cancel), `TransferFinished` (Open and Show in folder through `net::openInBrowser` on a `g_filename_to_uri` URI); `SendFiles` with `sdbus::UnixFd` descriptors the shell opens `O_RDONLY \| O_CLOEXEC`, from `link-send-file` and the Devices tab's file dialog |
| `org.umbriel.Link1` clipboard (client) | Each new selection another client owns, once `ClipboardService` read it, goes to `OfferClipboard` in a `memfd_create` descriptor; `ClipboardOffered` becomes a data-control source (`ClipboardService::offerRemote`) whose `send` events call `PullClipboard` with the paste target's fd; the source also offers `application/x-umbriel-link-remote`, and a selection with that type is never read back. `SetGrant` and `Grants` behind the Devices tab's toggles |
| `org.umbriel.Link1` `DeviceStatus` (client) | Shown in the Devices tab and by the bar's `phone` widget (`batteryGlyphName` from the UPower client), which is in the default end widgets and hides itself while no phone is connected |
| `org.freedesktop.DBus` `NameHasOwner(s) → b`, signal `NameOwnerChanged(s, s, s)` | The tab exists only while `umbriel-linkd` owns its name; it appears and disappears with the daemon. `NameHasOwner` instead of a first `GetAll`, so the shell never D-Bus-activates the daemon |
| `QRcode_encodeString(s, 0, QR_ECLEVEL_M, QR_MODE_8, 1)`, `QRcode_free` (libqrencode) | The pairing URI as a QR symbol; bit 0 of each `data` byte is a dark module, drawn into an RGBA texture with a 4-module quiet zone |
