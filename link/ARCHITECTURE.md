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
link/android/  the Android app, Kotlin + Compose over link-ffi; `fixture/` is a stand-in media and chat app for the
               emulator E2E.
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
| `zbus` | daemon | D-Bus service on the tokio runtime, no extra thread; also each exported MPRIS player and the client of the desktop's |
| `futures-util` | daemon | `StreamExt` over zbus's `MessageStream`, for the MPRIS signals the daemon follows; already in the tree through zbus |
| `serde_json` | core, daemon | The on-disk device store; LocalSend's JSON bodies |
| `hex` | phone, daemon | Transcript lines; artwork file names |
| `thiserror` | proto, core | Library error enums |
| `anyhow`, `clap`, `env_logger`, `log` | binaries (`log` everywhere) | CLI and logging |
| `cddl` | phone | Validates E2E transcripts against `protocol/link-v1/messages.cddl` |
| `prost`, `prost-types`, `prost-build` | quickshare | Quick Share frames are protobuf; generated from `protocol/quickshare/` at build time (needs `protoc`) |
| `aes`, `cbc` | quickshare | AES-256-CBC with PKCS#7 for Quick Share's D2D channel; ring has no CBC (see CONVENTIONS) |
| `httparse` | daemon | LocalSend's HTTP/1.1 request and response heads: a small, allocation-free parser for untrusted input; bodies and routing are ours, since a full HTTP stack would be far larger |
| `tokio-rustls` | daemon | LocalSend's TLS over tokio TCP streams, on the rustls and ring already in the tree |

## Threads

`umbriel-linkd` runs one tokio current-thread runtime (QUIC, D-Bus, timers, the store) plus `mdns-sd`'s responder
thread while it advertises, and a second one while Quick Share is visible. Nothing else. File I/O runs on the
runtime thread too: page-cache writes, an fdatasync per 8 MiB, and hashing that yields between 256 KiB chunks.

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
- Grants (Grants below): the desktop sends no clip-offer to a device without the clipboard grant and drops one from
  it; an offer from a device without the files grant is declined. The phone does the same with its switches.

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
### Notifications

```
phone                                                          desktop
  notification-posted {id, app, title, text, icon?, actions} ->  D-Bus NotificationPosted; the shell shows it
  notification-removed {id}                                  ->  D-Bus NotificationRemoved; the shell closes it
                                                             <-  notification-action {id, action, reply_text?}
                                                             <-  notification-dismiss {id}
```

- `id` is the phone's notification key, 1 to 256 bytes; a post with an id the desktop shows replaces it. `app` is
  1 to 128 bytes, `title` at most 512, `text` at most 4096 (the phone truncates at a character boundary). `icon` is
  the app icon as a PNG of 1 to 16384 bytes (the phone draws it at 64 px). At most 3 `actions`, each an `id` and a
  `label` of 1 to 64 bytes and `reply`, whether it takes RemoteInput text. `reply_text` is 1 to 4096 bytes. A post
  is at most about 22 KiB, well inside one frame.
- The phone sends posts and removals, the desktop actions and dismissals; neither is acknowledged, so each is
  delivered at most once. After every connect the phone posts every notification it mirrors and removes the ones it
  mirrored earlier that are gone; the daemon keeps each device's live notifications in memory across sessions and
  drops a re-post identical to the one it holds, so a reconnect shows nothing new.
- The daemon holds at most 64 live notifications per device; a post of a new id beyond that removes the oldest
  first. Unpairing removes all of the device's notifications.
- The phone mirrors only what the user would see: no ongoing, low-importance, or group-summary notifications, none
  of its own, and while the phone is locked none whose lock-screen visibility (the app's, or the user's override
  for its channel) is secret, and a private one as its public version when it has one. The user can exclude apps.

Failure modes:

1. A `notification-posted` or `-removed` sent to the phone, or an `-action` or `-dismiss` sent to the desktop: close 5.
2. A post with an empty or oversized field, more than 3 actions, or an icon over 16 KiB; a `reply_text` empty or
   over 4096 bytes: close 5, nothing delivered.
3. An icon that is not a decodable PNG of at most 256 by 256 pixels (checked in its header before decoding, so a
   small file cannot inflate into a huge bitmap): the shell shows the notification with the phone glyph.
