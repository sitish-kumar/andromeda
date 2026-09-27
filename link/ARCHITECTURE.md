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
link/android/  the Android app, Kotlin + Compose over link-ffi.
```

Dependency direction is strictly downward: `daemon`, `phone`, `ffi` → `core` → `proto`. `proto` depends on no other
crate of ours; `core` never knows which binary runs it.

## Dependencies

| Crate | Used by | Why |
|---|---|---|
| `quinn` | core | QUIC: streams, 0-RTT, client migration. The protocol's transport |
| `rustls` (ring) | core | TLS 1.3 inside QUIC; RFC 7250 raw public keys |
| `ring` | proto, core, phone | Ed25519 keys, SHA-256, HKDF, HMAC; the only crypto provider |
| `rustix` (`fs`) | core | `statvfs` for the free space an offer needs and `O_NOFOLLOW` on part files; std has neither, and it is already in the tree |
| `spake2` | proto | The PAKE for code and QR pairing (RustCrypto) |
| `ciborium`, `serde`, `serde_bytes` | proto | CBOR wire encoding |
| `mdns-sd` | core | mDNS responder and browser; no Avahi dependency (see continuity.md) |
| `tokio` | core, daemon, phone | Runtime, current-thread only |
| `zbus` | daemon | D-Bus service on the tokio runtime, no extra thread |
| `thiserror` | proto, core | Library error enums |
| `anyhow`, `clap`, `env_logger`, `log` | binaries (`log` everywhere) | CLI and logging |
| `cddl` | phone | Validates E2E transcripts against `protocol/link-v1/messages.cddl` |
| `httparse` | daemon | LocalSend's HTTP/1.1 request and response heads: a small, allocation-free parser for untrusted input; bodies and routing are ours, since a full HTTP stack would be far larger |
| `tokio-rustls` | daemon | LocalSend's TLS over tokio TCP streams, on the rustls and ring already in the tree |
| `serde_json` | core, daemon | The on-disk device store; LocalSend's JSON bodies |

## Threads

`umbriel-linkd` runs one tokio current-thread runtime (QUIC, D-Bus, timers, the store) plus `mdns-sd`'s responder
thread while it advertises. Nothing else. File I/O runs on the runtime thread too: page-cache writes, an fdatasync per
8 MiB, and hashing that yields between 256 KiB chunks.

## Wire format (link-v1)

- QUIC, ALPN `umbriel-link/1`, TLS 1.3 with raw public keys (Ed25519 SPKI) on both sides.
- The phone is always the QUIC client, the desktop the server. The phone's first bidirectional stream is the control
  stream; unidirectional streams, opened by either side, carry bulk data (at most 8 at once per side).
- Congestion control is BBR: loss-based control collapses under the random loss of Wi-Fi. quinn's BBR keeps the
  lowest RTT a connection ever saw, so a path whose delay grows under a live connection runs near its minimum
  window until the next connection.
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

### Files

LocalSend v2's offer and accept, plus resume by byte offset, a streaming SHA-256, and an enforced size.

```
sender                                               receiver
  offer {transfer, files: [{id, name, size,      ->  consent: a notification, or the device's auto-accept
         mime, sha256}]}
                                                 <-  offer-reply {transfer, accepted, reason?}
  one unidirectional stream per file, at most 4
  at once: file-data {transfer, file, offset}    ->  write .<name>.linkpart, hash, fsync every 8 MiB
  then the bytes from offset to size
                                                 <-  file-done {transfer, file, ok}   (after verify and publish)
after a reconnect:
  resume {transfer}                              ->
                                                 <-  file-done for each finished file, then
                                                     resume-at {transfer, offsets: [{file, offset}]}
either side, any time:
  cancel {transfer}                              ->  both drop the transfer; the receiver deletes its partials
