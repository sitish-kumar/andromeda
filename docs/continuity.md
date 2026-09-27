# Continuity

One system that makes a phone and this desktop behave like one device: clipboard, file drop, notifications,
messages, calls, media, handoff, camera, hotspot, and presence. Built as our own protocol, our own desktop service,
and our own lightweight phone app. Nothing is bolted on: every feature surfaces through the shell's existing
surfaces (notifications, clipboard, control center, bar, settings), not a separate app window.

Working name: **Link** (`umbriel-linkd` on the desktop, "Umbriel Link" on the phone).

## Why not KDE Connect

Its reliability problems are structural, so they cannot be fixed by reimplementing it faithfully. (KDE Connect
has since added mDNS next to broadcast, and connecting by IP; the rows below are what remains.)

| Problem | Cause | Link's answer |
|---|---|---|
| Devices vanish or never appear | Discovery and data both need a shared IP network that passes traffic between clients; client isolation, multicast filtering, and VLANs break it | mDNS plus last-known-address reconnect for normal networks; when IP between the devices is blocked, control messages move to BLE L2CAP and bulk data to a phone LocalOnlyHotspot |
| Phone drops off, or a permanent notification drains the battery | An always-on foreground service with keepalives, or Android killing it | `CompanionDeviceManager` presence starts the app's foreground service only while the desktop is in range; connections close when idle and reopen on demand with 0-RTT |
| Transfers stall or fail on Wi-Fi changes | One TCP socket per payload on a negotiated port; restarts from zero | QUIC: one connection, a stream per transfer, resumable by byte offset after a reconnect |
| Pairing can be MITM'd by a careless click | Compare a short verification key, often skipped | QR or 6-digit code through a PAKE bound to the TLS session; a wrong code fails cryptographically |
| Clipboard from the phone needs manual steps | Android 10+ lets only the focused app or the keyboard read the clipboard | Desktop to phone is automatic; phone to desktop is one tap through official entry points, with no log-reading or privileged-shell workarounds (see Clipboard) |
| Features feel scattered | Plugins with their own UIs and a separate app | No UI in the daemon; the shell owns every surface |
| Large attack surface | Run-command, SFTP with a password, plugins on by default | Per-device, per-feature grants, off until the user turns them on; no remote command execution |

## Architecture

```
 phone app ──QUIC/TLS 1.3──┐                         ┌── shell (all UI)
 (Android: Kotlin + core)  │   umbriel-linkd         │   notifications, clipboard, control center,
                           ├── (session service) ────┤   bar indicator, share sheet, settings page
 BLE presence + L2CAP ─────┘   D-Bus org.umbriel.Link1│
                                                     └── (via the shell) compositor: phone as touchpad
```

- **`umbriel-linkd` is a fourth process**, by the same reasoning as the three in `standards.md`: it parses
  untrusted network input and holds long-term keys, so it stays out of the shell's and compositor's address space.
  User service under `umbriel-session.target`, systemd-sandboxed (no DRM, no home write outside its state dir and
  the download dir, `RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX AF_BLUETOOTH`).
- **One protocol core, shared by desktop and phone.** Written once, in Rust: QUIC (`quinn`), TLS 1.3 (`rustls`),
  PAKE, framing, and the state machine. The daemon links it natively; the Android app calls it through UniFFI.
  Rust because the core parses untrusted input on both ends, and one implementation means the two ends cannot drift.
  The daemon's D-Bus side uses `zbus`.
- **The shell draws everything.** The daemon has no UI. The shell consumes `org.umbriel.Link1` the way it consumes
  UPower or NetworkManager today, and maps each feature onto a surface it already has.
- **The protocol lives in `protocol/link-v1/`**, the only copy, next to `desktop-unstable-v1.xml`.

## Protocol

### Discovery and transport

1. **mDNS/DNS-SD**: the daemon advertises `_umbriel-link._udp` with an in-process responder (`mdns-sd`), carrying
   a device id hash, never the name. In-process rather than Avahi's D-Bus API because Avahi is often not running
   (it is not on the reference machine) and the network-namespace E2E then needs no system daemon; `mdns-sd`
   shares port 5353 with Avahi when both run. The phone uses Android's `NsdManager`.