4. A removal, action, or dismissal for an id the receiver does not hold: ignored, since either side may have lost it
   to a reconnect or an expiry.
5. An action id the notification no longer has, or `reply_text` for an action that takes none: the phone ignores it.
6. A 65th live notification from one device: the oldest is removed on the desktop before the new one shows.
7. `NotificationAction` or `NotificationDismiss` on D-Bus without a live session: `NotConnected`; an empty id or
   action, or a reply over 4096 bytes: `Rejected`.
8. A post lost with its connection: it shows at the next connect's re-post.

### Media

Both sides describe their own players and command the other's, with the same three messages:

```
either side                                                   other side
  media-player {player, name, state, title, artist, album,  ->  phone players: an MPRIS player on the desktop
                length_ms?, position_ms, volume?, artwork?,      desktop players: the phone's Media screen
                can}
  media-gone {player}                                       ->  that player is gone
                                                            <-  media-command {player, command, value?}
```

- `player` is the sender's id for the player and `name` the app playing, 1 to 64 bytes each. `state` is `playing`,
  `paused`, or `stopped`. `title`, `artist`, and `album` are at most 512 bytes each. `position_ms` is the position
  when sent, which the receiver advances while playing; `length_ms` is absent when unknown. `volume` is 0 to 100,
  absent when the player has none. `artwork` is a PNG or JPEG of 1 to 49152 bytes. `can` lists the commands the
  player takes, each at most once.
- `command` is `play`, `pause`, `play-pause`, `next`, `previous`, `seek` (`value` is the absolute position in ms), or
  `volume` (`value` 0 to 100). `seek` and `volume` need a `value`; the others take none.
- A player is re-sent when its state, metadata, commands, or volume change, or its position jumps; not on the
  steady tick of playback. After every connect each side sends all its players.
- The phone sends one player: the session Android lists first, the one its own media controls show; when that
  changes it sends `media-gone` for the old one. The desktop sends every MPRIS player but the ones it exports for
  phones, at most 8.
- The daemon exports a phone's player as `org.mpris.MediaPlayer2.umbriel_link_<device id>` (the latest if a phone
  sends more than one), with its artwork as a file in the state directory. Its methods become `media-command`s.
  The name goes when the player does or the session ends, since a phone that cannot be reached cannot be commanded.
- Unacknowledged, at most once, like notifications.

Failure modes:

1. A field over its limit, an unknown `state` or `command`, a repeated entry in `can`, a `seek` or `volume` without
   `value`, a `value` on another command, or a volume over 100: close 5, nothing delivered.
2. A command for a player the receiver does not have, or one it did not list in `can`: ignored.
3. A 9th player from one device: the daemon drops the least recently updated one first.
4. Artwork the desktop cannot read, or over 49152 bytes: the player is sent without artwork.
5. The session ends: the other side's players are forgotten; they return with the next connect's re-send.

### Find my phone, find my desktop

```
either side                         other side
  ring {on}                     ->  starts (true) or stops (false) ringing
                                <-  ringing {on}   whenever its own ringing starts or stops, for any reason
```

- Both directions use the same two messages: the desktop rings the phone from the Devices tab or `link-ring`, the
  phone rings the desktop from its home screen. Either side stops a ring, from where it rings or from where it was
  started, and `ringing` keeps both surfaces showing the truth.
- The phone rings on the alarm stream at full volume, through silent mode, and puts the alarm volume back when the
  ring stops. Do Not Disturb lets alarms through unless the user blocked them there; before Android 15, DND access
  also lifts DND for the ring, which Android 15 no longer allows an app to do. The desktop
  plays the shell's alarm sound at full volume whatever the UI-sound setting.
- A ring stops by itself after 2 minutes. It outlives the session that started it, since a phone that moved out of
  reach is exactly the one being looked for.
- Unacknowledged, at most once.

Failure modes:

1. A `ring {on: true}` while already ringing, or `{on: false}` while silent: ignored, apart from a fresh `ringing`
   reply.
2. A ring to a phone whose ring switch is off: ignored; its `ringing` stays false.
3. `Ring` on D-Bus without a live session: `NotConnected`.
4. A `ringing` report that does not match what the receiver asked: shown as reported; the report is the truth.

### Calls

