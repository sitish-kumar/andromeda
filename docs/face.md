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
| Greeter | SDDM today, password only. Face needs a greetd greeter (see Greeter) | PAM |
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

## The password is never blocked

Every surface accepts the password from the first moment, while the face check runs, and a submitted password ends the
face check. The lock screen does this itself (face runs over D-Bus beside the field). PAM surfaces need
`pam_gaze.so simultaneous` in `/etc/pam.d/polkit-1` and `/etc/pam.d/sudo`: the default sequential mode holds the
password prompt until the face check ends (up to 12 s).

## polkit failure cases (`tests/e2e/face_polkit.sh`, live: real polkit, PAM, gazed, and the user)

| # | Case | Required outcome | Checked |
|---|---|---|---|
| P1 | `GAZE_MSG_LOOK_CAMERA` or `GAZE_MSG_LOOK_OR_PASSWORD` | Face glyph and "Look at the camera"; with `simultaneous`, the password field is open beside it | Live, `look.png` |
| P2 | `GAZE_REQUIRE_CONFIRMATION` (as info while the password prompt is open, or as the prompt itself) | "Face verified", a Confirm button; Enter or Confirm answers `GAZE_CONFIRMED`; Cancel cancels the request | Live, `confirm.png`, pkexec exit 0 |
| P3 | A typed password while face runs or confirmation is pending | Sent as the answer; Gaze uses it as the password | Code path only: the test never submits the real password |
| P4 | `GAZE_MSG_FACE_NOT_RECOGNIZED`, `_NOT_DETECTED`, `_TOO_DARK`, `_TIMED_OUT`, `_UNAVAILABLE` | Readable reason with the error glyph, then the password prompt | Live once (liveness budget spent, "Face not recognized") |
| P5 | An unknown `GAZE_*` marker | Never shown raw | Code path |
| P6 | `gazed` missing at shell start, or restarted | `AddPamInternal("polkit-1")` runs whenever the name gains an owner; without it Gaze sends English text | Code path |

The shell under test registers for one process (`NOCTALIA_POLKIT_TEST_PROCESS`), since `pkexec` asks on behalf of its
parent; the user's own agent keeps the session. The test removes `polkit-1` from gazed's list on exit, or the user's
agent would receive markers.

## Greeter

SDDM cannot give this flow. Its theme API is one call, `login(user, password, session)`: PAM starts only after the
password is submitted, the theme never sees individual PAM messages, and a running attempt cannot be cancelled. Face at
SDDM is the fprintd workaround, an empty password submitted and then a wait. greetd's IPC (`create_session`,
`auth_message` per PAM message, `post_auth_message_response`, `cancel_session`) gives the greeter every message and
lets it cancel at any time, which is what face and password side by side need. The first login after boot still takes
the password, since the keyring needs it.

## Greeter failure cases (`tests/e2e/face_greeter.sh` against `mock_greetd.py`)

`greeter/` is the noctalia-greeter fork on greetd. It opens the PAM session as soon as a user is shown and registers
with gazed as the marker host for `gdm-face` (`AddPamInternal`). The stack is `pam_gaze simultaneous` (service
`gdm-face`, so Gaze hands the keyring password over), then `pam_unix`, then `pam_gnome_keyring use_authtok`. The
password prompt opens at once beside the face check. greetd carries one conversation with no message ids, so pam_gaze
cannot send "face matched" while that prompt is open; gazed sends the match signal to the registered host instead,
and the greeter answers the open prompt with `GAZE_CONFIRMED`, which pam_gaze accepts only after its own match. This
needs the Gaze change on `~/src/gaze` branch `feat/marker-host-race` (for upstream); with stock Gaze the stack must
stay sequential, and the greeter then queues a password typed during the face check.

| # | Case | Required outcome |
|---|---|---|
| G1 | Face matches, nothing typed | `start_session` without any password posted |
| G2 | Password typed and submitted while the face check runs | The field stays usable; the password is posted as soon as the secret prompt arrives, with no second Enter |
| G3 | Wrong password | The field clears, the session is cancelled, and a new one starts at once (face runs again) |
| G4 | No face enrolled (PAM goes straight to the password prompt) | Waits for the password like a plain greeter |
| G5 | Face gives up (dark, no face) | Gaze's reason is shown, then the password prompt; nothing is submitted for the user |
| G6 | Patched Gaze: gazed's match arrives while the password prompt is open | The greeter answers `GAZE_CONFIRMED` and the session starts |
| G7 | Patched Gaze: password typed at once | Posted at once, no wait for the face check |
| G8 | Taps anywhere (empty space, the hint line) before typing | Keys still reach the password field; nothing else takes text under Ryoku |
| G9 | Enter on an empty field | Nothing is sent: an empty answer is a failed password to PAM and faillock |
| G10 | A session picked from the session list | That session starts; with nothing chosen before, Umbriel |
| G11 | A face match while a list is open | The login waits until the list closes, so a pick is never overtaken |
| G12 | The face check gives up (no face, too dark) | gazed tells the host; the hint says so instead of asking for the camera |

With stock Gaze, switching user mid-check waits for greetd's reply (at most about 5 s): greetd reads one request at a
time. With the patch the prompt is already open, so a switch cancels at once.

## Order of work

1. Lock screen (L1 to L11).
2. polkit sheet (P1 to P6), `AddPamInternal("polkit-1")` whenever gazed appears; `simultaneous` in polkit-1 and sudo.
3. Settings Face Unlock page.
4. `org.umbriel.Auth1` and `umbriel-auth`.
5. greetd greeter, `pkg/PKGBUILD` dependency on `gaze-bin`, the shipped `/etc/gaze/config.toml` and PAM lines.
