# Link: architecture

The code behind `docs/continuity.md`. That file says what Link does and why; this one says how the code is cut.
Conventions are in `CONVENTIONS.md`.

## Crates

```
link/crates/
  link-proto   sans-IO: wire messages, framing, pairing and session state machines. No I/O, no clock, no runtime.
  link-core    the engine: identity and store, TLS/QUIC transport, mDNS, the session actor. Used by all three below.
  link-daemon  bin umbriel-linkd: D-Bus org.umbriel.Link1, systemd service, pairing window, device registry.
  link-phone   bin umbriel-link-phone: the headless phone for E2E tests, and the transcript schema check.
  link-ffi     UniFFI bindings of link-core for the Android app (phase 2).
link/android/  the Android app (phase 2), Kotlin + Compose over link-ffi.
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

## Threads

`umbriel-linkd` runs one tokio current-thread runtime (QUIC, D-Bus, timers, the store) plus `mdns-sd`'s responder
thread while it advertises. Nothing else.

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

### Discovery

- The desktop advertises `_umbriel-link._udp.local.` only while it has a paired device or an open window, so an
  unpaired desktop is invisible: instance name = `DeviceId`, TXT `v=1`, plus `pair=1` while a window is open. No
  device name.
- The phone dials, in order: last-known addresses (most recent success first), then mDNS results for the paired
  desktop's `DeviceId`. The first handshake that verifies wins.
- The desktop's QUIC port is chosen at random on first start and kept in the store, so last-known addresses stay valid
  across restarts.

## D-Bus: `org.umbriel.Link1`

Session bus, object `/org/umbriel/Link1`. Contract file: `protocol/link-v1/org.umbriel.Link1.xml`; the shell's
client is written against it.

| Member | Signature | Meaning |
|---|---|---|
| method `StartPairing` | `() → (s code, s uri)` | Opens the 120 s window; a new call replaces the old window |
| method `CancelPairing` | `()` | Closes the window |
| method `Unpair` | `(s device_id)` | Forgets the device; it is told at next contact |
| property `Devices` | `a(ssb)` | `(device_id, name, connected)`, with `PropertiesChanged` |
| property `Pairing` | `b` | Whether a window is open |
| signal `PairingFinished` | `(s device_id, s name)` | A device was paired |
| signal `PairingFailed` | `(s reason)` | The window's attempt failed |

## Android app (phase 2)

Kotlin, Jetpack Compose, one Gradle project under `link/android/`:
- Modules: `app` (Compose UI and Android services), `core` (domain and data over `link-ffi`). Dependency direction
  Presentation → Domain → Data; the domain layer imports neither.
- Feature-first packages under `app`: `pairing`, `devices`, `share`, `clipboard`, `notifications`.
- One `ViewModel` per screen exposing `StateFlow`; UI actions return `Result`, never throw into the UI. Coroutines
  only, no callbacks above the data layer. Manual constructor injection from one `AppContainer`, no DI framework.
- E2E with Maestro flows under `link/android/maestro/`, each writing screenshots and a JSON result to
  `artifacts/link-android-*/`.
