# noctalia-pywalfox

Pywalfox-compatible native messaging host shipped with Noctalia. Replaces the Python `pywalfox` daemon while keeping
the [Pywalfox](https://addons.mozilla.org/en-US/firefox/addon/pywalfox/) browser extension.

## Clone this branch

```sh
git clone -b feat/noctalia-pywalfox-host --single-branch https://github.com/noctalia-dev/noctalia.git
cd noctalia
```

If you already have a clone:

```sh
git fetch origin feat/noctalia-pywalfox-host
git checkout feat/noctalia-pywalfox-host
```

## Build

Built with the main project:

```sh
just configure
just build
# binary: build-debug/noctalia-pywalfox

just configure release
just build release
# binary: build-release/noctalia-pywalfox
```

`just install` / `just install release` installs `noctalia-pywalfox` next to `noctalia`.

## Quick test (no system install)

```sh
sudo ./build-debug/noctalia-pywalfox install
# restart Firefox
# install Pywalfox extension if needed
# enable community template "pywalfox" in Noctalia
# or write ~/.cache/wal/colors.json and run:
./build-debug/noctalia-pywalfox update
```