```
phone                                              desktop
  call {state, number?, name?}                 ->  D-Bus Call; the shell's incoming-call notification
                                               <-  call-action {action}   ("mute" or "decline")
```

- `state` is `ringing`, `active`, or `idle`, as Android's call state says; the phone sends each change. `number` is
  at most 64 bytes and only sent when Android gives the app the number (`READ_CALL_LOG`); `name` is at most 128
  bytes and only sent when the number is in the contacts and the app may read them (`READ_CONTACTS`).
- `mute` silences the ringer until the call ends; `decline` ends a ringing call (`TelecomManager.endCall`, which
  needs `ANSWER_PHONE_CALLS`).
- While any phone's call is ringing or active, the daemon pauses the desktop's playing MPRIS players, and resumes
  those it paused when every call is idle.
- Unacknowledged, at most once.

Failure modes:

1. A `call` sent to the phone or a `call-action` sent to the desktop: close 5.
2. An unknown state or action, or a number or name over its limit: close 5.
3. `call-action` with no ringing call, or `decline` without the permission: the phone ignores it.
4. The session ends during a call: the daemon treats the call as idle and resumes the paused players, since it will
   not hear the end.
5. `CallAction` on D-Bus without a live session: `NotConnected`; an unknown action: `Rejected`.

### Grants

One switch per paired device and feature, in the store (`grants` in `devices.json`; a phone store's older
`sharing` is read as it): `clipboard`, `files`, `notifications`, `media`, `ring`, and `calls`, all on after pairing.
Each side applies its own: the desktop's say what that phone may send it (D-Bus `SetGrant` and `Grants`, the Devices
tab's toggles), the phone's what it shares with that desktop (the device page). A message whose feature is off is
dropped where it arrives, and logged; a file offer is declined, not dropped, so the sender learns at once.

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

### From anywhere (Tailscale)

The desktop's hello lists every address a phone could dial except loopback, IPv6 link-local, and container or VM
bridges (`docker*`, `br-*`, `veth*`, `virbr*`, ...): LAN IPv4, then overlay IPv4 (Tailscale's `100.64.0.0/10`), LAN
IPv6, overlay IPv6 (`fd7a:115c:a1e0::/48`). The phone keeps up to 8, with the one that last worked first and the
latest hello's next, so a full list still takes a new network's addresses. Two devices on one tailnet then reach each
other through last-known addresses like on a LAN; mDNS does not cross it. A phone on mobile data does not probe from
a Bluetooth session, so an overlay never moves one onto the data plan. E2E `link_tailnet.sh`.

### Bluetooth

A paired phone with no IP path to its desktop runs the same session over RFCOMM. Pairing stays QUIC-only: SPAKE2
binds to the QUIC TLS exporter.

```
phone (client)                                        desktop (server)
  RFCOMM to the desktop's adapter, SDP service     ->  BlueZ Profile1.NewConnection hands the socket over
  5c3b1e5a-7d2f-4a8e-9b61-3f0c2d4e8a17, insecure
  TLS 1.3, raw public keys, ALPN umbriel-link-stream/1, the desktop pinned by fingerprint
  mux frames: the control stream (bidirectional, id 0), then bulk streams, exactly as over QUIC
```

- Where: the desktop's hello lists `bt:AA:BB:CC:DD:EE:FF` among its `addresses`; phones from before skip it, since it
  is not a socket address. The phone keeps it in its store as the peer's `bluetooth`.
- When: the phone tries last-known addresses and mDNS first, then Bluetooth if the store has an address and the
  platform can open RFCOMM (`BluetoothOpener`). The profile asks BlueZ for neither authentication nor authorization,
  since Link's TLS authenticates both ends, but BlueZ accepts connections only from bonded devices, so the phone
  bonds with the desktop once (below) and then opens an encrypted socket.
- Bonding, over a Wi-Fi session: the desktop's hello lists `feature:bt-pairing`; a phone not yet bonded sends
  `bt-pairing {}`, and the desktop accepts unpaired connections for 60 s; Android then pairs (the user taps Pair
  once) and the phone sends `bt-pairing {passkey}`. The shell's BlueZ agent asks `ConfirmBluetoothPairing` (which
  waits up to 10 s for the report) and confirms a match without a prompt; anything else gets the normal prompt.
