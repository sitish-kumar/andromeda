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
| Clipboard from the phone needs manual steps | Android 10+ lets only the focused app or the keyboard read the clipboard | Desktop to phone is automatic; phone to desktop is automatic in the sideload build (a one-time `READ_LOGS` grant over adb, as KDE Connect) and one tap through official entry points in a Play build (see Clipboard) |
| Features feel scattered | Plugins with their own UIs and a separate app | No UI in the daemon; the shell owns every surface |
| Large attack surface | Run-command, SFTP with a password, plugins on by default | Per-device, per-feature grants (only clipboard and files on after pairing); no remote command execution |

## Architecture

```
 phone app ──QUIC/TLS 1.3──┐                         ┌── shell (all UI)
 (Android: Kotlin + core)  │   umbriel-linkd         │   notifications, clipboard, control center,
 BLE presence + L2CAP ─────┼── (session service) ────┤   bar indicator, share sheet, settings page
 stock Quick Share ────────┘   D-Bus org.umbriel.Link1│
 (Android, Windows, ChromeOS)  ▲                      └── (via the shell) compositor: phone as touchpad
   via umbriel-quickshared ────┘ private socket
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

## One system, many transports

The user picks a device and an action, never a transport. Link and Quick Share are two backends of one system,
and nothing in the shell names either of them.

- **One device list.** `umbriel-linkd` owns every device: paired Link devices and nearby Quick Share endpoints.
  `umbriel-quickshared` (the C++ `google/nearby` process) reports to it over a private socket and has no D-Bus
  name of its own, so the shell sees exactly one API, `org.umbriel.Link1`. A phone that is both Link-paired and
  visible to Quick Share is one entry: while it is present over Link, a Quick Share endpoint with the same device
  name folds into it.
- **One send.** "Send to <device>" from the share sheet, the bar drop target, or `umbriel-link send` goes to the
  daemon, which picks the transport: Link when the device is paired and reachable (no visibility toggle, resumable,
  works over the phone hotspot), Quick Share otherwise. The user never sees the choice unless it fails.
- **One receive.** An incoming Link or Quick Share transfer produces the same accept/decline notification, the
  same progress, the same Downloads destination, and the same "open" action.
- **LocalSend too.** With "Visible to LocalSend" on (off by default), any LocalSend app (Android, iOS, Windows,
  macOS) can send to the desktop and receive from it without installing anything, through the same consent
  notification and Downloads; `sudo ufw allow "Umbriel Link LocalSend"` opens 53317/tcp and /udp for it.
- **Never show an action that cannot work.** Each device advertises what it can do right now, and the shell shows
  only those actions. A phone without the app shows "Send files" and nothing else; its card offers the app once
  ("Get notifications, clipboard, and more"), then stays quiet. Nothing is greyed out with a caveat.
- **Failures explain themselves in one line, with the fix as a button.** Example: a phone whose Quick Share
  visibility is "Contacts" cannot be seen by any non-Google device. The card says so and offers the one-tap
  "Everyone for 10 minutes" hint, or pairing with the app, which removes the dependency on visibility entirely.

## Protocol

### Discovery and transport

1. **mDNS/DNS-SD**: the daemon advertises `_umbriel-link._udp` with an in-process responder (`mdns-sd`), carrying
   a device id hash, never the name, and only while it has a paired device or an open pairing window. In-process rather than Avahi's D-Bus API because Avahi is often not running
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
5. **Connect on demand, present when wanted**: by default the connection closes after 30 s idle instead of
   holding keepalives, and the next one resumes the TLS session (0-RTT accepted), skipping the full handshake. No
   application data rides as early data, so nothing can be replayed. While the phone wants to be present (the app
   in the foreground, or its "Stay connected" service running) it sends QUIC keep-alives every 10 s and redials
   with backoff when the connection drops, so the desktop's device list shows the truth. When the phone is not
   connected, the desktop reaches it through the BLE presence path.
6. **BLE L2CAP control**: when no IP path works (client isolation, no shared network), control messages
   (notifications, clipboard offers, presence, "open a hotspot") travel over an L2CAP connection-oriented channel
   on the bond (BlueZ; Android API 29+), carrying the same CBOR messages, encrypted with the session keys derived
   from the pinned identities. Bulk data never uses BLE.
7. **No shared network**: for bulk data the phone opens a `LocalOnlyHotspot` (no internet, credentials generated
   by Android and readable by the app that opened it), sends its credentials over the L2CAP channel, and the
   daemon joins it through NetworkManager for the transfer, then leaves. This is Quick Share's approach, and
   more dependable than Wi-Fi Direct on Linux.
8. **Firewall**: the desktop listens on 4717/udp (unassigned at IANA) unless that port is taken, so a host firewall
   can allow it by name. The package ships a ufw profile; after installing, enable it once with
   `sudo ufw allow "Umbriel Link"` (4717/udp, and mDNS on 5353/udp).

### Identity and pairing

- Each device has an Ed25519 long-term key. The desktop keeps it in a mode-0600 file in the daemon's state directory,
  the one directory its systemd sandbox may write (the same model as SSH host keys); the Secret Service was rejected
  because it would stop the daemon until the keyring unlocks. The phone keeps its key encrypted by a non-exportable
  Android Keystore key, since TLS signing happens in the Rust core.
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
  announced up front and enforced, content hashed (SHA-256 through `ring`, the one crypto provider) and verified
  before the file is revealed.
- **Files** follow LocalSend v2's offer and accept: the sender offers names, sizes, types, and hashes; the receiver's
  user accepts (or the device is set to auto-accept); each file then streams on its own QUIC stream, four at once.
  The receiver writes a hidden part file in Downloads, syncs every 8 MiB, and publishes it under a name that never
  overwrites once the hash matches. After any reconnect, or a receiver restart, the sender asks where to resume and
  continues from the durable offset. Names are reduced to a safe basename. Details: `link/ARCHITECTURE.md` (Files).
- **Shares** of text and links are control messages, 1 to 61440 bytes, acknowledged by the receiver; a link must
  be http or https, so opening one can never run or read anything local.

## Features

Each row lands only with an E2E proof (see Testing). "Surface" is where it appears in the shell.

| Feature | Mac equivalent | How | Surface |
|---|---|---|---|
| Presence and status | Continuity devices | Battery, signal, network of the phone | Bar indicator, control-center Devices tab |
| Clipboard | Universal Clipboard | See Clipboard below | `ClipboardService`: remote offers appear as a normal clipboard source |
| Send files, links, text | AirDrop | Send from the share sheet, drag onto the device in the bar, or `umbriel-link send`. The receiver accepts or declines; resumable QUIC stream; files land in Downloads with a notification that opens them | Share sheet, bar drop target, notification |
| Quick Share, both ways | AirDrop with any Android, Windows, or ChromeOS device | Send to and receive from stock Quick Share with no app installed, over BLE, LAN, Wi-Fi Direct, or hotspot. Built on Google's own Nearby code (`google/nearby`, Apache-2.0) and its Linux platform layer, which is in open upstream PRs (BlueZ plus NetworkManager); we contribute there rather than ship a reverse-engineered clone. Runs as `umbriel-quickshared`, a backend of `umbriel-linkd` (see One system, many transports). Limit: the other device must have "Everyone" visibility, because "Contacts" and "Your devices" need Google-account certificates a third party cannot obtain; a Link-paired phone needs neither | Same send and receive surfaces as Link |
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

Automatic both ways for every device holding the clipboard grant, which a device gets at pairing and the user can
turn off per device in the Devices tab.

- **Desktop to phone, automatic.** A desktop copy goes to connected devices. Text is set on the phone clipboard at
  once (apps may write it in the background). Images and files are set as a `content://` URI served by the app, so
  the bytes move only when a phone app pastes. Cleared after 2 minutes unless the clipboard changed since.
