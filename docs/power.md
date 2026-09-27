# Power

Goal: leave the lid open on a static screen and lose almost nothing, the way a MacBook does. The hardware sets the
floor (Arrow Lake-H 255H, Arc 140T, Samsung ATNA 2880x1800 120 Hz OLED); software decides how far above the floor the
machine idles. Every item below is a lever we own, ranked by the watts it can move. Each gets a before/after number in
`bench/` before it counts as done.

## What decides display-on idle power

1. **The panel.** On OLED, power follows emitted light: brightness times average picture level. A dark theme and a
   dark wallpaper are the single largest software lever, worth more than every other item combined at mid brightness.
2. **The display engine.** With Panel Self Refresh (PSR2 / Panel Replay) the panel refreshes from its own buffer, the
   eDP link and the display engine power down, and memory stops being read 120 times a second. PSR only engages when
   the compositor commits nothing. One blinking cursor or a seconds clock is enough to keep it off.
3. **Package C-states.** The SoC reaches PC10 only when the display is in PSR, no core is woken, and every device
   is runtime-suspended. Residency in PC10 is the number that separates 3 W from 8 W on this class of chip.
4. **Wakeups.** Every timer, in our processes, in system daemons, and in apps we drive, pulls the package out of
   PC10. Idle means zero wakeups from the desktop and none forced onto apps.
5. **Devices.** Audio DSP, Wi-Fi, Bluetooth, NVMe, USB, and the touchscreen controller must all be allowed to suspend.

## Current state (2026-09-26, Umbriel session, on AC)

| Lever | State | Verdict |
|---|---|---|
| PCIe ASPM | `powersupersave` via cmdline | Good |
| PCI / USB runtime PM | all `auto` | Good |
| NVMe APST | `default_ps_max_latency_us=100000` | Good |
| Wi-Fi power save | on | Good |
| Audio | SOF (`sof-audio-pci-intel-mtl`); suspend of idle PipeWire nodes unverified | Measure |
| Platform profile / EPP | `quiet` / `power` (power-saver even on AC) | Policy missing: nothing switches on AC change |
| Compositor idle wakeups | `m_backgroundFrameTimer` armed only while a hidden view wants frames (game content type or a `background_frames` rule) or has a configure pending | **Fixed** (harness check 637) |
| Apps out of sight | suspended and sent no frames on a hidden workspace, behind the lock screen, and on a powered-off output | **Fixed** (harness checks 637, 642) |
| xdg-shell `suspended` state (v6) | offered | **Fixed** |
| Overlay planes (`wlr_output_layer`) | absent: video is always GPU-composited | Gap |
| Third-party session daemons | `kded6`, `kdeconnect`, `gvfs-*`, `at-spi` running under Umbriel | Audit |
| PSR / PC10 residency | not measured (needs root) | Measure first |

## Baseline (2026-09-26, battery, static screen, Zen open on another workspace)

| Metric | Value |
|---|---|
| Battery draw | 3.52 W |
| Package | 2.43 W |
| Deepest package C-state | Pkg%pc8 3.9 % (no PC10 counter on Arrow Lake) |
| PSR2 residency | 96 % |
| System wakeups (powertop) | 636/s; umbriel 16/s, noctalia 3/s, Zen 113/s |
| Display commits on a static screen | 0.9/s (`intel_atomic_commit_work`, `tools/trace-kwork.sh`) |

The once-a-second commit was the bar: `Bar::onSecondTick` relayouts every bar each second, and `Flex::setPadding`
marked layout dirty without comparing values; system-monitor gauges also repainted on every sample. Both fixed in
the shell; `tests/e2e/idle_commits.sh` asserts at most one commit per 30 s on a static bar.

## Measurement (the only source of truth)

Every power claim is rows in `bench/idle.tsv` from `sudo tools/measure-idle.sh 60 <label>`. One run records, for
the compositor and the shell, memory, threads, CPU and wakeups, plus for the whole machine: mean battery draw,
package watts and the deepest package C-state residency turbostat can read (Arrow Lake exposes no Pkg%pc10 counter; `SYS%LPI` is the fallback), and the share of one-second samples in which the panel was
in self-refresh (i915 PSR status in debugfs). The panel supports PSR2 with selective fetch and no Panel Replay
(verified 2026-09-26), so PSR2 residency is the display metric.

Fixed conditions: battery, 2 min settle, brightness 30 %, static screen, same wallpaper, Wi-Fi associated, no
browser. Needs `turbostat` (`pacman -S turbostat`). Root is used only by this measurement, never by the session.

## Program, in order

1. **Baseline.** Record the table above on battery, on this session, before touching anything.
2. **Zero desktop wakeups.**
   - Compositor: arm the background frame timer only while a hidden view exists and a rule opts it in; default off.
   - Offer xdg-shell v6 and send `suspended` to toplevels on hidden workspaces, so apps stop drawing on their own.
     This is what GNOME and KDE do, and it is upstream material.
   - Done for the compositor: the timer and v6 (checks 637), and `suspended` also behind the lock screen and on a
     powered-off output, where a game keeps its slow tick (check 642).
   - Shell: find the 25 wakeups/s Noctalia does at idle; every periodic timer must justify itself or become an event.
   - Clock ticks once per minute, aligned to the minute, unless seconds are shown.