- Bonding with no network: the QR code names the adapter (`b=`), and the desktop accepts unpaired connections while
  a pairing window is open. A phone that reaches no address bonds (confirmed on both screens, since no session can
  vouch for the code) and runs the same pairing over the Bluetooth stream, bound to the stream's TLS exporter.
- The multiplexer (`link_proto::mux`, `link_core::mux`) gives a byte stream QUIC's stream model: a 9-byte header
  (`kind: u8`, `stream: u32`, `len: u32`), streams opened by the client even and by the server odd, DATA of at most
  16 KiB, a 256 KiB credit window per stream, FIN, RESET and STOP with a code, CLOSE with a close code and reason,
  and PING/PONG. The control stream's frames go ahead of queued bulk data, and at most 64 KiB of bulk data waits for
  the writer. Keep-alive and the idle timeout are QUIC's (10 s, 30 s). `link_core::wire` holds either connection
  behind the one API the session and transfers use.
- Moving up: while a session runs over Bluetooth, the phone probes for an IP path at once and then every 30 s, off
  its actor, punching the desktop's firewall first (see below). A probe
  that reaches the desktop becomes a QUIC session; the desktop then closes the Bluetooth one, as it does any older
  session, and open transfers resume on the new one by offset.
- A send past 1 MiB (`BLUETOOTH_UPGRADE_ABOVE`) on Bluetooth asks for the phone's hotspot first, from both sides,
  since it takes longer over Bluetooth than joining. When the hotspot fails, files up to 20 MiB per offer
  (`BLUETOOTH_FILE_LIMIT`) go over Bluetooth; larger ones are refused before any byte moves with "too large to send
  over Bluetooth". A `hotspot` over the peer's rate limit is answered with `hotspot-end`, so neither side waits.
- Android: `BluetoothSocket` exposes only streams, so `RfcommLink` pumps it through a socket pair on two threads and
  hands the core the other end. `BLUETOOTH_CONNECT` is asked on the onboarding's last page.
- E2E: `tests/e2e/link_bluetooth.sh` replaces RFCOMM with a Unix socket (`UMBRIEL_LINK_TEST_BLUETOOTH_SOCKET` on the
  daemon, `--bluetooth-socket` on the headless phone), so everything after the descriptor handover is the real code.

Failure modes:

1. No adapter, or Bluetooth off, on either side: no `bt:` address, or the RFCOMM connect fails; the phone reports the
   IP error and redials with backoff as before.
2. A phone whose key is not paired: admitted as over QUIC and refused with `not-paired`; nothing is delivered. Over
   Bluetooth, a pairing attempt runs only while a pairing window is open, and a wrong QR secret fails it
   (`link_pair_bluetooth.sh`).
3. A desktop presenting another key: the pinned handshake fails before any mux frame.
4. Garbage or a malformed mux frame: the handshake fails, or the connection closes with `protocol-error`; the daemon
   keeps serving. A peer opening a stream with the wrong parity, a reused id, more than 9 streams, or DATA past its
   window is a violation.
5. The IP path returning mid-session: the session moves to QUIC; no share is lost, since the old session stays up
   until the new one is adopted.
6. RFCOMM dropping mid-file: the partial stays and the transfer resumes by offset on the next session.
7. More than 20 MiB offered on Bluetooth: refused at once, from D-Bus and from the phone.

### Through the desktop's firewall

A host firewall (ufw, firewalld) drops the phone's QUIC dial, so the session would stay on Bluetooth at a few
tens of KB/s. The user is never asked to open a port: the desktop sends first, so its firewall takes the dial for a
reply.

```
phone (on Bluetooth)                             desktop
  punch {addresses: ["ip:port", ...]}        ->  one UDP byte from the Link port to each
                                                 (conntrack now expects replies from there)
  QUIC from that port to the desktop         ->  ESTABLISHED, accepted: a new session, the Bluetooth one closes
```

- `addresses` are the phone's own interface addresses with its dialer's port, IPv4 first, at most 8. The desktop
  sends from the listening socket itself (a clone of the one quinn reads), since conntrack matches the exact pair.
- The phone punches before every probe, and probes at once when a session lands on Bluetooth instead of after 30 s.
  An early dial that crosses the punch is dropped and QUIC's retransmit gets through.
- The byte is not QUIC; the phone's endpoint drops it. The desktop punches only addresses that parse, and a peer can
  make it send no more than 8 one-byte datagrams per message.
