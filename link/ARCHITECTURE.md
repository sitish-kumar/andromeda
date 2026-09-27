# Link: architecture

The code behind `docs/continuity.md`. That file says what Link does and why; this one says how the code is cut.
Conventions are in `CONVENTIONS.md`.

## Crates

```
link/crates/
  link-proto   sans-IO: wire messages, framing, pairing and session state machines. No I/O, no clock, no runtime.
  link-core    the engine: identity and store, TLS/QUIC transport, mDNS, the session actor, and the phone's client
               actor (presence, redial, events). Used by all three below.
  link-daemon  bin umbriel-linkd: D-Bus org.umbriel.Link1, systemd service, pairing window, device registry.
  link-phone   bin umbriel-link-phone: the headless phone for E2E tests, and the transcript schema check.
  link-ffi     UniFFI bindings of link-core for the Android app.
  link-quickshare  Quick Share over the LAN (UKEY2, the D2D channel, sharing frames, mDNS and the BLE hint); bin
               umbriel-quickshare for tests. Depends on no other crate of ours.
link/android/  the Android app, Kotlin + Compose over link-ffi.
```

Dependency direction is strictly downward: `daemon`, `phone`, `ffi` → `core` → `proto`. `proto` depends on no other
crate of ours; `core` never knows which binary runs it.

## Dependencies

| Crate | Used by | Why |
|---|---|---|
| `quinn` | core | QUIC: streams, 0-RTT, client migration. The protocol's transport |
| `rustls` (ring) | core | TLS 1.3 inside QUIC; RFC 7250 raw public keys |
| `ring` | proto, core | Ed25519 keys, SHA-256, HKDF, HMAC; the only crypto provider |
| `spake2` | proto | The PAKE for code and QR pairing (RustCrypto) |
| `ciborium`, `serde`, `serde_bytes` | proto | CBOR wire encoding |
| `mdns-sd` | core | mDNS responder and browser; no Avahi dependency (see continuity.md) |
| `tokio` | core, daemon, phone | Runtime, current-thread only |
| `zbus` | daemon | D-Bus service on the tokio runtime, no extra thread |
| `serde_json` | core | The on-disk device store |
| `thiserror` | proto, core | Library error enums |
| `anyhow`, `clap`, `env_logger`, `log` | binaries (`log` everywhere) | CLI and logging |
| `cddl` | phone | Validates E2E transcripts against `protocol/link-v1/messages.cddl` |
| `prost`, `prost-types`, `prost-build` | quickshare | Quick Share frames are protobuf; generated from `protocol/quickshare/` at build time (needs `protoc`) |
| `aes`, `cbc` | quickshare | AES-256-CBC with PKCS#7 for Quick Share's D2D channel; ring has no CBC (see CONVENTIONS) |

## Threads

`umbriel-linkd` runs one tokio current-thread runtime (QUIC, D-Bus, timers, the store) plus `mdns-sd`'s responder
thread while it advertises, and a second one while Quick Share is visible. Nothing else.

## Wire format (link-v1)

- QUIC, ALPN `umbriel-link/1`, TLS 1.3 with raw public keys (Ed25519 SPKI) on both sides.
- The phone is always the QUIC client, the desktop the server. The phone's first bidirectional stream is the control
  stream; later streams carry bulk data (phase 1).
- A frame is a big-endian `u32` length then one CBOR envelope `{type: tstr, id: uint, body: map}`; control frames are
  at most 64 KiB. Schema: `protocol/link-v1/messages.cddl`.
- Close codes (QUIC application error): 0 done, 1 not-paired, 2 unpaired, 3 unsupported-version, 4 pairing-failed,
  5 protocol-error, 6 busy.

### Identity

- Each device has one Ed25519 key. `DeviceId` = the first 16 bytes of SHA-256 over its SPKI, lowercase hex.
- The desktop stores `identity.pk8` (mode 0600) and `devices.json` in `$XDG_STATE_HOME/umbriel-link/`; the systemd
  sandbox makes that directory writable and nothing else in home but Downloads.

