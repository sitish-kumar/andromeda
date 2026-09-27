# Continuity

One system that makes a phone and this desktop behave like one device: clipboard, file drop, notifications,
messages, calls, media, handoff, camera, hotspot, and presence. Built as our own protocol, our own desktop service,
and our own lightweight phone app. Nothing is bolted on: every feature surfaces through the shell's existing
surfaces (notifications, clipboard, control center, bar, settings), not a separate app window.

Working name: **Link** (`umbriel-linkd` on the desktop, "Umbriel Link" on the phone).

## Why not KDE Connect

Its reliability problems are structural, so they cannot be fixed by reimplementing it faithfully:

| Problem | Cause | Link's answer |
|---|---|---|
| Devices vanish or never appear | UDP broadcast discovery, blocked on many networks (client isolation, multicast filtering, VLANs) | mDNS plus last-known-address reconnect plus BLE presence; no broadcast |
| Phone drops off after minutes | Android kills the background service | Android `CompanionDeviceManager` association: the OS wakes the app when the desktop is in range |
| Transfers stall or fail on Wi-Fi changes | One TCP socket per payload on a negotiated port; dies on roam | QUIC: one connection, a stream per transfer, connection migration across networks |
| Pairing can be MITM'd by a careless click | Compare an 8-character code, often skipped | QR or 6-digit code through a PAKE; a wrong code fails cryptographically |
| Features feel scattered | Plugins with their own UIs and a separate app | No UI in the daemon; the shell owns every surface |
| Large attack surface | Run-command, SFTP with a password, plugins on by default | Per-device, per-feature grants, off until the user turns them on; no remote command execution |

## Architecture

