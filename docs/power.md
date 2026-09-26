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
| Compositor idle wakeups | `m_backgroundFrameTimer` fires at 10 Hz forever (`umbriel src/server/server.cpp:754`) | **Bug** |
| Apps on hidden workspaces | receive `wl_surface.frame` at 10 Hz from that timer, so they keep rendering | **Bug** |
| xdg-shell `suspended` state (v6) | not offered, xdg-shell capped at v3 | Gap |
| Overlay planes (`wlr_output_layer`) | absent: video is always GPU-composited | Gap |
| Third-party session daemons | `kded6`, `kdeconnect`, `gvfs-*`, `at-spi` running under Umbriel | Audit |
| PSR / PC10 residency | not measured (needs root) | Measure first |

## Measurement (the only source of truth)

Every power claim is one row in `bench/*.tsv` from `tools/measure-idle.sh`, extended to record, per run:

- `turbostat --quiet --interval 60 --show PkgWatt,CorWatt,GFXWatt,Pkg%pc2,Pkg%pc6,Pkg%pc8,Pkg%pc10` (RAPL + residency)
- `/sys/kernel/debug/dri/0/i915_edp_psr_status` sampled before and after (PSR state and exit count)
- `/sys/kernel/debug/pmc_core/slp_s0_residency_usec` delta (whole-SoC low-power residency)
- `BAT0/power_now` mean on battery (the number the user feels)
- per-process wakeups for umbriel, noctalia, and every other process with more than 1 wakeup/s (`powertop --csv`)

Fixed conditions: battery, 2 min settle, brightness 30 %, static screen, same wallpaper, Wi-Fi associated, no
browser. Root reads go through one `sudo` wrapper in `tools/`; nothing root runs in the session.

## Program, in order

1. **Baseline.** Record the table above on battery, on this session, before touching anything.
2. **Zero desktop wakeups.**
   - Compositor: arm the background frame timer only while a hidden view exists and a rule opts it in; default off.
   - Offer xdg-shell v6 and send `suspended` to toplevels on hidden workspaces, so apps stop drawing on their own.
     This is what GNOME and KDE do, and it is upstream material.
   - Shell: find the 25 wakeups/s Noctalia does at idle; every periodic timer must justify itself or become an event.
   - Clock ticks once per minute, aligned to the minute, unless seconds are shown.
3. **Keep PSR on.** Assert zero commits over 60 s of a static screen in a harness check (frame counter via IPC) and
   in the idle bench (PSR exit count 0). Anything that animates at idle is a bug.
4. **Refresh policy.** Measure three idle strategies on battery with PSR on: fixed 120 Hz, VRR (panel minimum), and a
   60 Hz mode while nothing moves. i915 may refuse PSR with VRR on; pick the default from the numbers.
5. **Overlay planes.** Put fullscreen video and the cursor on KMS planes (`wlr_output_layer`) so playback does not
   wake the GPU for composition.
6. **Power policy owned by the desktop, no third-party daemon.**
   - On AC change (UPower `OnBattery` signal), the shell sets the profile through `power-profiles-daemon`
     (`net.hadess.PowerProfiles`), and the compositor drops expensive effects (blur recompute, shadows) through the
     private protocol.
   - Static kernel policy (ASPM, runtime PM, audio power save, USB autosuspend) ships as `tmpfiles.d` and `udev`
     rules in the `desktop` package. No TLP, auto-cpufreq, or powertop auto-tune: they fight each other and us.
7. **Quality of service, the macOS trick.** Arrow Lake-H has 2 LP-E cores on the SoC tile. Put background work
   (session daemons, indexers, apps on hidden workspaces) in a systemd slice with `AllowedCPUs=` on those cores and a
   low `CPUWeight=`. The compute tile can then stay power-gated while the desktop sits idle. On this machine the
   LP-E cores are CPUs 14 and 15 (2.5 GHz max; P-cores 0-5 at 5.1 GHz, E-cores 6-13 at 4.4 GHz). Confirm the gain
   with turbostat before shipping it, and check it does not fight `scx_lavd`.
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
