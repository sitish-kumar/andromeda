# xdg-desktop-portal-umbriel

An [xdg-desktop-portal](https://github.com/flatpak/xdg-desktop-portal) backend for the [Umbriel](https://github.com/noctalia-dev/umbriel) compositor.

## Supported interfaces

- `org.freedesktop.impl.portal.ScreenCast`
- `org.freedesktop.impl.portal.Screenshot`

## Building

Requires Meson >= 1.3 and a C++23 compiler.

### Dependencies

- sdbus-c++ >= 2.0
- libpipewire-0.3
- wayland-client
- wayland-protocols >= 1.39
- libdrm
- gbm
- cairo
- tomlplusplus
- GTK4 (optional, for the share picker)

### With just

```sh
just configure
just build
```

Modes: `debug` (default), `release`, `asan`.

```sh
just build release
just install        # release build + sudo meson install
```

### Manual

```sh
meson setup build
meson compile -C build
sudo meson install -C build
```

### Nix

```sh
nix build
```

A dev shell is also available via `nix develop`.

## Configuration

The config file lives at `$XDG_CONFIG_HOME/xdg-desktop-portal-umbriel/config.toml` (or the system-installed default).

```toml
[screencast]
chooser_cmd = "/usr/local/libexec/umbriel-share-picker"
max_fps = 0

[screenshot]
cmd = ""
color_pick_cmd = ""
```

## License

MIT License. See [LICENSE](LICENSE) for details.
