# Principles

These rules apply to every change in both forks. Where upstream (`CONTRIBUTING.md` and `SCOPE.md` in Umbriel and
Noctalia) is stricter, upstream wins.

## Code

1. **YAGNI applies to code, not to features.** A feature on the roadmap is built fully. The code for it has no
   speculative options, no abstraction with one implementation, no configuration nobody asked for, no "future" hooks.
2. **Call native APIs directly.** A module talks to the kernel, the Wayland protocol, libinput, or the D-Bus interface
   that owns the data. No wrapper libraries, no shelling out to CLI tools (`hyprctl`, `nmcli`, `udisksctl`, `lpstat`).
   Every API used is listed in [native-apis.md](native-apis.md) before the code lands.
3. **One owner per piece of state.** Each setting has exactly one process that applies it and one file that persists
   it. Nobody else writes that file. The compositor owns outputs, input, and motion; the shell owns theme and UI state;
   system daemons own system state.
4. **Event-driven only.** Subscribe to signals, Wayland events, or inotify. A timer is allowed only when the source has
   no change notification, and the reason goes in a one-line comment.
5. **Mechanism and policy stay separate** (Umbriel's rule). The code that applies a mode is not the code that decides
   which mode to apply.
6. **No new threads.** The fork adds none. Blocking work is asynchronous D-Bus calls or fd polling through the
   existing main loop. The shell's upstream worker threads start on first use and stop when idle.
7. **Allocate on change, not per frame.** Nothing on the frame path allocates.
8. **Fail at the boundary.** Validate data where it enters (D-Bus replies, protocol events, config files). Inside, trust
   the types.
9. **Match the surrounding code.** Upstream naming, clang-format, file layout (one domain per directory, header beside
   source). Run `just format` and `just lint` before committing.
10. **Comments say only what the code cannot:** a constraint, an invariant, a workaround and its reason. No history,
    no banners, no restating the line.
11. No em dashes or double hyphens as punctuation in code, comments, docs, or commits (upstream rule).

## Tests

- End-to-end first. Compositor behaviour is proven with Umbriel's headless harness (`just check`), which boots a real
  instance per check and asserts with screenshots and `umbriel settle`. Each check leaves a repeatable artifact: its
  screenshot and pixel-probe result.
- Unit tests only for pure math and decisions (layout geometry, curve solving, config parsing), matching upstream's
  tiering.
- Performance claims are proven with [`tools/measure-idle.sh`](../tools/measure-idle.sh) results committed next to
  the change, measured on the same machine before and after.

## Forks

- **Upstream first.** Anything inside upstream `SCOPE.md` (bug fixes, protocol support real apps need, output
  management) goes upstream as a pull request. The fork carries only what upstream declines.
- **Fork code lives in its own files** with the smallest possible hook into upstream files, so a rebase touches a few
  lines.
- Rebase onto upstream weekly. A rebase that conflicts outside the hook lines means the hook is too wide.