- Out of reach: a network that isolates clients (guest Wi-Fi) drops both directions, and an outbound-blocking
  firewall drops the punch; both stay on Bluetooth and the hotspot move still works.
- E2E: `tests/e2e/link_firewall.sh` puts the desktop behind an nftables input policy of drop (established only).

Failure modes:

1. `punch` with more than 8 addresses or one that is not `ip:port`: close 5 while decoding. `punch` sent to the phone:
   close 5.
2. A punch the network loses: the probe fails and the next one, 30 s later, punches again.
3. A phone with no Bluetooth session to the desktop cannot ask for a punch: it stays unreachable behind the firewall
   until it has one.

### Hotspot

A Bluetooth session moves to the phone's own local-only hotspot (no tethering, no mobile data) when a transfer is
larger than Bluetooth carries.

```
desktop                                          phone (on Bluetooth)
  hotspot-request {}                         ->  (or the phone's own send over 1 MiB)
                                                 start a local-only hotspot
                                             <-  hotspot {ssid, passphrase}
  join it through NetworkManager
  hotspot-joined {address: "ip:port"}        ->
                                             <-  QUIC to that address: a new session, the Bluetooth one closes
  either side: hotspot-end {reason?}         ->  stop / leave
```

- Only for a session on Bluetooth, and only with the files grant on; one hotspot at a time on the phone. The desktop
  rate-limits `hotspot` to two per 30 s, since each one makes it join a network.
- The desktop joins as a volatile, non-autoconnect WPA-PSK profile for the user (`AddAndActivateConnection2`,
  `persist: volatile`), so NetworkManager deletes it once it deactivates and brings the usual network back by itself.
  Android 13+ may run the hotspot in WPA3 transition mode; joining as WPA2-PSK avoids the SAE interop failures seen
  with wpa_supplicant 2.11. The laptop leaves its current Wi-Fi while joined; D-Bus signals `Hotspot(device, ssid)`
  on joining and with an empty SSID on leaving.
- A send over the limit waits up to 60 s for the move (`UPGRADE_TIMEOUT`), then goes over the new session; on either
  side, a failure comes back with its reason ("hotspot: NetworkManager could not join it").
- The phone stops the hotspot after 60 s with no transfer (`hotspot::IDLE`), tells the desktop, and closes the
  session that ran over it, so the redial falls back to Bluetooth at once instead of after the idle timeout. The
  desktop leaves when told, when that session ends, or when a Bluetooth session replaces it.
- Neither the hotspot's address nor the addresses the desktop announces on it are kept as last-known addresses.
- Android: `LocalHotspot` over `WifiManager.startLocalOnlyHotspot` (`NEARBY_WIFI_DEVICES` on Android 13+, fine
  location before), asked with `BLUETOOTH_CONNECT` on the onboarding's last page.

### Wi-Fi Direct

Tried before the hotspot, since the desktop joins a Wi-Fi Direct group as a P2P client beside its own Wi-Fi instead
of leaving it. Same slot, timers, and answers as the hotspot; only the start differs.

```
desktop                                          phone (on Bluetooth)
  wifi-direct-ready {name}                   ->  on every Bluetooth session, when NetworkManager has a P2P device
  hotspot-request {}                         ->  (or the phone's own send over 1 MiB)
                                             <-  wifi-direct {name}, then connect to the desktop's name as owner
  find that name, join it through NetworkManager
  hotspot-joined {address}  or  hotspot-end  ->  QUIC there (via "wifi-direct"), or fall back to the hotspot
```

- Both sides connect at once: a connection the app starts needs no confirmation on Android (an incoming one shows
  "Invitation to connect"), and NetworkManager accepts the negotiation it asked for; it cannot join an existing group
  (it always negotiates). The desktop waits 3 s after finding the phone, so the phone's request goes first and its
  intent 15 makes it the owner, serving DHCP: a desktop-owned group's DHCP is dropped by host firewalls. Groups are
  temporary (`WifiP2pConfig.Builder`), since Android re-invokes a persistent one by invitation.
- Needs NetworkManager on wpa_supplicant: with iwd on iwlwifi every P2P scan fails ("Network is down"), so groups
  never form and sends fall back to the hotspot.