### Pairing

A pairing window lasts 120 s and accepts exactly one attempt. It carries two secrets: a 6-digit code (typed) and a
128-bit secret in the QR URI `umbriel-link:pair?v=1&k=<sha256(spki), base64url>&c=<secret>&a=<host:port>...`.

```
phone (client)                                   desktop (server)
  TLS handshake (QR: pin k; code: accept any)  ->  accept any key while the window is open
  hello {version, name}                        ->
                                               <-  hello {version, name, addresses}
  pair-spake {method: "code"|"qr", msg}        ->
                                               <-  pair-spake {msg}
  pair-confirm {mac: HMAC(Kc, transcript)}     ->  verify, constant time; on failure close 4 and burn the window
                                               <-  pair-confirm {mac: HMAC(Ks, transcript)}   (store phone key)
  verify; store desktop key and addresses
```

- SPAKE2 is the symmetric Ed25519 variant; its identity is `"umbriel-link-pair-v1" || exporter || spki_server ||
  spki_client`, where `exporter` is 32 bytes of the TLS exporter labelled `EXPORTER-umbriel-link-pair`.
- `Kc`, `Ks` = HKDF-SHA256 over the SPAKE2 key with info `"confirm client"` / `"confirm server"`; the transcript is
  the SPAKE2 identity.

Failure modes, each of which must end in no stored key on either side:

1. Wrong code or secret: the MACs differ; the desktop closes with 4 and the window closes.
2. A relay that terminates TLS on both legs: the exporters differ, so the MACs differ, even with the right code.
3. Replayed pairing messages from an earlier run: SPAKE2 is randomised per run, so the MACs differ.
4. Messages out of order (confirm before spake, a second spake, a hello after pairing began): close 5.
5. Oversized or malformed frame: close 5, no panic, no allocation beyond the 64 KiB cap.
6. An attempt with the window closed: close 1, no state change.
7. A second attempt while one is running: close 6.
8. A peer that stalls: each step has a 10 s deadline; the window closes.
9. The QR path pins `k`; a server presenting another key fails the handshake before any SPAKE2 message.
10. A version the other side does not speak: close 3.

### Session

- A connection whose client key is paired is a session: both sides exchange `hello`; the desktop updates the device's
  name and last-seen time; the phone updates the desktop's name and addresses.
- A key the desktop unpaired is answered with close 2, and the phone forgets the desktop. The phone unpairing sends
  `unpair` and then forgets; the desktop forgets on receipt.
- Connect on demand: the connection idles out after 30 s; the phone keeps its TLS session ticket and the next
  connection resumes it (0-RTT accepted), skipping the full handshake. No application data is sent as early data, so
  nothing is replayable. rustls reuses a cached session only with the same verifier and key-resolver objects, so the
  phone builds one client config and carries the desktop pin in the TLS server name
  (`<fingerprint[0..16]>.<fingerprint[16..32]>.link` in hex, `pairing.link` for code pairing).
- Presence: while the phone wants to be present it dials with QUIC keep-alive every 10 s. That is a third of the
  30 s idle timeout, so two lost PINGs in a row still keep the session, and it stays under the 30 s after which Linux
  conntrack and many home routers drop an idle UDP mapping. When a present session ends for any reason but
  unpairing, the phone redials (last-known addresses, then mDNS) after 1 s, doubling to at most 30 s, and resets the
  delay on success. Only the phone sends PINGs; the desktop never dials.
- The desktop keeps one session per device: a new session from a device closes the older one with 0, because a phone
  whose network changed reconnects before the desktop's old connection idles out.
- Each side runs one session actor per connection. It owns the control stream, reads it without a deadline, and is
  reached through a handle for sending; the desktop's actors are the listener's connection tasks.

### Session messages

```
either side                                    other side
  share {kind: "text"|"link", text}        ->  hand to the local surface (D-Bus Received, the phone's events)
                                           <-  share-ack {of: <the share's envelope id>}
phone                                          desktop
  unpair {}                                ->  forget the phone, close 0
```