- **Phone to desktop, automatic, in the sideload build.** Android lets only the focused app or the keyboard read the
  clipboard. As KDE Connect does, the app registers a clipboard listener, which makes Android log a denial for it on
  every copy in another app; with `READ_LOGS` (a development permission, granted once with
  `adb shell pm grant org.umbriel.link android.permission.READ_LOGS`) the app watches logcat for that line while
  "Stay connected" runs, and on each one brings up a transparent activity for a moment, which reads the clipboard
  while focused, offers it, and closes. Starting that activity from the background needs "Display over other apps"
  too, and Android 13+ asks once per start of the log reader. Without the grants the app shows them in one line. Play would reject
  `READ_LOGS` scraping, so a Play build keeps only the one-tap path.
- **Phone to desktop, one tap.** Three official entry points: the Share button on Android 13+'s copy overlay, which
  appears on every copy (Link is a share target); "Send to desktop" in the text-selection menu
  (`ACTION_PROCESS_TEXT`); and a quick-settings tile that opens the same transparent activity.
- **Lazy on the desktop.** A phone offer becomes a Wayland data source owned by the shell's `ClipboardService`;
  the content is pulled when a desktop app pastes, so a large image moves only if it is used.
- **No echoes.** Each side keeps the hash of the last clip it applied from the other and never offers it back; the
  shell also marks the selection it serves for a phone, so reading its own selection never pulls it.