2. **Last-known addresses**: both ends remember every address that worked and dial them directly first, so a
   network that blocks multicast still reconnects.
3. **BLE**: pairing also creates a BlueZ bond, so the desktop advertises with a Resolvable Private Address that
   only bonded phones can resolve (no tracking, no custom rotation scheme). `CompanionDeviceManager` matches
   devices by Bluetooth address, which is why the bond is needed: its presence callback then fires when the
   desktop is in range and grants the app the right to start its foreground service.
4. **Transport**: QUIC with TLS 1.3, both sides authenticated by the keys pinned at pairing. One connection per
   device pair; stream 0 is control, every transfer or media flow gets its own stream, so a 4 GB file never
   blocks a notification. Only the QUIC client (the phone) migrates: a phone roaming between networks keeps the
   connection, but a desktop address change ends it. What makes transfers survive either case is resumption by
   byte offset after a fast reconnect.
5. **Connect on demand**: the connection closes after a short idle period instead of holding keepalives, and
   reopens with 0-RTT session resumption when either side has something to send. Only idempotent messages
   (presence, clipboard offers, notification sync by id) may ride in 0-RTT, since 0-RTT data can be replayed.
   When the phone is not connected, the desktop reaches it through the BLE presence path.
6. **BLE L2CAP control**: when no IP path works (client isolation, no shared network), control messages
   (notifications, clipboard offers, presence, "open a hotspot") travel over an L2CAP connection-oriented channel
   on the bond (BlueZ; Android API 29+), carrying the same CBOR messages, encrypted with the session keys derived
   from the pinned identities. Bulk data never uses BLE.
7. **No shared network**: for bulk data the phone opens a `LocalOnlyHotspot` (no internet, credentials generated
   by Android and readable by the app that opened it), sends its credentials over the L2CAP channel, and the
   daemon joins it through NetworkManager for the transfer, then leaves. This is Quick Share's approach, and
   more dependable than Wi-Fi Direct on Linux.

### Identity and pairing

- Each device has an Ed25519 long-term key. The desktop keeps its key in the Secret Service (the shell's
  `SecretStore` path); the phone keeps its key in the Android Keystore.
- **Pairing**: the desktop shows a QR code (addresses, public key, one-time secret) in Settings; the phone scans it.
  Fallback: a 6-digit code typed on the phone. Either way the secret feeds SPAKE2, so a man in the middle without
  the code learns nothing and a wrong code fails the handshake instead of relying on the user to compare strings.
  A code allows one attempt and then expires.
- **Channel binding**: during pairing TLS cannot pin a key yet, so SPAKE2 runs over the TLS session and is bound
  to it: the SPAKE2 identity is the TLS exporter value (`EXPORTER-umbriel-link-pair`) plus both TLS public keys.
  Each side then sends a confirmation MAC (keyed by the SPAKE2 output, over role and transcript) and pins the
  peer's key only after verifying the other's MAC. An attacker terminating TLS in the middle sees different
  exporters on the two legs, so the MACs fail even if the code leaks later.
- After pairing, each side pins the other's key; TLS uses those raw public keys (RFC 7250).
- Pairing ends with the BLE bond (above) when both sides have Bluetooth; a device without it still pairs, and
  loses only background presence and the L2CAP path.
- Unpairing on either side revokes the key on both ends at the next contact.

### Messages

- CBOR messages on the control stream: `{type, id, body}`. The schema lives in `protocol/link-v1/` (CDDL). The
  Rust types are hand-written serde structs that reject unknown fields, so a malformed message fails decoding
  before any feature code sees it; the E2E tests validate every transcribed message against the CDDL (`cddl`
  crate), which keeps the two from drifting. (No mature CDDL-to-Rust generator exists.)
- The first control message carries the protocol version; an unknown major version closes the connection.
- **Capabilities** are negotiated per connection and gated per device by the user's grants, so the phone never
  sends what the desktop did not ask for.
