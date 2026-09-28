# Link: the next stages

Decided 2026-09-28 after the first real-phone pass on the OnePlus 12. Each stage ships with an E2E that writes its
artifacts under `artifacts/`; nothing starts before the one above it is proven.

What the phone pass found:
- Home has no hierarchy: a tall hero whose only action is "Features and pairing", four equal bento tiles, and Stay
  connected hidden under the action orb.
- Files can be sent only from another app's share sheet; the system picker the quick fix opens is not wanted.
- "Apps to mirror" read as screen mirroring; it is the per-app notification filter.
- Nothing lets the desktop see the phone's files or screen.
- Link works only on a shared Wi-Fi network.

Settled choices:
- Browsing is a FUSE mount at `~/Phone/<name>`, not a panel in the shell.
- The app asks for All files access (`MANAGE_EXTERNAL_STORAGE`); it is sideloaded, so Play policy does not apply.
- Mirroring's input is an accessibility service. Phone Link on Windows injects input only through an OEM-signed
  system app (Samsung and a few partners); `INJECT_EVENTS` is signature-only, so a sideloaded app cannot. An ADB
  mode (scrcpy's route) may follow for users who keep wireless debugging on.

## 1. Home and the in-app picker (phone only)

Home, top to bottom:
1. A header row: a small connection dot, "<desktop> · Connected", and a gear that opens the desktop's page. With more
   than one desktop, tapping the name switches the primary.
2. A thin permissions banner, only while a grant is missing.
3. The Send card, the screen's one primary element: a row of the latest photos (tap to select), then Photos, Videos,
   and Files, which open the picker.
4. One row of three compact actions: Clipboard, Ring, Media.
5. A settings list: Stay connected, Notifications (state and "Choose which apps"), Add a desktop.

The action orb goes. The picker is a bottom sheet with Photos, Videos, and Files tabs over one grid of thumbnails,
multi-select, and a "Send to <desktop>" bar.
- Photos and Videos read `MediaStore` under `READ_MEDIA_IMAGES` and `READ_MEDIA_VIDEO`; Android 14's partial grant
  (selected photos) shows only what was granted, with a row to grant more.
- Files lists folders under All files access; without it the tab explains the grant and offers it.
- The file layer (`files` package: roots, listing, thumbnails, open as a descriptor) is shared with stage 4.

Proof: `link_android_ui.sh` screenshots Home and each picker tab in light and dark; `link_android.sh` selects two
fixture photos in the picker and asserts both arrive, hash-checked, in the desktop's Downloads.

## 2. Bluetooth fallback

Built 2026-09-28 as specified below (see "Bluetooth" in `link/ARCHITECTURE.md`); `link_bluetooth.sh` proves it without
radios. Not yet: a real RFCOMM run on the OnePlus, and notification icons and artwork cut down for the slower link.

The same session over RFCOMM when no IP path exists: rustls with the paired raw public keys over the RFCOMM byte
stream, then link-v1 framing unchanged. No Bluetooth bonding, since Link's pairing already authenticates both ends.
- The phone dials, as over QUIC: an insecure RFCOMM socket to Link's service UUID (`BLUETOOTH_CONNECT`). The desktop
  registers an `org.bluez.Profile1` with `RequireAuthentication=false` and takes the descriptor from `NewConnection`.
- One byte stream, so bulk data is multiplexed on it: a stream id per chunk, 16 KiB chunks, control frames first.
- Carried: shares, clipboard text, notifications and replies (icons cut to 64 px), calls, ring, media controls
  (artwork 128 px), status, files up to 20 MiB. A larger offer asks for the hotspot (stage 3).
- Path order: LAN QUIC, then the hotspot, then Bluetooth. While on Bluetooth the phone keeps probing for an IP path
  and moves the session up when one answers.

Failure modes to prove:
1. The Bluetooth adapter is off or absent on either side: the device shows disconnected, no error loop.
2. A peer presenting an unpaired key over RFCOMM: the TLS handshake fails, nothing is delivered.
3. Wi-Fi returns mid-session: the session moves to QUIC with no lost or duplicated share.
4. RFCOMM drops mid-file: the transfer resumes by offset on the next path, as over QUIC.
5. A file over 20 MiB offered on Bluetooth: refused with the hotspot hint, not trickled.

Proof: the headless phone (`umbriel-link-phone`) over a BlueZ virtual controller pair (`btvirt`), all IP routes
removed; artifacts are the transcript and per-feature results.

## 3. Hotspot handoff

Built 2026-09-28 (see "Hotspot" in `link/ARCHITECTURE.md`); `link_hotspot.sh` proves it with a NetworkManager stand-in.
Not yet: a real run with the OnePlus and the laptop's NetworkManager, and a shell notice while the laptop is on the
phone's hotspot (the `Hotspot` signal is there for it).

When a transfer, browsing, or mirroring needs full speed and there is no shared Wi-Fi, the phone starts a
`LocalOnlyHotspot` (no tethering, no data use; `NEARBY_WIFI_DEVICES`) and sends its SSID and passphrase over the
Bluetooth session. The desktop joins it through NetworkManager as a temporary, non-autoconnect profile, the session
moves to QUIC over it, and the desktop returns to its previous network when the work ends. The laptop leaves its
current Wi-Fi while joined; the shell says so before joining.

Proof: on the emulator is not possible; a scripted real-phone run records join time, throughput of a 1 GiB file, and
the restore of the previous connection.

## 4. Browsing the phone's files

Built 2026-09-28 (see "Browsing" in `link/ARCHITECTURE.md`); `link_browse.sh` proves it through a real FUSE mount.
Thumbnails (`fs-thumb`) and writes are not built: file managers read the file for their own thumbnails.

Protocol, read-only first:
```
desktop                                            phone
  fs-list {path, cursor?}                      ->
                                               <-  fs-entries {entries: [{name, kind, size, mtime, mime}], cursor?}
  fs-stat {path}                               ->
                                               <-  fs-entry {...} | fs-error {reason}
  fs-read {path, offset, len}                  ->  bytes on a unidirectional stream
  fs-thumb {path, px}                          ->  a JPEG on a unidirectional stream
```
- Roots: `Photos` (a MediaStore view: Camera, Screenshots, the other albums) and `Storage` (shared storage, only
  with All files access). Paths are relative to a root; `..`, absolute paths, and symlinks out of a root are refused.
- The phone answers only while a new grant, "Browse files", is on for that desktop; it is off after pairing.
- Desktop: `umbriel-link-mount`, a user service outside `umbriel-linkd`'s sandbox (which has `PrivateDevices` and a
  private mount namespace, so its mounts would be invisible). It mounts `~/Phone/<name>` read-only with the `fuser`
  crate while the phone is connected and talks to the daemon over D-Bus. Listings cache 5 s; reads read ahead 1 MiB.
  The Devices tab gets "Browse files", which opens the mount in the file manager.