- **Watch Android 17's `UniversalClipboardManager`** (`android.companion.datatransfer.continuity`, a system service
  for Google's own Handoff sync). If Google opens it to companion apps, it becomes the automatic path for Play builds
  too.

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

- **Per device, per feature grants**: presence always; clipboard and files on after pairing (the two things a user
  pairs for), notifications and everything later off until turned on. The Devices tab lists exactly what each
  device may do.
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
- **Baseline first**: before phase 1 ships, KDE Connect is measured on the same bench and the same phones:
  time to discover, reconnect success after a Wi-Fi switch and after a night asleep, transfer resume under loss,
  and phone battery per day. Each phase's proof must beat those numbers; "better than KDE Connect" is a
  measurement, not a claim.
- **Real phones, not one**: a Pixel, a Samsung, and a Xiaomi (the aggressive background killers), each on a
  current and an older Android. Phase 2 onward passes only on all of them. The headless phone proves the protocol;
  the matrix proves the lifecycle, which is where KDE Connect actually fails.
- **Bluetooth as its own milestone**, with a list of tested laptop adapters. The IP-only path must stand alone
  and be at least as good as KDE Connect; Bluetooth only adds to it.

## Phases

| Phase | Delivers | Proof |
|---|---|---|
| 0 | Protocol core, `umbriel-linkd`, headless phone CLI, mDNS plus last-known addresses, QR/code pairing with SPAKE2 bound to the TLS session, connect on demand with 0-RTT | Pair and reconnect across a blocked-multicast namespace; a relayed pairing fails |
| 1 | Presence, battery, send files/links/text, clipboard, shell surfaces (bar, Devices tab, share sheet, Settings); Quick Share both ways behind the same surfaces; KDE Connect baseline measured | Resumed 1 GB transfer under loss; clipboard pull on paste; stock-Android Quick Share send and receive through the same share sheet and notification as Link |
| 2 | Android app: pairing with the BLE bond, share target, CompanionDeviceManager presence, BLE L2CAP control, clipboard entry points, notification mirroring with reply | Real phone on the bench; mirrored notification with reply; a notification arrives over L2CAP with IP blocked |
| 3 | Media, find my phone, message replies (SMS in the non-Play build), calls | E2E per feature with the headless phone |
| 4 | Handoff (files and links, then a browser extension), touchpad and keyboard | Handoff card round trip |
| 5 | Phone camera as a PipeWire source, LocalOnlyHotspot transfers, iPhone (ANCS/AMS, foreground share) | Camera visible to a stock app; transfer with no shared network |
| 6 | Unlock with phone, after its threat model | Reviewed model plus E2E |

Phases 0-3 are the product: what people use KDE Connect for (notifications with reply, sending files,
clipboard, media, find my phone), done reliably. Phases 4-6 start only after 0-3 beat the baseline on the whole
phone matrix.

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
