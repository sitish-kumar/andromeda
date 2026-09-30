# Standards

How the desktop is built as one system while staying three processes. `principles.md` holds the code rules; this
file holds the structure, the boundary, and the pipeline that enforce them.

## Why three processes

Compositor, shell, and portal stay separate processes; they are one product in every other sense.

- **Crash radius.** A compositor crash ends every Wayland client. The shell is the largest, fastest-changing code
  (D-Bus, PAM, image decoding, network, scripting); in its own process it restarts in under a second and no app
  notices.
- **Frame deadline.** The compositor loop has 8.3 ms per frame at 120 Hz and must never block. Shell work (text
  layout, image decode, D-Bus replies, file parsing) would turn every stall into a dropped frame for every window.
- **Privilege.** The compositor holds DRM master and every keystroke. Parsers of untrusted data (notification images,
  tray icons, HTTP) stay out of that address space.
- **Upstream.** Both halves keep rebasing onto Umbriel and Noctalia. One process means owning 316k lines alone.

## One repo, one build, one release

```
desktop/
  compositor/   git subtree of the Umbriel fork   (remote: upstream umbriel)
  shell/        git subtree of the Noctalia fork  (remote: upstream noctalia)
  portal/       git subtree of xdg-desktop-portal-umbriel
  greeter/      git subtree of the noctalia-greeter fork (greetd login, face first)
  protocol/     desktop-unstable-v1.xml, the only copy
  session/      systemd units, portals.conf, tmpfiles.d, udev rules, session entry
  tests/e2e/    cross-process flows only
  tools/, bench/, docs/
```

- One `just check` builds all three, runs both upstream suites, the cross-process E2E, and the idle bench gate.
- One package set, built from one commit. The protocol version and the binaries never skew.
- Upstream work leaves through `git subtree split` into a clean branch per PR; upstream fixes arrive with
  `git subtree pull`.

## The contract

- `protocol/desktop-unstable-v1.xml` is the only channel between compositor and shell. Both sides generate from that
  one file.
- The compositor's JSON IPC and `noctalia msg` stay for scripts and the test harness. Production paths never use them
  and never spawn a process to talk to the other side. Keybinds that drive the shell become a `shell_action` event
  on the Wayland connection the shell already holds.
- Standard protocols first. A private request exists only where no standard one does, and it is proposed upstream as
  a standard when it is general.

## The boundary rule

| Belongs to | Anything that needs |
|---|---|
| Compositor | window pixels, input grabs, frame timing, output state: overview and switcher previews, surface motion, keybind dispatch, lock enforcement, input and output settings, DPMS |
| Shell | system services or its own UI: bar, panels, notifications, network, Bluetooth, audio, power policy, settings UI, theming |
| Portal | app-facing sandbox APIs: ScreenCast, Screenshot, Settings, and each further interface we implement |
| System daemons | system state: logind, timedated, localed, UDisks2, NetworkManager, BlueZ, UPower, PipeWire |

A feature that spans two owners is one protocol request, never two implementations. Each setting has one owner that
applies it and one generated file that persists it; a key set in two places is refused with a reason (the pattern
`displays.toml` and `input.toml` already follow).

## Native calls, not tools

Production paths call the owning API. Known spawns to replace, with their native path:

| Today | Native |
|---|---|
| `systemctl suspend/poweroff/reboot`, `loginctl` | `org.freedesktop.login1.Manager` `Suspend`, `Hibernate`, `SuspendThenHibernate`, `PowerOff`, `Reboot`, `Session.Terminate` |
| `gsettings set … color-scheme` | serve `org.freedesktop.impl.portal.Settings` from the shell; GSettings through GIO for GTK apps that ignore the portal |
| `fc-list` | fontconfig `FcFontList` (already linked) |
| `xdg-open` | `org.freedesktop.portal.OpenURI` or D-Bus activation of the default handler |
| `noctalia msg` from keybinds | `shell_action` protocol event |
| `umbriel msg dpms-off` for idle | `zwlr_output_power_management_v1` (add to Umbriel, upstream material) |
| template `apply.sh` hooks | stay, but only on explicit theme change, never on a hot path |

## Session

- `umbriel.service` starts the compositor. `noctalia.service` (`Restart=on-failure`, `RestartSec=0`,
  `PartOf=umbriel-session.target`) and `xdg-desktop-portal-umbriel.service` start from the session target, not from
  `general.autostart`.
- `XDG_CURRENT_DESKTOP` names the desktop; `session/<name>-portals.conf` routes every interface we implement to our
  backends and only the rest to `gtk`.
- The session carries no daemon we do not own or a daily app does not need.

## Pipeline

Every commit on the integration branch passes, in order:

1. Format and lint, each subtree with its upstream config.
2. Unit tests of both upstreams (pure math and parsing only, per their tiering).
3. Harness checks of both upstreams (headless, `settle`, no wall-clock sleeps).
4. Cross-process E2E in `tests/e2e/`, each leaving screenshots and a result file under `artifacts/`.
5. Idle gate: `tools/measure-idle.sh` on a headless session. Compositor and shell wakeups, thread count, and
   commits on a static screen must not exceed the last committed row in `bench/`.
6. Spawn and thread gate: a grep over the diff for `fork`, `exec`, `popen`, `posix_spawn`, `system(`,
   `std::thread`, `std::jthread`, `std::async`; any hit needs an entry in `tools/allowlist.txt` with its reason.

Nothing merges red. Performance numbers from the real laptop (battery, PSR, PC10) are recorded per change in
`bench/` as described in `power.md`.