- **Bulk data** (files, clipboard images, attachments) opens its own stream: resumable by byte offset, a size
  announced up front and enforced, content hashed (BLAKE3) and verified before the file is revealed.

## Features

Each row lands only with an E2E proof (see Testing). "Surface" is where it appears in the shell.

| Feature | Mac equivalent | How | Surface |
|---|---|---|---|
| Presence and status | Continuity devices | Battery, signal, network of the phone | Bar indicator, control-center Devices tab |
| Clipboard | Universal Clipboard | See Clipboard below | `ClipboardService`: remote offers appear as a normal clipboard source |
| Send files, links, text | AirDrop | Send from the share sheet, drag onto the device in the bar, or `umbriel-link send`. The receiver accepts or declines; resumable QUIC stream; files land in Downloads with a notification that opens them | Share sheet, bar drop target, notification |
| Receive from stock Quick Share | AirDrop from any Android | Any Android phone sends files with no app installed. Built on Google's own Nearby code (`google/nearby`, Apache-2.0) and its Linux platform layer, which is in open upstream PRs (BlueZ plus NetworkManager); we contribute there rather than ship a reverse-engineered clone. A separate optional process, since the library is C++. Limit: "Everyone" visibility only, because "Contacts" and "Your devices" need Google-account certificates a third party cannot obtain | Notification, Downloads |
| Notifications | iPhone notifications on Mac | Phone notifications appear as native desktop notifications, with actions and inline reply; dismissing on one side dismisses on both | `NotificationManager` (app icon, grouping, DND respected) |
| Messages (Android) | Messages | Replies through each notification's `RemoteInput`, which works for every messaging app, RCS included. RCS threads are not readable by any third-party app (Google keeps its API on an allowlist). SMS history and new-message compose need `READ_SMS`/`SEND_SMS`, which Play grants only to the default SMS app, so they exist only in a non-Play build | Notifications; messages panel (non-Play build) |
| Calls (Android) | Continuity calls | Incoming-call notification with decline/mute; desktop media pauses while a call is active. Audio routing to the desktop is a later phase | Notification, media widget |
| Media | — | Phone playback appears as an MPRIS player; desktop players controllable from the phone | Media widget, lock screen |
| Handoff | Handoff | "Continue on…": the focused app's current URL or file is offered to the other device, which shows a one-tap card. Browser URLs need a small extension; files and links work from the start. Android 17's "Continue On" is Android-to-Android through Play services and a shared Google account, so it cannot carry a Linux peer | Bar device menu, phone card |
| Phone as webcam | Continuity Camera | Phone encodes H.264/HEVC in hardware; the daemon decodes (VA-API) and publishes a PipeWire video source, so every app sees a normal camera | PipeWire node "Phone camera" |
| Find my phone | — | Ring at full volume, even on silent (needs Do Not Disturb access, granted once) | Devices tab |
| Phone as touchpad and keyboard | — | The daemon is not a Wayland client: input goes daemon → D-Bus → shell → a Link-only request on the shell's `dsk_shell_v1`, never a generic virtual-input protocol any client could use | Devices tab |
| Unlock with phone | Apple Watch unlock | Explicit approval on the phone signs a challenge from the lock screen. Needs its own threat model (below) before it ships; never unlocks on proximity alone | Lock screen |
| Phone storage | — | Browse and pull files over a read-only Link stream; no SFTP, no passwords | File picker |

Tethering has no row: Android gives apps neither a way to turn it on nor its password (`getSoftApConfiguration`
is system-only). NetworkManager remembers the phone's hotspot after the user joins it once.

### Clipboard

Everything here uses documented Android APIs. Automatic background reading of the phone clipboard is possible only
for the default keyboard or the focused app, and we do not work around that (no `READ_LOGS` log scraping, no
Shizuku, no accessibility-service tricks): they break across Android releases and would be rejected by Play.

- **Desktop to phone, automatic.** A desktop copy goes to present devices that hold the clipboard grant. Text is
  set on the phone clipboard at once (apps may write it in the background). Images and files are set as a
  `content://` URI served by the app, so the bytes move only when a phone app pastes. Cleared after 2 minutes if
  unchanged.