```

- `transfer` is 16 random bytes (32 hex characters on D-Bus); file ids are unique within an offer. An offer must fit
  one control frame. `reason` is `declined`, `no-space` (the files exceed statvfs' free space), `too-large` (they
  exceed the whole filesystem), or `busy` (8 transfers from that device are already open).
- A stream's first frame is a `file-data` envelope in the control framing; the raw bytes follow it.
- The receiver writes `Downloads/.<name>.linkpart`, created `O_EXCL` (a taken name gets ` (1)`, ` (2)`, ...), hashes
  as it writes (`ring::digest::Context`), and records the durably written offset in its state file, fsyncing every
  8 MiB and when a stream stops early. When the size is reached and the hash matches, it hard-links the part to a
  free final name (`name (1).ext` when `name.ext` exists), never overwriting, and unlinks the part. Android denies
  apps hard links, so there it renames with `RENAME_NOREPLACE` instead.
- A resumed file is truncated to its durable offset and re-hashed from disk, since a digest context cannot be saved.
- The receiver keeps its state on disk (`transfers/<id>.json` in the state directory) and drops transfers older than
  24 h with their partials. The sender keeps its sources open in memory for 24 h: a transfer resumes across
  reconnects and across a receiver restart, not across a sender restart.
- Names are sanitized on receipt: the basename after the last `/` or `\`, NUL and control characters removed,
  leading dots stripped, at most 255 bytes cut on a UTF-8 boundary; an empty or all-dots result is `file`.
- The desktop's sandbox cannot read the user's files, so the shell opens them and passes the descriptors (`SendFiles`,
  `a(hs)`). The daemon accepts only regular files.

Failure modes:

1. An offer with no files, duplicate file ids, an empty name or mime, or a wrong-length id or hash; an `offer-reply`
   whose `accepted` and `reason` disagree; a `resume-at` with duplicate files: close 5 while decoding.
2. An offer reusing a transfer id the receiver holds: close 5.
3. An `offer-reply` for a transfer not offered to that peer, or already answered: close 5.
4. A `file-data` on the control stream, or any other message as a stream's first frame: close 5.
5. A stream for an unknown or unaccepted transfer, an unknown or finished file, a file that already has a stream, or
   an offset other than the receiver's: the stream is stopped with 5 and nothing is written.
6. A byte past the announced size, or a stream finished before it: the stream is stopped with 5, the partial deleted,
   and the file reported `file-done {ok: false}`.
7. A stream reset or a connection lost mid-file: the partial and its durable offset stay for a resume.
8. A hash mismatch: the partial is deleted, nothing is published, and the file is reported failed.
9. A `resume` for a transfer the receiver does not hold (finished, cancelled, expired): answered with `cancel`.
10. A `resume-at` for a transfer that was not resumed, naming a file the transfer lacks or already finished, or an
    offset past a file's size: close 5.
11. A `file-done` for a transfer or file the sender does not have, or contradicting an earlier one: close 5. The same
    result again (after a resume) is accepted.
12. A `cancel` for an unknown transfer: ignored, since it may have crossed the end.
13. No consent within 120 s: declined. The session ending before the answer: the offer is withdrawn on both sides.
14. `SendFiles` with a descriptor that is not a regular file, or for a device without a live session: fails at once.

### Clipboard

Automatic both ways, for devices holding the clipboard grant. Content moves only when it is used, except text sent
inline so the other side can set it at once.

```
either side                                     other side
  clip-offer {id, mimes, size, text?}      ->   desktop: a Wayland selection owned by the shell, served on paste
                                                phone: text set at once; other types as a content:// URI
on paste or read:
                                           <-   clip-pull {id, mime}
  a unidirectional stream: clip-data       ->   the bytes, then FIN
  {id, mime}, then at most `size` bytes