- The phone matches the desktop by the P2P device address in `wifi-direct-ready` (the Wi-Fi device's permanent
  address; wpa_supplicant shows no name by default), sent only to phones whose hello lists
  `feature:wifi-direct-address`, else by name.
- The desktop finds the phone by name with
  `WifiP2P.StartFind` for up to 20 s, then activates a volatile, non-autoconnect `wifi-p2p` profile for the peer's
  hardware address. No `Hotspot` D-Bus signal, since the desktop kept its network.
- The phone falls back to the hotspot when its group does not form, the desktop answers `hotspot-end`, or 25 s pass
  (`WIFI_DIRECT_TIMEOUT`); it does not try a group again in that session. `wifi-direct` shares `hotspot`'s rate
  limit, and one over it is answered with `hotspot-end`.
- Before dialling the reported address, the phone sends `punch` with its addresses on the new link, so a desktop
  firewall takes the dial for a reply (hotspot too).
- Android: `WifiDirectGroup` over `WifiP2pManager` (`NEARBY_WIFI_DEVICES`, as for the hotspot). E2E
  `link_wifi_direct.sh`; interop with a real phone is proven by `tools/wifi-direct-spike.py`.
- E2E: `tests/e2e/link_hotspot.sh`, with `--hotspot` on the headless phone and `tests/e2e/nm_mock.py` standing in for
  NetworkManager by bringing up a second link between the namespaces.

Failure modes:

1. The phone cannot start a hotspot (no provider, Android refused, the files switch off, another desktop has it):
   `hotspot-end` with the reason; the send fails with it and nothing is sent.
2. NetworkManager cannot join (wrong passphrase, out of range): `hotspot-end` with the reason to the phone; the send
   fails with it.
3. The session never moves within 60 s: the send fails with "too large to send over Bluetooth".
4. `hotspot` on a session already on IP, or a second one: ignored. `hotspot-joined` from a desktop the hotspot does
   not serve: ignored.
5. The phone vanishes with the hotspot up: the session over it ends and the desktop leaves; NetworkManager would drop
   the profile anyway once the network is gone.

### Mirroring

The phone's screen in a desktop window (`umbriel-link-mirror`), with the desktop's input back on the phone. Only over
IP, since video needs Wi-Fi, and only while the phone's `screen` switch for the desktop is on (off after pairing).

```
viewer          desktop                                  phone
OpenMirror  ->  mirror-request {}                     -> screen switch off: mirror-stop {reason}
                                                         else a notification; Allow opens Android's capture prompt
                                                      <- mirror-started {width, height}
                                                      <- uni stream: mirror-data {width, height}, then access units
fd, size    <-  hands the viewer a socket, pumps units into it
MirrorInput ->  mirror-input {action, x, y, ...}      -> gestures, global actions, text
MirrorKeyframe  mirror-keyframe {}                    -> the encoder's next frame is a keyframe
close fd    ->  mirror-stop {"the viewer closed"}     -> capture stops (or the phone's Stop: mirror-stop back)
```

- Video: Android's hardware H.264 encoder behind a `MediaProjection` virtual display, at most 1080 px on the short
  side, 60 fps, 8 Mbit/s, realtime priority, a keyframe every 2 s or on request; the codec configuration goes in front
  of every keyframe, so the viewer can start at any. Each unit is a u32 length, u64 presentation time (µs), a flags
  byte (1 keyframe, 2 configuration), then Annex B bytes of at most 4 MiB; the daemon's socket to the viewer carries
  the same framing.
- The viewer: `appsrc ! h264parse ! vah264dec ! gtk4paintablesink sync=false` (VA-API on the Arc 140T,
  `avdec_h264` without it), letterboxed; a press under 12 px is a tap (a long press past 500 ms), a drag a swipe with
  its duration, a wheel notch a 15 % scroll at the pointer, Escape is Back, typed characters text. Coordinates are in
  1/10000 of the video.
- Input on Android goes through the `ScreenInput` accessibility service, which the user turns on once: a sideloaded
  app has no other way (`INJECT_EVENTS` is signature-only). Gestures by `dispatchGesture`, Back, Home, and Recents by
  `performGlobalAction`, text by `ACTION_SET_TEXT` on the focused field (appending; U+0008 deletes).