- **Phone to desktop, one tap.** Three official entry points: the Share button on Android 13+'s copy overlay,
  which appears on every copy (Link is a share target); "Send to <desktop>" in the text-selection menu
  (`ACTION_PROCESS_TEXT`); and a quick-settings tile or a presence-notification action that opens a transparent
  activity, reads the clipboard while focused, sends it, and closes.
- **Lazy on the desktop.** A phone offer becomes a Wayland data source owned by the shell's `ClipboardService`;
  the content is pulled when a desktop app pastes, so a large image moves only if it is used.
- **Watch Android 17's `UniversalClipboardManager`** (`android.companion.datatransfer.continuity`, a system service
  for Google's own Handoff sync). If Google opens it to companion apps, it becomes the automatic path; until then,
  one tap is the best any third-party app can do.

### iPhone

iOS forbids most of this for third-party apps (no SMS, no call control, no background clipboard, no background
networking). What is possible uses Apple's own services, which a Linux desktop can speak without an app:
- **ANCS** (Apple Notification Center Service): read iPhone notifications and trigger their actions over BLE,
  worldwide.
- **AMS** (Apple Media Service): control iPhone media over BLE.
- **EU notification forwarding** (iOS 26.3+, `AccessoryNotifications` framework, under the DMA): richer forwarding
  to one third-party device at a time, which then disables it for an Apple Watch. EU only; evaluate against ANCS
  when that phase starts.
- An iOS app can handle share-sheet file sends and clipboard while it is in the foreground.

So: Android gets the full feature set, and iPhone gets notifications, media, and foreground sharing.

## Security model

- **Per device, per feature grants**, all off after pairing except presence. The Settings page lists exactly
  what each device may do.
- **Nothing executes.** No remote commands; received files are never opened automatically; receiving from an
  unpaired device is impossible by design.
- **Limits enforced by the daemon**: message size, stream count, transfer size (announced and verified), and
  rate per feature, so a compromised phone cannot flood the desktop.
- **Unlock with phone** (later) needs: challenge bound to the lock-screen session, approval with the phone's
  biometric, a short TTL, a distance bound (BLE RSSI as a hint only, never the decision), and an audit log. It
  stays off until that model is reviewed.

## Phone app

- **Android first**, Kotlin with Jetpack Compose, the Rust core through UniFFI.
- **Background, by documented means only.** A plain BLE `CompanionDeviceManager` association (no device profile)
  with `REQUEST_OBSERVE_COMPANION_DEVICE_PRESENCE` and `REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND`,
  both normal permissions. The OS starts the app's `connectedDevice` foreground service when the desktop comes into
  range and stops it when it leaves, so its notification exists only while the desktop is nearby. The `COMPUTER`
  profile (one prompt for notification and media access) and self-managed associations are `signature|privileged`,
  reserved for preinstalled apps like Microsoft's Link to Windows, so notification access is its own settings grant.
  Without Bluetooth, the service runs only while the user keeps "Stay connected" on.
- `NotificationListenerService` for mirroring, the default share target, the text-selection action, and a
  quick-settings tile.
- **No-shared-network transfers**: `LocalOnlyHotspot` drops the phone's own Wi-Fi on phones without STA/AP
  concurrency; the app checks `isStaApConcurrencySupported()` and tells the user before a transfer.
- Small by design: pairing, a device page, a share target, a list of granted features. The phone UI never
  duplicates desktop UI.
- **iOS** after Android, limited to what iOS allows (above).

## Testing

In the repo's existing style: E2E only, each producing an artifact.
- **A headless phone**: a CLI built on the same Rust core stands in for the phone, so every feature runs end to
  end in CI against a real `umbriel-linkd` and a headless Umbriel with the shell: pair, send, clipboard pull,
  notification with reply, media, reconnect.
- **Network faults**: run the pair across network namespaces with `tc netem` loss and delay, multicast blocked,
  and an address change mid-transfer; the transfer must resume and the hash must match.
