# Face unlock

Gaps 5.1 to 5.8. [Gaze](https://github.com/GunduLabs/gaze) (`gazed`, GPL-3.0, packaged as `gaze-bin`) decides "is
this the enrolled user"; the desktop only asks it and draws the prompt. We talk to it over D-Bus and PAM and never
copy its code, so its licence stays on its side of the process boundary.

## Why Gaze, not our own daemon

Measured on the reference machine (2026-09-30, `artifacts/face-gaze/`): 5 of 5 genuine attempts matched, median
1.0 s, with RGB and IR captured in parallel and liveness on. It already has what our own design listed: a system
daemon with per-user claims and peer-credential checks, direct V4L2 capture that refuses PipeWire and virtual
cameras, the strobing Windows Hello emitter, RGB anti-spoof (MiniFASNet) plus IR eye motion, TPM-sealed AES-256-GCM
templates, a watchdog on every stage, and `PAM_AUTHINFO_UNAVAIL` (not a failure) for dark rooms and no face. Howdy
has no liveness; visage and irlume trust IR alone, which a phone screen and a printed photo beat in their own tests.

The InsightFace models Gaze downloads on first start are for non-commercial use. We never ship them.

## Configuration we ship

`/etc/gaze/config.toml` on the reference machine:

| Key | Value | Why |
|---|---|---|
| `security.level` | `high` | ResNet50, threshold 0.5; RGB and IR must both match unless RGB is measured too dark |
| `cameras.rgb`, `cameras.ir` | `usb:3277:0055` | Resolved by VID:PID at runtime; `/dev/videoN` numbers move |
| `cameras.emitter_enabled` | `false` | The emitter strobes by firmware default |
| `cameras.parallel_capture` | `auto` | Both nodes stream at once on this camera (IR 15 fps, RGB 30 fps) |
| `auth.require_confirmation_elevation` | `true` | A face is passive; sudo and polkit need a deliberate confirm |
| `storage.encrypt_templates` | `true` | TPM-sealed key |
| `storage.unlock_gnome_keyring` | `false` | A face never releases a secret that needs the password |

## Surfaces

| Surface | Path | Verdict from |
|---|---|---|
| Lock screen | `shell/src/auth/face_authenticator.cpp` over `com.gundulabs.Gaze` (Claim, VerifyStartFor with service `umbriel-lock`, FaceStatus, VerifyStatus), beside the password field | `gazed` |
| polkit sheet | `pam_gaze.so` in `/etc/pam.d/polkit-1`; the shell registers `polkit-1` with `AddPamInternal`, so the agent gets `GAZE_*` markers and draws face state and a Confirm button | PAM |
| `run0`, `pkexec` | Through polkit, so the same sheet | PAM |
| `sudo` in a terminal | `pam_gaze.so` in `/etc/pam.d/sudo`; text prompt, Enter to confirm | PAM |
| SDDM | Password on the first login after boot (the keyring needs it) | PAM |
| Settings, Face Unlock page | EnrollStart, ListFaces, DeleteFace, VerifyStart over D-Bus | `gazed` |
| Apps and scripts | `org.umbriel.Auth1.Request(reason)` and `umbriel-auth`: the polkit sheet as a presence check, never a privilege grant | PAM |

## Lock screen failure cases (each has an E2E in `tests/e2e/face_lock.sh` against a mock `gazed`)

| # | Case | Required outcome |
|---|---|---|
| L1 | `gazed` not on the bus | No face prompt; password unlocks |
| L2 | No enrolled faces (`HasEnrolledFaces` false) | Never claims, so the camera stays dark |
| L3 | `VerifyStatus` `verify-match` | Unlocks |
| L4 | A judged no-match (a face was scored) | "Face not recognized"; re-arms, and after 3 judged misses stays off until the next lock |
| L5 | Ended without judging (no face, too dark, camera unused) | Shows the reason; re-arms on the next key press or pointer button, never in a loop |
| L6 | The user is typing a password | Face status never overwrites the prompt; submitting stops and releases the claim |
| L7 | `Claim` fails (camera busy, another claim) | Silent; password unlocks |
| L8 | `gazed` exits mid-verify | Treated as an unjudged end; the claim is dropped |
| L9 | Suspend mid-verify | Stops on `PrepareForSleep`; re-arms on resume |
| L10 | Unlocked by password | VerifyStop and Release; the camera goes dark |
| L11 | `lockscreen.face = false` | Never claims |

## polkit failure cases (`tests/e2e/face_polkit.sh`)

| # | Case | Required outcome |
|---|---|---|
| P1 | `GAZE_MSG_LOOK_CAMERA` or `GAZE_MSG_LOOK_OR_PASSWORD` | Sheet shows "Look at the camera", the password field stays usable |
| P2 | `GAZE_REQUIRE_CONFIRMATION` request | Sheet shows "Face verified" and a Confirm button; Confirm answers `GAZE_CONFIRMED`, Cancel answers `GAZE_CANCEL` |
| P3 | Typed password while confirmation is pending | Sent as the answer; Gaze falls back to it |
| P4 | `GAZE_MSG_FACE_NOT_RECOGNIZED`, `_NOT_DETECTED`, `_TOO_DARK`, `_TIMED_OUT`, `_UNAVAILABLE` | Readable reason, then the password prompt |
| P5 | An unknown `GAZE_*` marker | Never shown raw |
| P6 | `gazed` missing at shell start | No `AddPamInternal`; Gaze's English text is shown as before |

## Order of work

1. Lock screen (L1 to L11).
2. polkit sheet (P1 to P6), `AddPamInternal("polkit-1")` at shell start.
3. Settings Face Unlock page.
4. `org.umbriel.Auth1` and `umbriel-auth`.
5. SDDM theme, `pkg/PKGBUILD` dependency on `gaze-bin`, the shipped `/etc/gaze/config.toml`.