```

- `id` counts up per sender; a new offer replaces the sender's previous one, and only the latest can be pulled.
  `mimes` (1 to 16, each at most 255 bytes) is in the sender's order of preference; `size` (at most 64 MiB) is the
  length of the first. `text` (at most 61440 bytes) is allowed only when a `text/plain` type is offered, and a pull
  of that type is answered from it without a stream. The desktop inlines text it offers; the headless phone never
  does, so the E2E can prove a lazy pull; the Android app inlines text, since its process may be frozen by the time
  a desktop app pastes.
- Echoes: each side keeps the SHA-256 of the last clip it applied from the other and offers nothing with that hash.
  The shell also marks the selection it serves for a phone (`application/x-umbriel-link-remote`), so reading its own
  selection never pulls it.
- Grants: `clipboard` (on after pairing), `files` (on), `notifications` (off), per device in the store. The desktop
  sends no clip-offer to a device without the clipboard grant and drops one from it; an offer from a device without
  the files grant is declined.

Failure modes:

1. A `clip-offer` with no types or more than 16, a type longer than 255 bytes, a size over 64 MiB, or `text` without
   a `text/plain` type: close 5 while decoding.
2. A `clip-pull` for an id that is not the latest offer, or a type it did not offer: the offerer answers with a
   `clip-data` stream it resets at once, so the paste fails and nothing is sent.
3. A `clip-data` stream nobody pulled, or bytes past the offered size: the stream is stopped with 5 and the paste
   fails.
4. A pull not answered within 10 s: the paste fails; the session stays.
5. A `clip-offer` from a device without the clipboard grant: dropped, and logged.

### Status and limits

- `status {battery, charging, network}` goes from the phone to the desktop when a session starts and when the value
  changes, at most every 10 s (a change inside that window goes out when it ends). `network` is `wifi`, `cellular`,
  `ethernet`, `none`, or `other`. The desktop keeps it only while the session lives.
- The desktop limits what one phone connection sends, per feature, with a token bucket: 10 shares then one per 2 s,
  5 file offers then one per 6 s, 20 clipboard offers then one per second, 3 statuses then one per 10 s.

Failure modes:

1. A `status` with a battery over 100 or an unknown network: close 5 while decoding. A `status` sent to the phone:
   close 5.
2. A share over its limit: dropped without an ack, so the sender's send times out; nothing reaches D-Bus.
3. A file offer over its limit: answered `busy`; nothing reaches D-Bus.
4. A clipboard offer or a status over its limit: dropped.

### LocalSend

A second transfer backend in `umbriel-linkd`, off until "Visible to LocalSend" is turned on, so any LocalSend app
(protocol v2) can send to the desktop without Link, and the desktop can send to LocalSend devices on the LAN.

- While on: UDP 53317 joined to 224.0.0.167 for announcements (ours: `announce: true` when turned on; theirs are
  answered with an HTTPS `POST /api/localsend/v2/register` to the announcer), and HTTPS on TCP 53317.
- TLS 1.3 with a self-signed ECDSA P-256 certificate made once and kept in the state directory (`localsend.pk8`,
  `localsend.der`, mode 0600); its SHA-256, lowercase hex, is the LocalSend fingerprint. As a client the daemon
  accepts any certificate whose SHA-256 is the fingerprint the peer announced, and nothing else.
- Receive: `prepare-upload` becomes the same `TransferOffered` as a Link offer and waits for the answer (120 s);
  accepted, it returns a session id and one token per file; `upload?sessionId&fileId&token` streams a file into the
  same part files as Link (sanitized names, O_EXCL, size enforced, published by hard link), verified against
  `sha256` when the sender gave one; `cancel` drops the session and its partials. Device ids are
  `localsend:<fingerprint>`.
- Send: `SendFiles` to a `localsend:<fingerprint>` id discovered on the LAN (the `Nearby` property) posts
  `prepare-upload` with each file's SHA-256, then uploads each accepted file.
- HTTP/1.1 is parsed with `httparse` (request and response heads); bodies are `Content-Length` or chunked, JSON
  bodies at most 1 MiB. One session at a time: a second `prepare-upload` while one is open is answered 409.

Failure modes:

1. A body over 1 MiB, malformed JSON, a head over 16 KiB, or an unknown path: 400 or 404, connection closed.
2. `prepare-upload` declined or unanswered for 120 s: 403. A file set larger than the free space: 403 and a
   `no-space` result; another session open: 409.
3. `upload` with an unknown session, file, or token, or a file already uploaded: 403; a body longer or shorter than
   the file's size: 400 and the partial deleted; a SHA-256 that does not match the sender's: 422, nothing published.
4. `cancel` for an unknown session: 200, nothing changes.
5. As a sender: a peer whose certificate is not the announced fingerprint fails the handshake; 403 is `declined`,
   409 and 429 are `busy`, anything else `failed`.

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

## D-Bus: `org.umbriel.Link1`

Session bus, object `/org/umbriel/Link1`. Contract file: `protocol/link-v1/org.umbriel.Link1.xml`; the shell's
client is written against it.

| Member | Signature | Meaning |
|---|---|---|
| method `StartPairing` | `() → (s code, s uri)` | Opens the 120 s window; a new call replaces the old window |
| method `CancelPairing` | `()` | Closes the window |
| method `Unpair` | `(s device_id)` | Forgets the device; it is told at next contact |
| method `Share` | `(s device_id, s kind, s text)` | Sends `text` or `link` to a connected device and returns once it acknowledged; `org.umbriel.Link1.Error.NotConnected` without a live session, `org.umbriel.Link1.Error.Rejected` for a share that breaks the rules above, `org.umbriel.Link1.Error.Failed` when the device did not acknowledge it |
| method `SendFiles` | `(s device_id, a(hs) files) → s transfer_id` | Offers the files behind the descriptors (regular files only, each with the name the device sees); returns at once, the signals below report the rest. `NotConnected`, `Rejected`, `Failed` as for `Share` |
| method `AcceptTransfer`, `DeclineTransfer` | `(s transfer_id)` | Consent for a `TransferOffered`; `Rejected` once it no longer waits |
| method `CancelTransfer` | `(s transfer_id)` | Either direction; the device is told, partials are deleted |
| method `SetAutoAccept` | `(s device_id, b enabled)` | Accept that device's offers without asking; off after pairing |
| property `Devices` | `a(ssb)` | `(device_id, name, connected)`, with `PropertiesChanged` |
| method `SetLocalSendVisible` | `(b visible)` | "Visible to LocalSend"; kept in the store |
| property `LocalSendVisible` | `b` | Whether the LocalSend backend listens |
| property `Nearby` | `a(ss)` | LocalSend devices on the LAN, `(localsend:<fingerprint>, alias)`; `SendFiles` takes these ids |
| method `SetGrant` | `(s device_id, s feature, b granted)` | `clipboard`, `files`, or `notifications` |
| method `OfferClipboard` | `(as mimes, h data)` | The desktop's clipboard changed; `data` is the first type's bytes. Offered to connected devices with the clipboard grant, text inline |
| method `PullClipboard` | `(s device_id, t id, s mime, h sink) → t bytes` | Writes a device's offered clip into the paste target's pipe |
| property `AutoAccept` | `as` | Devices whose offers are accepted without asking |
| property `Grants` | `a{sas}` | Device id to the features it holds |
| property `DeviceStatus` | `a{s(ubs)}` | Connected device id to `(battery, charging, network)` |
| property `Pairing` | `b` | Whether a window is open |
| signal `PairingFinished` | `(s device_id, s name)` | A device was paired |
| signal `PairingFailed` | `(s reason)` | The window's attempt failed |
| signal `Received` | `(s device_id, s kind, s text)` | A device shared text or a link (`kind` is `text` or `link`), already checked |
| signal `ClipboardOffered` | `(s device_id, t id, as mimes, t size)` | A device's clipboard changed; served as a selection, pulled on paste |
| signal `TransferOffered` | `(s transfer_id, s device_id, a(st) files)` | A device offers files `(sanitized name, size)`; not sent for auto-accept devices |
| signal `TransferProgress` | `(s transfer_id, t bytes, t total)` | Either direction, at most 4 Hz per transfer |
| signal `TransferFinished` | `(s transfer_id, s status, as paths)` | `done`, `failed`, `declined`, `no-space`, `too-large`, `busy`, or `cancelled`; `paths` are the verified files an incoming transfer published |

Transfers and consent are backend-neutral in the daemon: the transfer actor (`link_core::transfer`) owns every
transfer and reports offers to the hub, which answers from the device's auto-accept setting or forwards the offer to
the shell, so a second backend (Quick Share, LocalSend) plugs into the same signals and notifications.

## Android app

Kotlin, Jetpack Compose, one Gradle project under `link/android/`:
- Modules: `app` (Compose UI and Android services), `core` (domain and data over `link-ffi`). Dependency direction
  Presentation → Domain → Data; the domain layer imports neither.
- Feature-first packages under `app`: `pairing`, `devices`, `presence`, `share`, `transfer`, `notifications` (`clipboard` when
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
- Files: the share target also takes `ACTION_SEND` and `ACTION_SEND_MULTIPLE` of any type; each content URI is
  opened as a descriptor the core takes over (`detachFd`), so a provider that hands out a pipe is refused. Offers
  from the desktop become a notification with Accept and Decline (`TransferNotifier`, `TransferReceiver`). The core
  writes received files to app storage; `Downloads` then inserts a `MediaStore.Downloads` entry with `IS_PENDING=1`,
  copies while hashing, and clears `IS_PENDING` only when the copy's SHA-256 is the one the core verified.
  `TransferService`, a `dataSync` foreground service, runs while a transfer is open, since Android freezes a cached
  process and its sockets.
- Status: `StatusReporter` follows the sticky `ACTION_BATTERY_CHANGED` broadcast and the default network callback
  and hands every change to the core, which rate-limits what it sends.
- Clipboard (`clipboard` package): `ClipboardSync` sets a desktop's text at once and other types as a URI of
  `ClipProvider`, whose `openFile` pulls the type into a pipe; it clears the clip after 2 minutes unless the clipboard
  changed. `ClipboardWatcher` runs while `PresenceService` does and `READ_LOGS` is granted: it follows
  `logcat -T 1 ClipboardService:E` for the denial Android logs for this app on every copy elsewhere (it holds a
  clipboard listener for that reason) and starts `ClipboardReadActivity`, transparent, which reads the clipboard once
  focused, offers it unless its hash is the last desktop clip, and finishes. The same activity serves the
  quick-settings tile (`ClipboardTileService`) and "Send to desktop" in the text-selection menu (`PROCESS_TEXT`).
- E2E: `tests/e2e/link_android.sh` drives the Maestro flows under `link/android/maestro/` on an emulator against a
  private `umbriel-linkd`, writing screenshots and `results.json` to `artifacts/link-android/`.