- **Artifacts**: transcripts of every message (schema-validated), screenshots of each shell surface, transfer
  hashes.

## Phases

| Phase | Delivers | Proof |
|---|---|---|
| 0 | Protocol core, `umbriel-linkd`, headless phone CLI, mDNS plus last-known addresses, QR/code pairing with SPAKE2 bound to the TLS session, connect on demand with 0-RTT | Pair and reconnect across a blocked-multicast namespace; a relayed pairing fails |
| 1 | Presence, battery, send files/links/text, clipboard, shell surfaces (bar, Devices tab, share sheet, Settings); receive from stock Quick Share | Resumed 1 GB transfer under loss; clipboard pull on paste; a stock-Android Quick Share send lands |
| 2 | Android app: pairing with the BLE bond, share target, CompanionDeviceManager presence, BLE L2CAP control, clipboard entry points, notification mirroring with reply | Real phone on the bench; mirrored notification with reply; a notification arrives over L2CAP with IP blocked |
| 3 | Media, find my phone, message replies (SMS in the non-Play build), calls | E2E per feature with the headless phone |
| 4 | Handoff (files and links, then a browser extension), touchpad and keyboard | Handoff card round trip |
| 5 | Phone camera as a PipeWire source, LocalOnlyHotspot transfers, iPhone (ANCS/AMS, foreground share) | Camera visible to a stock app; transfer with no shared network |
| 6 | Unlock with phone, after its threat model | Reviewed model plus E2E |

## Decided: Rust core, native shells

The core and `umbriel-linkd` are Rust; the phone UI is Kotlin (Compose) on Android and SwiftUI on iOS, bound by
UniFFI. A C++ core was rejected: its bindings would be hand-written JNI or Djinni, the approach Dropbox built and then
abandoned as costlier than writing the code twice; and this core parses untrusted input and holds keys, where
Google measures Rust at roughly 1000x fewer memory-safety bugs per line than C/C++. No C++ is shared either way,
since the daemon talks to the shell over D-Bus. Flutter over the same core stays possible if the phone UI grows.
Costs accepted: a new language in the repo, a larger app binary, `cargo-ndk` in the Android build. QUIC loss
behaviour (where `msquic` benchmarks ahead of `quinn`) is measured in the `tc netem` E2E tests rather than assumed.

## Open questions

- Name and branding (Link is a placeholder).
- Whether the phone app is distributed through F-Droid, Play, or both. Play forbids `READ_SMS`/`SEND_SMS` for an
  app that is not the default SMS app, so SMS history and compose exist only outside Play; everything else is
  identical in both builds.

## References

Prior art, read for what to avoid as much as what to keep:
- KDE Connect protocol notes: https://github.com/KDE/kdeconnect-meta/blob/work/protocol-schemas/protocol.md
- Valent's protocol reference (a GNOME implementation of KDE Connect): https://valent.andyholmes.ca/documentation/protocol.html
- Dropbox on dropping its shared C++ mobile core: https://dropbox.tech/mobile/the-not-so-hidden-cost-of-sharing-code-between-ios-and-android
- Google on Rust in Android: https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html
- UniFFI: https://github.com/mozilla/uniffi-rs
- Google Nearby (official Quick Share and Nearby Connections code, Apache-2.0): https://github.com/google/nearby,
  Linux platform PRs https://github.com/google/nearby/pull/2098 and https://github.com/google/nearby/pull/4106
- Android companion device pairing and background presence: https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing
- Companion device profiles (why `COMPUTER` is privileged): https://source.android.com/docs/core/connect/companion-device-profile
- Play policy on SMS and Call Log permissions: https://support.google.com/googleplay/android-developer/answer/10208820
- Wi-Fi STA/AP concurrency (LocalOnlyHotspot): https://source.android.com/docs/core/connect/wifi-sta-ap-concurrency
- Android 17 Continue On: https://developer.android.com/develop/better-together/continue-on
- iOS notification forwarding (EU): https://developer.apple.com/documentation/accessorynotifications/notificationsforwarding