3. **Keep PSR on.** Assert zero commits over 60 s of a static screen in a harness check (frame counter via IPC) and
   in the idle bench (PSR exit count 0). Anything that animates at idle is a bug. Harness check 643: a panel, two
   windows after a focus change, and a game drawing on a hidden workspace make zero commits over 3 s. E2E
   `idle_commits.sh`: a clock-only bar and a gauge bar make zero commits over 30 s that hold no minute tick. The
   harness boots no shell, so the shell half is the E2E. Nothing was found animating at idle. The idle bench's PSR
   exit count needs root and is still open.
4. **Refresh policy.** Measure three idle strategies on battery with PSR on: fixed 120 Hz, VRR (panel minimum), and a
   60 Hz mode while nothing moves. i915 may refuse PSR with VRR on; pick the default from the numbers.
5. **Overlay planes.** Put fullscreen video and the cursor on KMS planes (`wlr_output_layer`) so playback does not
   wake the GPU for composition.
6. **Power policy owned by the desktop, no third-party daemon.**
   - On AC change (UPower `OnBattery` signal), the shell sets the profile through `power-profiles-daemon`
     (`[battery] profile_on_ac` / `profile_on_battery`; done, E2E `power_profile.sh`). The compositor dropping
     expensive effects on battery through the private protocol is still open.
   - Static kernel policy (ASPM, runtime PM, audio power save, USB autosuspend) ships as `tmpfiles.d` and `udev`
     rules in the `desktop` package. No TLP, auto-cpufreq, or powertop auto-tune: they fight each other and us.
     Written in `session/power/` (checked with `udevadm verify`, `systemd-tmpfiles --dry-run --create`, and
     `modprobe --showconfig -C`; not installed on this machine, whose cmdline and defaults already match):
     - `umbriel-power.tmpfiles` sets ASPM to `powersupersave` (L1 substates). Safe to ship: the kernel refuses the
       write when the firmware kept ASPM control (`_OSC`/FADT) or `pcie_aspm=off` was booted, and its per-device
       quirks keep ASPM off links known to break. A machine that misbehaves opts out with an empty
       `/etc/tmpfiles.d/umbriel-power.conf`.
     - `60-umbriel-power.rules` sets runtime PM `auto` on every PCI device (the SOF DSP runtime-suspends through it)
       except the drivers on TLP's deny list (`nouveau`, `radeon`, `mei_me`), and USB autosuspend on every device
       without a HID interface.
     - `umbriel-power.modprobe` gives `snd_hda_intel` `power_save=1` and `power_save_controller=Y`; SOF has no such
       parameter and needs none.
     - Still open: the `desktop-git` split in `pkg/PKGBUILD` that installs them to `/usr/lib/tmpfiles.d`,
       `/usr/lib/udev/rules.d`, and `/usr/lib/modprobe.d`.
7. **Quality of service, the macOS trick.** Arrow Lake-H has 2 LP-E cores on the SoC tile. Put background work
   (session daemons, indexers, apps on hidden workspaces) in a systemd slice with `AllowedCPUs=` on those cores and a
   low `CPUWeight=`. The compute tile can then stay power-gated while the desktop sits idle. On this machine the
   LP-E cores are CPUs 14 and 15 (2.5 GHz max; P-cores 0-5 at 5.1 GHz, E-cores 6-13 at 4.4 GHz). Confirm the gain
   with turbostat before shipping it, and check it does not fight `scx_lavd`.
   - Written: a drop-in for systemd's own user `background.slice` (`session/background.slice.d/`, installed by
     `umbriel-desktop-git`) adds `AllowedCPUs=14-15`; the slice already has `CPUWeight=30`. `umbriel-linkd` and the
     portal run in it (`Slice=background.slice`). The user manager delegates `cpuset` here, so the pin holds
     (`systemd-run --user --scope -p AllowedCPUs=14-15` sees `Cpus_allowed_list: 14-15`). Not installed or measured
     yet. A machine with other LP-E cores overrides the drop-in in `~/.config/systemd/user/background.slice.d/`.
   - Verify after installing: `systemctl --user show background.slice -p AllowedCPUs` says `14-15`;
     `grep Cpus_allowed_list /proc/$(pidof umbriel-linkd)/status` says `14-15`; then
     `sudo turbostat --quiet --show CPU,Busy%,Bzy_MHz,CPU%c6,CoreTmp,PkgWatt --interval 5` on a static screen shows
     the daemons' work only on CPUs 14 and 15, and a `tools/measure-idle.sh` row before and after gives the watts.
     `scx_lavd` schedules within each task's allowed CPUs, so the two only fight if lavd's own core compaction
     prefers the compute tile; its idle rows decide.
8. **Audio.** Verify WirePlumber suspends idle sinks (`session.suspend-timeout-seconds`) and that the SOF device
   runtime-suspends; notification sounds must not keep the DSP awake.
9. **Session diet.** Every daemon in the session is either owned by us, required by an app in daily use, or
   removed. `kded6` and `kdeconnect` are the first two to justify.

## Targets

Set after step 1 from the measured baseline, never guessed. The shape of the target:

| Metric | Target |
|---|---|
| Desktop wakeups at static idle (compositor + shell) | 0 per second |
| Wakeups forced onto apps on hidden workspaces | 0 |
| Compositor commits on a static screen | 0 over 60 s |
| PSR exits on a static screen | 0 over 60 s |
| Pkg%pc10 on a static screen | as high as the platform allows; the baseline sets the bar |
| Battery draw, static screen, 30 % brightness | baseline minus the measured win of each step, recorded per step |