- Android asks for capture every session, and an app may not skip it; the request notification times out with the
  desktop's 60 s. `mirror-request` is limited to 2 per 10 s and `mirror-input` to a burst of 120, 100 a second.
- One mirror per phone; a second `OpenMirror` fails. A stream nobody asked for is stopped.
- E2E `link_mirror.sh` (headless: a file of SMPTE bars stands in for the encoder, `--frames` decodes to PNG).

### Phone apps in desktop windows

`umbriel-link-apps <phone>` (the Devices tab's apps button) opens the phone's apps one per desktop window. Only the
ADB `shell` user may create a virtual display and launch an activity on it, so this rides wireless debugging and
scrcpy (`--new-display --start-app`, each app its own display and window with full keyboard and mouse input), not the
Link session. The phone is found in `adb devices`, else by connecting to the `_adb-tls-connect` services `adb mdns`
lists, matched by `settings get global device_name` when there is more than one; an unpaired phone gets a pairing
page that takes the code from Wireless debugging's "Pair with code" dialog (`adb pair` to its `_adb-tls-pairing`
service). Closing a window ends its display. `--list` and `--launch <package>` do the same without a window. E2E
`link_apps.sh` (emulator and scrcpy).

### Browsing
### Browsing

The desktop reads the phone's shared storage, read-only, while the phone's browse switch for it is on (off after
pairing).

```
desktop                                         phone
  fs-list {req, path, cursor?}              ->
                                            <-  fs-entries {req, entries, next?}   (128 per page, name order)
  fs-read {req, path, offset, len}          ->
                                            <-  fs-data {req, offset, data, last?} (48 KiB each, in order)
                                            <-  fs-error {req, reason}             (instead of either answer)
```

- Paths: `/` lists the roots the app gives (`Storage` = shared storage, `Photos` = its `DCIM`, and none without All
  files access); below that, `/<root>/<name>/...` with no empty, `.`, or `..` component, checked while decoding. The
  phone canonicalizes every path and refuses one that leaves its root once symlinks resolve (`denied`); hidden
  entries are left out. At most 8 requests are served at once (`busy` beyond).
- Answers go on the control stream, so reads are chunked to keep other messages moving. A read is at most 1 MiB.
- The desktop pairs answers to requests by `req` (`link-daemon/src/browse.rs`), follows cursors to the end of a
  listing (at most 16384 entries), and fails a request the phone does not answer within 15 s. D-Bus: `ListFiles` and
  `ReadFile`, whose `.Refused` errors carry the phone's reason.
- `umbriel-link-mount` (crate `link-mount`), a user service outside the daemon's sandbox, mounts `~/Phone` with
  FUSE: one folder per connected phone, named after it, and under it the phone's roots. Listings and attributes stand
  5 s; reads go through 1 MiB blocks (32 kept), so a sequential read fetches ahead. Read-only, `noexec`, `nosuid`,
  `nodev`. The shell's Devices tab opens a phone's folder.
- E2E: `tests/e2e/link_browse.sh` mounts in private user, network, and mount namespaces against the headless phone's
  `--browse-root`.

Failure modes:

1. The switch off: `not-allowed`, EACCES through the mount; nothing is read.
2. A path out of its root, by `..` (refused while decoding) or by symlink (`denied`); a missing file (`not-found`,
   ENOENT); a folder read as a file or the reverse (EISDIR, ENOTDIR).
3. A phone that disconnects: pending requests fail at once, and its folder leaves the mount once the root listing
   goes stale.
4. A phone answering against the protocol (a read longer than asked, a listing past the cap): the request fails
   with EIO; the session stays.