- `text` is 1 to 61440 bytes of UTF-8, so a share always fits one 64 KiB frame. A `link` is also an absolute
  `http://` or `https://` URL (scheme in any case) with a non-empty rest and no whitespace or control characters.
  Every other scheme (`javascript:`, `file:`, `intent:`) is refused, so opening a received link cannot run or read
  anything local.
- The sender checks a share before sending it; the receiver checks it again while decoding.
- A share is acknowledged once it reached the local surface. The sender waits 10 s for the ack.
- Delivery is at most once per send: a send that failed may still have arrived, and resending may duplicate it.

Failure modes, each of which delivers nothing where it says so:

1. A `hello`, `pair-spake`, or `pair-confirm` once the session is running: close 5.
2. A `share-ack` for an id with no outstanding share, including a second ack for the same share: close 5.
3. A `share` with an empty or oversized text, an unknown kind, or a `link` that is not http or https: close 5,
   nothing delivered.
4. An `unpair` sent to the phone (the desktop unpairs with close 2): close 5.
5. A share not acknowledged within 10 s: the send fails with "timed out"; the session stays up.
6. The connection ends while a share waits for its ack: the send fails; the share may have arrived.
7. `Share` on D-Bus for a device without a live session: fails at once with `org.umbriel.Link1.Error.NotConnected`;
   nothing is dialled, since only the phone dials.
8. A network black hole shorter than the idle timeout: the same connection survives (QUIC retransmits).
9. A black hole longer than 30 s: both ends time out, the desktop shows the device disconnected, and the phone
   redials with backoff until the path returns.

### Discovery

- The desktop advertises `_umbriel-link._udp.local.` only while it has a paired device or an open window, so an
  unpaired desktop is invisible: instance name = `DeviceId`, TXT `v=1`, plus `pair=1` while a window is open. No
  device name.
- The phone dials, in order: last-known addresses (most recent success first), then mDNS results for the paired
  desktop's `DeviceId`. The first handshake that verifies wins.
- The desktop's QUIC port is 4717/udp (unassigned at IANA) on first start, or a random port if 4717 is taken, and is
  kept in the store either way, so last-known addresses stay valid across restarts. A store from before keeps its
  port.
- A fixed port lets a host firewall allow Link by name: the package ships the ufw profile
  `/etc/ufw/applications.d/umbriel-link` (4717/udp and mDNS 5353/udp), enabled with `sudo ufw allow "Umbriel Link"`.
  A desktop that fell back to a random port needs that port allowed by hand.

## Quick Share (`link-quickshare`)

Receive and send with stock Android Quick Share on the same network. The protocol is Google's; the reference for
it is NearDrop's `PROTOCOL.md`.

- Discovery: mDNS `_FC9F5ED42C8A._tcp`, instance name `base64url(0x23, 4-char endpoint id, FC 9F 5E, 00 00)`, TXT `n`
  = endpoint info (device type in bits 1-3 of byte 0, 16 random bytes, name length, name). Android only looks for
  receivers after it sees a BLE advertisement of service `fe2c` with data `FC 12 8E 01 42`, 12 zero bytes and 10
  random ones; the receiver registers it with BlueZ's `LEAdvertisingManager1`.
- Connection: TCP, every message a big-endian `u32` length and a protobuf. Plain connection request, UKEY2
  (P-256, SHA-512 commitment, next protocol `AES_256_CBC-HMAC_SHA256`), plain connection responses (client first),
  then every offline frame through the D2D channel: HKDF-SHA256 keys, AES-256-CBC with a fresh IV, HMAC-SHA256 over
  header and body, sequence numbers from 1 per direction. Keep-alive every 10 s; 30 s of silence ends it.
- Sharing: paired-key frames with random contents and result "unable" (contact certificates need a Google account),
  introduction, the receiver's accept or reject, then file payloads in chunks with offsets; the receiver
  disconnects when every file is in place. The PIN is the auth string folded base 31 modulo 9973.

