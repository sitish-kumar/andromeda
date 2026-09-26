# Performance

## Budgets

A change that breaks a budget does not merge. "Idle" means screen on, no input, nothing animating, no client
redrawing.

| Metric | Compositor | Shell | How measured |
|---|---|---|---|
| Frames committed at idle | 0 per second | n/a | PSR status reads active; see below |
| Wakeups at idle added by the fork | 0 over upstream | 0 over upstream | `tools/measure-idle.sh` |
| CPU at idle | ≤ 5 ‰ of a core | ≤ 5 ‰ of a core | `tools/measure-idle.sh` |
| Threads added by the fork | 0 | 0 | `Threads` in `/proc/<pid>/status` |
| Memory for a closed settings page | n/a | 0 (page state built on open, freed on close) | `RssAnon` before open and after close |
| Frame time during animation | < refresh interval at 120 Hz (8.3 ms) with blur on | same for shell content | `wlr_scene_output` commit duration (the scene API reports it) |
| Shell process during a compositor-driven panel animation | n/a | no wakeups for the whole animation | `wakeups_per_s` over the animation window |
| Fullscreen video, 4K, battery | overlay plane accepted, GPU composition off | n/a | `wlr_output_layer_state.accepted`, `battery_mw` |

## Baseline

Measured on this machine (Intel Arrow Lake-P, eDP 2880x1800 at 120 Hz, scale 1.5) on 2026-09-26, Hyprland 0.56.2
and Noctalia 5.1.0, on AC, with a terminal streaming output (so not a true idle). Raw rows: `bench/baseline.tsv`.

| Process | RSS | Anon | Threads | CPU ‰ | Wakeups/s |
|---|---|---|---|---|---|
| Hyprland | 254 MB | 88 MB | 16 | 4 | 8 |
| noctalia | 178 MB | 59 MB | 30 | 8 | 25 |

## Method

1. Run from a TTY or an idle session with nothing on screen changing. Close the terminal that started the run
   (`systemd-run --user --collect tools/measure-idle.sh …` keeps it alive), because a streaming terminal makes both
   processes redraw.
2. Measure each process for 60 s: `tools/measure-idle.sh <process> 60 <label> >> bench/<topic>.tsv`.
3. For battery numbers, unplug AC, wait 2 minutes for the power reading to settle, then measure.
4. Compare before and after on the same machine, same outputs, same brightness. Commit the TSV rows with the change.

PSR check (root): `cat /sys/kernel/debug/dri/0/i915_edp_psr_status`. With zero commits at idle it should report the
panel in a self-refresh state; any periodic commit shows up as PSR exiting.