5. Writes through the mount: EROFS.

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
`Accept` or `Decline`; files go to `XDG_DOWNLOAD_DIR`, the only writable path in home (`ReadWritePaths`). Sending:
`StartDiscovery` browses mDNS and sends the BLE hint (Android phones advertise only after seeing it) and fills
`Nearby`; `Send(peer, a(hs))` takes descriptors the shell opened, so the daemon still reads no path; `SendPin` and
`SendFinished` report. E2E `quickshare_daemon.sh`, `quickshare_shell.sh`.

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
- Feature-first packages under `app`: `pairing`, `devices` (Home and a desktop's page), `presence`, `share`,
  `notifications` (the mirror and its per-app filter), `media`, `ring`, `calls`, `onboarding`, `transfer` (files),
  `files` (the phone's storage and the in-app picker), and `clipboard`.
- The UI is the app's own design system in `ui/theme` (color, spacing, radius, type, shadow, and motion tokens: an
  electric-blue accent over charcoal and stone, a #0A0A0A dark surface, Source Sans 3 under the SIL OFL) and
  `ui/components` (the 28 dp soft card, 20 dp hero surface, 100 dp pills, the sliding-pill segmented control, switch
  and navigation rows, the connection orb, pull to refresh), built on Compose foundation. The app does not depend on Material components; only `material-icons-core` supplies glyphs.
- One `ViewModel` per screen exposing `StateFlow`; UI actions return `Result`, never throw into the UI. Coroutines
  only, no callbacks above the data layer. Manual constructor injection from one `AppContainer`, no DI framework.
- Home, Activity, and Settings have a persistent bottom navigation. Home is a device-first dashboard with a live
  connection card, a target selector for multiple desktops, the file picker, and clipboard, ring, media, and device
  controls. Clipboard still targets all connected desktops that grant it. Features disabled for the selected desktop
  lead to that desktop's settings rather than silently failing.
- Settings groups background connection, sharing and permissions, paired desktops, appearance, and app information.
  Appearance persists System, Light, or Dark in SharedPreferences and applies across all app activities. The permission
  onboarding itself is unchanged. Device settings separate everyday sharing from optional screen and storage access;
  unpairing requires confirmation.
- Activity exposes real transfer offers, progress, completion/failure, Accept/Decline/Cancel, and links to received
  files and Downloads. `LinkRepository.transferActivity` retains the latest 30 observed transfers for the current
  process only; it is explicitly a session list, not a persisted history. Notifications and their actions still work.
- `LinkRepository` reads `LinkClient.next_event()` for the life of the process, so `desktops` carries live connected
  flags and `incoming` every share; `ShareNotifier` turns each share into a notification (Open for a link, Copy for
  text). POST_NOTIFICATIONS is requested once a desktop is paired; the other grants (notification access, DND
  access, the battery-optimization exemption, and the call permissions) through the paged onboarding, each explained
  before Android asks.
- Presence: the phone is present while any of its activities is started (`ProcessLifecycleOwner`) and while "Stay
  connected" is on. That toggle runs `PresenceService`, a `connectedDevice` foreground service whose notification
  exists only while it runs; it holds no state and only lets the connection live in the background. The multicast
  lock is held while present, for the mDNS half of a redial.
- The share target (`ACTION_SEND`, `text/plain`) sends to the only paired desktop, or asks which; a single http or
  https URL goes as a link.
- The in-app picker (`files`): a sheet over Home with Photos and Videos from `MediaStore` (`READ_MEDIA_*`, or the
  Android 14 partial grant) and Files, the shared-storage tree under All files access (`MANAGE_EXTERNAL_STORAGE`; on
  Android 10 the legacy read grant). Home's recent-photos row and the sheet share one selection. Picked items go
  through the same `sendFiles` path as the share target: a MediaStore or `file:` URI opened as a descriptor.
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
  focused, offers it unless it came from a desktop, and finishes. Text stays inline; URI content, including screenshot
  images, is copied while the activity is focused into a private snapshot capped at 64 MiB, then offered with its MIME
  type through `offer_clip_file`. The core holds the snapshot open for lazy pulls after its path is removed. Desktop
  `ClipProvider` URIs are never offered back. Home's Clipboard action offers the clip the same way, so it lands on the
  desktop's clipboard rather than in a notification. The onboarding's last page, optional,
  walks through the two grants and copies the adb command; the watcher starts as soon as both are there. The same activity serves the
  quick-settings tile (`ClipboardTileService`) and "Send to desktop" in the text-selection menu (`PROCESS_TEXT`).
- E2E: `tests/e2e/link_android.sh` drives the Maestro flows under `link/android/maestro/` on an emulator against a
  private `umbriel-linkd`, writing screenshots and `results.json` to `artifacts/link-android/`;
  `link_android_features.sh` covers notifications, media, ring, and calls, with the `fixture` app as a stand-in media
  session and chat; `link_android_ui.sh` screenshots every screen in light and dark to `artifacts/link-android-ui/`.
