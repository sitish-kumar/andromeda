# Link: Rust conventions

Rules for every crate under `link/`. The repo-wide rules in `docs/principles.md` still apply (YAGNI, native APIs,
event-driven, fail at the boundary, comments only for what code cannot say, no em dashes or double hyphens as
punctuation). Where this file is more specific, it wins for `link/`.

## Layout

- Flat workspace: every crate in `link/crates/<name>`, one screen of crates, no nesting
  ([matklad](https://matklad.github.io/2021/08/22/large-rust-workspaces.html)). Crate names carry the `link-` prefix;
  directory name equals crate name.
- Binaries have product names (`umbriel-linkd`, `umbriel-link-phone`) set through `[[bin]]`; crate names stay `link-*`.
- One module per concern, file named after it. `mod.rs` is not used; `foo.rs` plus `foo/` when a module grows children.
- Versions, edition, lints, and every dependency are declared once in the root `Cargo.toml` (`[workspace.package]`,
  `[workspace.lints]`, `[workspace.dependencies]`); member manifests use `workspace = true`.

## Dependencies

- A dependency is added only when writing it ourselves would be larger or less safe (crypto, QUIC, TLS, D-Bus,
  mDNS, CBOR). Each one is listed with its reason in `ARCHITECTURE.md` in the same change.
- `default-features = false`, then enable only what is used.
- `cargo deny check` (licenses, advisories, bans, sources; `deny.toml`) passes before a commit. Duplicate versions
  are warnings to fix, not to ignore.
- One crypto provider: `ring`, through rustls and directly. No second hash or signature library. The one exception is
  AES-256-CBC for Quick Share's secure channel, which ring lacks: RustCrypto `aes` and `cbc`, used only in
  `link-quickshare`.

## Errors

- Library crates (`link-proto`, `link-core`, `link-ffi`) return `thiserror` enums, one per module whose callers
  branch on the failure. Variants carry the source with `#[source]` or `#[from]`; messages are lowercase, no
  trailing period.
- Binaries use `anyhow` with `.context("...")` at each boundary (file, socket, bus).
- No `unwrap` or `expect` on anything derived from the network, disk, or the bus. `expect("reason")` is allowed only
  for an invariant the code itself guarantees, and the reason states the invariant.
- Untrusted input never panics: sizes are checked before allocation, integer conversions use `try_from`.

## Async and concurrency

- `link-proto` is sans-IO: no sockets, no clocks, no runtime. It takes bytes and an `Instant` and returns bytes,
  events, and deadlines ([quinn-proto](https://lib.rs/crates/quinn-proto), [Firezone](https://www.firezone.dev/blog/sans-io)).
- Everything with I/O runs on tokio's current-thread runtime. The daemon adds no threads of its own; the only other
  thread is `mdns-sd`'s responder.
- Shared mutable state lives in one actor task that owns it and is reached through a cloneable handle wrapping an
  `mpsc::Sender`; replies travel on a `oneshot` in the message
  ([Actors with Tokio](https://ryhl.io/blog/actors-with-tokio/)). No `Arc<Mutex<_>>` around application state.
- Every spawned task has an owner that joins or aborts it (`JoinSet` in the owning actor). No detached `tokio::spawn`.
- Every network wait has a deadline (`tokio::time::timeout`) chosen by the protocol, not by the call site.

## Types and naming

- [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/checklist.html): RFC 430 casing, `as_`/`to_`/
  `into_` conversions, getters without `get_`.
- Newtypes for identifiers and secrets (`DeviceId`, `PairingCode`, `Spki`), never bare `String` or `Vec<u8>` across
  a module boundary. Secret types do not implement `Debug` with their contents.
- Wire structs live only in `link-proto::message`, derive `Serialize, Deserialize` with
  `#[serde(deny_unknown_fields)]`, and are converted to domain types at the boundary.
- Functions stay under about 60 lines and 4 parameters; past that, a struct or a smaller function.
- Early returns over nested `if`/`match`.

## Lints and formatting

- `rustfmt.toml`: `max_width = 120`, matching the C++ trees. `cargo fmt --check` is part of the build.
- `[workspace.lints]`: `unsafe_code = "forbid"` (except `link-ffi`, whose generated scaffolding needs it),
  clippy `pedantic` at warn with a short allow list, `unwrap_used` and `expect_used` at warn. `cargo clippy
  --all-targets -- -D warnings` passes before a commit.

## Logging

- The `log` facade in libraries; `env_logger` in binaries, without timestamps (journald adds them). `RUST_LOG`
  selects the level. Keys, codes, and message bodies are never logged; device ids and addresses may be.

## Tests

- End-to-end first: `tests/e2e/link_*.sh` runs real `umbriel-linkd` and `umbriel-link-phone` processes across network
  namespaces and writes artifacts to `artifacts/link-*/`.
- Isolated tests only for logic E2E cannot reach reliably, chiefly the `link-proto` state machines. The failure modes
  are written in `ARCHITECTURE.md` first, then the code, then one test per failure mode.
- No tests of constants, derives, or getters.

## Builds

- Heavy builds go through `tools/build.sh` (machine-wide lock): `tools/build.sh just link`.