```
 phone app ──QUIC/TLS 1.3──┐                         ┌── shell (all UI)
 (Android: Kotlin + core)  │   umbriel-linkd         │   notifications, clipboard, control center,
                           ├── (session service) ────┤   bar indicator, share sheet, settings page
 BLE presence ─────────────┘   D-Bus org.umbriel.Link1│
                                                     └── compositor: remote input (phone as touchpad)
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

1. **mDNS/DNS-SD**: the desktop advertises `_umbriel-link._udp` through Avahi's D-Bus API, carrying a device id
   hash, never the name. The phone uses Android's `NsdManager`.
2. **Last-known addresses**: both ends remember every address that worked and dial them directly first, so a
   network that blocks multicast still reconnects.
3. **BLE**: the desktop advertises a rotating, keyed identifier (resolvable only by paired devices, so it cannot
   be tracked); the phone's `CompanionDeviceManager` presence callback fires when it is seen. BLE carries presence
   and a "come online" nudge, never data.
4. **Transport**: QUIC with TLS 1.3, both sides authenticated by the keys pinned at pairing. One connection per
   device pair; stream 0 is control, every transfer or media flow gets its own stream, so a 4 GB file never
   blocks a notification. Connection migration keeps the session across Wi-Fi changes and Wi-Fi to hotspot.
5. **No shared network (later)**: Wi-Fi Direct (NetworkManager's Wi-Fi P2P device on Linux, `WifiP2pManager` on
   Android) for AirDrop-style transfers with nothing in between.

### Identity and pairing

- Each device has an Ed25519 long-term key. The desktop keeps its key in the Secret Service (the shell's
  `SecretStore` path); the phone keeps its key in the Android Keystore.
- **Pairing**: the desktop shows a QR code (addresses, public key, one-time secret) in Settings; the phone scans it.
  Fallback: a 6-digit code typed on the phone. Either way the secret feeds SPAKE2, so a man in the middle without
  the code learns nothing and a wrong code fails the handshake instead of relying on the user to compare strings.
- After pairing, each side pins the other's key; TLS uses those raw public keys (RFC 7250).
- Unpairing on either side revokes the key on both ends at the next contact.

### Messages

- CBOR messages on the control stream: `{type, id, body}`. Schemas live in `protocol/link-v1/` (CDDL) and
  generate the Rust types, so every message is validated against the schema before any feature code sees it.
- **Capabilities** are negotiated per connection and gated per device by the user's grants, so the phone never
  sends what the desktop did not ask for.
- **Bulk data** (files, clipboard images, attachments) opens its own stream: resumable by byte offset, a size
  announced up front and enforced, content hashed (BLAKE3) and verified before the file is revealed.

## Features

Each row lands only with an E2E proof (see Testing). "Surface" is where it appears in the shell.

| Feature | Mac equivalent | How | Surface |
|---|---|---|---|
| Presence and status | Continuity devices | Battery, signal, network of the phone | Bar indicator, control-center Devices tab |
| Clipboard | Universal Clipboard | Copy advertises "clipboard available" (type, size) to nearby devices; content moves only on paste, pulled lazily, so nothing leaves the device unless it is pasted. Expires after 2 min | `ClipboardService`: remote offers appear as a normal clipboard source |
| Send files, links, text | AirDrop | Send from the share sheet, drag onto the device in the bar, or `umbriel-link send`. The receiver accepts or declines; resumable QUIC stream; files land in Downloads with a notification that opens them | Share sheet, bar drop target, notification |
| Notifications | iPhone notifications on Mac | Phone notifications appear as native desktop notifications, with actions and inline reply; dismissing on one side dismisses on both | `NotificationManager` (app icon, grouping, DND respected) |
| Messages (Android) | Messages | SMS/RCS threads and replies through the phone's default SMS role APIs | Shell messages panel |
| Calls (Android) | Continuity calls | Incoming-call notification with decline/mute; desktop media pauses while a call is active. Audio routing to the desktop is a later phase | Notification, media widget |
| Media | — | Phone playback appears as an MPRIS player; desktop players controllable from the phone | Media widget, lock screen |
| Handoff | Handoff | "Continue on…": the focused app's current URL or file is offered to the other device, which shows a one-tap card. Browser URLs need a small extension; files and links work from the start | Bar device menu, phone card |
| Phone as webcam | Continuity Camera | Phone encodes H.264/HEVC in hardware; the daemon decodes (VA-API) and publishes a PipeWire video source, so every app sees a normal camera | PipeWire node "Phone camera" |
| Hotspot | Instant Hotspot | Android does not let apps turn tethering on. The app shares its hotspot credentials once it is on; NetworkManager joins it in one click from the network menu | Network panel |
| Find my phone | — | Ring at full volume, even on silent (needs Do Not Disturb access, granted once) | Devices tab |
| Phone as touchpad and keyboard | — | Input reaches the compositor through a Link-only request on `dsk_shell_v1`, never a generic virtual-input protocol any client could use | Devices tab |
| Unlock with phone | Apple Watch unlock | Explicit approval on the phone signs a challenge from the lock screen. Needs its own threat model (below) before it ships; never unlocks on proximity alone | Lock screen |
| Phone storage | — | Browse and pull files over a read-only Link stream; no SFTP, no passwords | File picker |

### iPhone

iOS forbids most of this for third-party apps (no SMS, no call control, no background clipboard, no background
networking). What is possible uses Apple's own BLE services, which a Linux desktop can speak without an app:
- **ANCS** (Apple Notification Center Service): read iPhone notifications and trigger their actions over BLE.
- **AMS** (Apple Media Service): control iPhone media over BLE.
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

- **Android first**, Kotlin with Jetpack Compose, the Rust core through UniFFI. No persistent foreground service:
  a `CompanionDeviceManager` association supplies the background start rights and presence events, plus
  `NotificationListenerService` for mirroring, the default share target, and a quick-settings tile.
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
| 0 | Protocol core, `umbriel-linkd`, headless phone CLI, mDNS plus last-known addresses, QR/code pairing with SPAKE2 | Pair and reconnect across a blocked-multicast namespace |
| 1 | Presence, battery, send files/links/text, clipboard, shell surfaces (bar, Devices tab, share sheet, Settings) | Resumed 1 GB transfer under loss; clipboard pull on paste |
| 2 | Android app: pairing, share target, CompanionDeviceManager presence, notification mirroring with reply | Real phone on the bench; mirrored notification with reply |
| 3 | Media, find my phone, messages, calls, hotspot credentials | E2E per feature with the headless phone |
| 4 | Handoff (files and links, then a browser extension), touchpad and keyboard | Handoff card round trip |
| 5 | Phone camera as a PipeWire source, Wi-Fi Direct transfers, iPhone (ANCS/AMS, foreground share) | Camera visible to a stock app; transfer with no shared network |
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
- Whether the phone app is distributed through F-Droid, Play, or both (affects which Android APIs are usable).

## References

Prior art, read for what to avoid as much as what to keep:
- KDE Connect protocol notes: https://github.com/KDE/kdeconnect-meta/blob/work/protocol-schemas/protocol.md
- Valent's protocol reference (a GNOME implementation of KDE Connect): https://valent.andyholmes.ca/documentation/protocol.html
- Dropbox on dropping its shared C++ mobile core: https://dropbox.tech/mobile/the-not-so-hidden-cost-of-sharing-code-between-ios-and-android
- Google on Rust in Android: https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html
- UniFFI: https://github.com/mozilla/uniffi-rs