In the daemon it is `org.umbriel.Link1.QuickShare` on the Link object (contract in the XML): hidden until `Visible` is
set, which persists across restarts; TCP 4718 (ufw profile), random if taken; every offer waits up to 60 s for
`Accept` or `Decline`; files go to `XDG_DOWNLOAD_DIR`, the only writable path in home (`ReadWritePaths`). E2E
`quickshare_daemon.sh`.

Failure modes, each ending with nothing written except complete, announced files:

1. A client finish that does not hash to the client init's commitment, or a key off the curve: the handshake fails.
2. A secure message with a bad HMAC, bad padding, or a sequence number out of order: the connection closes.
3. A frame over 5 MiB, a bytes payload over 1 MiB, or text over 1 MiB: refused before allocation.
4. File bytes past the announced size, or a last chunk short of it: the transfer fails and its part files are deleted.
5. A hostile name (`../x`, `.bashrc`, NUL, control characters, over 255 bytes): saved as a bare, visible name.
6. A name already taken: saved as `name (n).ext`; publishing is a hard link, so it never replaces a file.
7. Declined or cancelled: nothing is written. The sender going silent: the connection ends after 30 s.

## D-Bus: `org.umbriel.Link1`

Session bus, object `/org/umbriel/Link1`. Contract file: `protocol/link-v1/org.umbriel.Link1.xml`; the shell's
client is written against it.

| Member | Signature | Meaning |
|---|---|---|
| method `StartPairing` | `() → (s code, s uri)` | Opens the 120 s window; a new call replaces the old window |
| method `CancelPairing` | `()` | Closes the window |
| method `Unpair` | `(s device_id)` | Forgets the device; it is told at next contact |
| method `Share` | `(s device_id, s kind, s text)` | Sends `text` or `link` to a connected device and returns once it acknowledged; `org.umbriel.Link1.Error.NotConnected` without a live session, `org.umbriel.Link1.Error.Rejected` for a share that breaks the rules above, `org.umbriel.Link1.Error.Failed` when the device did not acknowledge it |
| property `Devices` | `a(ssb)` | `(device_id, name, connected)`, with `PropertiesChanged` |
| property `Pairing` | `b` | Whether a window is open |
| signal `PairingFinished` | `(s device_id, s name)` | A device was paired |
| signal `PairingFailed` | `(s reason)` | The window's attempt failed |
| signal `Received` | `(s device_id, s kind, s text)` | A device shared text or a link (`kind` is `text` or `link`), already checked |

## Android app

Kotlin, Jetpack Compose, one Gradle project under `link/android/`:
- Modules: `app` (Compose UI and Android services), `core` (domain and data over `link-ffi`). Dependency direction
  Presentation → Domain → Data; the domain layer imports neither.
- Feature-first packages under `app`: `pairing`, `devices`, `presence`, `share`, `notifications` (`clipboard` when
  its entry points land).
- One `ViewModel` per screen exposing `StateFlow`; UI actions return `Result`, never throw into the UI. Coroutines
  only, no callbacks above the data layer. Manual constructor injection from one `AppContainer`, no DI framework.
- `LinkRepository` reads `LinkClient.next_event()` for the life of the process, so `desktops` carries live connected
  flags and `incoming` every share; `ShareNotifier` turns each share into a notification (Open for a link, Copy for
  text). POST_NOTIFICATIONS is requested once a desktop is paired.
- Presence: the phone is present while any of its activities is started (`ProcessLifecycleOwner`) and while "Stay
  connected" is on. That toggle runs `PresenceService`, a `connectedDevice` foreground service whose notification
  exists only while it runs; it holds no state and only lets the connection live in the background. The multicast
  lock is held while present, for the mDNS half of a redial.
- The share target (`ACTION_SEND`, `text/plain`) sends to the only paired desktop, or asks which; a single http or
  https URL goes as a link.
- E2E: `tests/e2e/link_android.sh` drives the Maestro flows under `link/android/maestro/` on an emulator against a
  private `umbriel-linkd`, writing screenshots and `results.json` to `artifacts/link-android/`.