- Writes (copy to the phone, delete) come after reads are proven.

Proof: the headless phone serves a fixture tree; the E2E lists, stats, reads ranges, and checksums every file through
the mount, then checks the mount is gone after disconnect and that `../` escapes fail.

## 5. Mirroring the phone's screen

**Built 2026-09-28**: desktop side proven headless (`link_mirror.sh`); the Android capture, encoder, and input service
wait for the real-phone pass. Glass-to-glass latency is measured there.

- Phone: `MediaProjection` (a `mediaProjection` foreground service; Android asks every session, which no app can
  skip) into a hardware H.264 `MediaCodec` surface, at most 1080p and 60 fps, low-latency and realtime priority,
  bitrate stepped down under loss, a keyframe on request. One unidirectional stream of length-prefixed access units
  with timestamps.
- Control: an accessibility service replays taps, swipes, long presses, and scrolls (`dispatchGesture`) and Back,
  Home, and Recents (`performGlobalAction`); text goes to the focused field (`ACTION_SET_TEXT`) or through the
  clipboard. No cursor and no arbitrary key events.
- Desktop: `umbriel-link-mirror`, a Rust window on gtk4 and GStreamer (`h264parse ! vah264dec`, VA-API on the Arc
  140T) fed by a descriptor from the daemon. Started from the Devices tab ("Mirror screen"), which makes the phone
  show the capture prompt.
- A new grant, "Screen", off after pairing. Audio (`AudioPlaybackCapture`, Opus) comes later.

Proof: the emulator mirrors the fixture app; the E2E measures glass-to-glass latency with a timestamp the fixture
draws, taps a fixture button through the viewer, and saves frames and the latency histogram.

## 6. The internet path

**Done 2026-09-28** for overlay networks: Tailscale addresses are announced and kept like LAN ones (`link_tailnet.sh`).

First, addresses on a Tailscale (or any routed) interface are advertised and dialled like LAN ones, so two devices on
one tailnet connect from anywhere. A relay of our own (hole punching, then a relayed QUIC path) only if that proves
insufficient.

## 7. Phone apps as desktop windows (lowest priority)

**Built 2026-09-28**: `umbriel-link-apps` over wireless debugging and scrcpy; `link_apps.sh` needs scrcpy and the
emulator.

DeX-like: each phone app in its own Umbriel window. Only the ADB route can do it: the `shell` user may create a
virtual display and launch activities on it (scrcpy's `--new-display` and `--start-app`), which a sideloaded app may
not. Needs wireless debugging on; gives full keyboard and mouse input. Starts after stage 5 is proven.
