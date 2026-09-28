# Contributing to Andromeda

Andromeda is one repository: the compositor (`compositor/`), the shell (`shell/`), the portal (`portal/`), Link
(`link/`, including the Android app), and the protocols between them (`protocol/`). A change that crosses a boundary
changes both sides in one pull request.

Read [docs/principles.md](docs/principles.md) first; every change follows it. The short version: call native APIs
directly, stay event-driven, one owner per piece of state, no new threads, and prove behaviour end to end.

## Set up (Arch Linux)

```sh
git clone https://github.com/sitish-kumar/andromeda && cd andromeda
sudo pacman -S --needed just $(source pkg/PKGBUILD && echo "${makedepends[@]}")
just build
```

For the Android app, also the Android SDK with NDK 28.2.13676358, `rustup target add aarch64-linux-android
x86_64-linux-android`, and `cargo install cargo-ndk`; then `cd link/android && ./gradlew :app:assembleDebug`.

To run what you built as your desktop, `just package` builds the Arch packages from your checkout into `pkg/`;
install them with `sudo pacman -U pkg/*.pkg.tar.zst` and log in to Umbriel.

## Test

Behaviour is proven end to end, and every test leaves a repeatable artifact under `artifacts/`.

| Command | Runs |
|---|---|
| `just link` | Link's format, pedantic clippy, protocol tests, and supply-chain policy |
| `cd compositor && just check` | the compositor's headless harness, one real instance per check |
| `bash tests/e2e/<name>.sh` | one end-to-end test; each script's header says what it proves and what it writes |
| `just check` | all of the above |

Link's network tests run the phone and the desktop in separate network namespaces with no radios; the Android ones
need a running emulator. A new behaviour comes with a test that fails without it; unit tests are for pure math and
parsing only.

## Commits and pull requests

- One subject line in the form `area: what changed`, lower case, no trailing period: `link: ...`, `shell: ...`,
  `compositor: ...`, `pkg: ...`, `docs: ...`. The body says why, and which test proves it.
- `just format` (C++) and `cargo fmt` (Rust) before committing. CI runs only on `main`, so run `just link` and the
  tests your change touches before opening the pull request, and paste their results into it.
- Keep a pull request to one change. Update the document that describes what you changed (`link/ARCHITECTURE.md`,
  `docs/*.md`) in the same pull request.
- Changes to `protocol/` keep older peers working, or say in the pull request why they cannot.

## Where to start

[docs/gaps.md](docs/gaps.md) lists what is missing, by component and priority, and the order of work;
[docs/link-plan.md](docs/link-plan.md) covers Link. Open an issue before large work, so it can be placed.

## Syncing upstream

`compositor/`, `shell/`, and `portal/` are merged with upstream, not rebased, so a sync is one merge per directory:

```sh
git fetch https://github.com/noctalia-dev/umbriel main && git merge -X subtree=compositor FETCH_HEAD
git fetch https://github.com/noctalia-dev/noctalia main && git merge -X subtree=shell FETCH_HEAD
git fetch https://github.com/noctalia-dev/xdg-desktop-portal-umbriel main && git merge -X subtree=portal FETCH_HEAD
```

Resolve conflicts by porting our change to where upstream moved the code, then run `just build`, `cd compositor
&& just test && just check`, and the E2E tests the merged areas touch before committing.
