# Changelog

## 0.5.0 (unreleased)

Fitting the eyes to you, for when the avatar's eyes look a little off (for example looking too far down, or eyelids that close when you look down).

- The panel has a new "Eye fit" tab with one button. After you close the dashboard, a dot appears straight ahead, up, down, left and right, and then it asks you to close your eyes for a moment. It takes about 30 seconds. The result shows on the tab, with "Fit again" and "Reset". "Re-center only" measures straight ahead again after you put the headset back on. The values can be changed by hand under "Fine-tune". The dot is its own small overlay, fixed to the headset 2 m ahead. It only shows while the dashboard is closed, and it is redrawn every frame while it is up.
- Gaze: new settings for where "straight ahead" is and how far the gaze moves from there: `gaze_offset_x` / `gaze_offset_y`, and `gaze_gain_x`, `gaze_gain_up` and `gaze_gain_down`. They apply to both eyes and the combined gaze, before smoothing.
- Eyelids: each eye's openness with the eyes shut and while looking up, straight ahead and down (`lid_fit_*`). The Frame reads an eye as less open when you look down, so a fitted eye is judged against what is normal for where you look and no longer closes when you only look down. Fitted eyes don't use the learned calibration or `lid_scale_*`. On a recording, the times an eye looking down was sent a third closed went from 35 to 5, and more blinks were sent fully closed (53 of 60, from 50).
- Each eye's sideways gaze is fitted too (`gaze_offset_x_left/right`, `gaze_gain_x_left/right`), for "Move eyes separately". The fit aims each eye at the angle it really has to turn to see a dot 2 m away, using the distance between the eyes from SteamVR (63 mm if it doesn't say), so the avatar's eyes turn in naturally. The combined gaze keeps its own values. The "Jittery on the Frame" note on that switch is gone: each eye's gaze is only about 15% less steady than the combined one.
- Looking far down, the Frame's sideways gaze jumps (about 19° to the right from about 30° down), so the avatar looked down and to the right. Below `gaze_down_hold_x_deg` (28° by default; the tracker's own angle) the sideways gaze now fades into its value from just before, fully held 10° further down. On a recording, the sideways gaze sent while looking 32° or more down went from 10.1° to 2.7° (median). It's under "Fine-tune" on the Eye fit tab.
- frameeyeosc averages the raw gaze and each eye's openness for 2 seconds when the panel asks (`gaze_capture` in `config.json`), and reports the result in the status file and as one line in the journal.
- `--replay` also reports the eyelid while looking down and the times it was sent closing, the sideways gaze while looking far down, each eye's jitter and left minus right while fixating, and reads a lid fit from a recording that has one.
- With the defaults (nothing fitted), the output is the same as 0.4.0's, except while looking more than 28° down.

## 0.4.0 (2026-09-27)

Steadier eyes and fewer missed blinks. The ideas come from the README of [CyrusOtter/SteamFrameEye](https://github.com/CyrusOtter/SteamFrameEye).

- A closed eye is sent fully closed for at least 80 ms (`blink_hold_ms`), so short blinks show up for other players. Longer blinks keep their own length.
- One-sample glitches in gaze and eyelids are removed (`despike`). Everything arrives one sample (~11 ms) later.
- When one eye is closed and the other nearly so, both are sent closed (`blink_sync_below`). Winks pass through.
- Optional, off by default: an eye whose gaze the Frame itself marks as uncertain (`gaze_quality_limit`) can be kept from moving the gaze. The other eye moves both, or the gaze is held while both are uncertain, and eyelid calibration doesn't learn meanwhile. It's a safety net. On a well-fitted headset it measured no benefit, because the uncertainty only rises while the eyes are mostly shut, where the gaze is already held.
- The panel has the four new settings: the quality check and glitch removal on the Gaze page, and the blink hold and closing both eyes in one row on the Eyelids page.
- The gaze deadzone default is now 0.02 (0.9°) instead of 0.03 (1.35°), so small gaze shifts come through.
- Gaps in eye tracking are logged to the journal: one line when samples stop, with the reason (the eye server stopped producing, no new samples for 1 s, or an unreadable sample), and one when they come back ("Eye tracking resumed after 5.2 s"). When the eye tracker restarts and re-creates its shared memory, frameeyeosc reattaches and logs it. If the new one isn't ready yet, it keeps retrying instead of exiting.
- `--record FILE` writes the eye tracker's raw samples to a CSV file, and `--replay FILE` runs such a file through the processing and prints how the output behaved, next to the same settings with the steps above turned off.
- The panel shows the installed version on the Advanced page and can update frameeyeosc: it asks GitHub for a newer release at start and at most once a day ("Check for updates", `update_check`, on by default), "Check now" asks right away, and "Update" installs the new release after one confirmation. Downloads are checked against the release's `SHA256SUMS`, a checksum from the same release (not a signature) that catches a corrupted or incomplete download. Releases without it are left for a manual update. After a failed check, the panel tries again an hour later. While a new release is available, a notice at the bottom of the left column leads to it.
- `install.sh` also installs the update script (`~/.local/share/frameeyeosc/frame-update.sh`) and remembers its options in `~/.config/frameeyeosc/install-args`, so an update from the panel installs the same way. Versions before 0.4.0 have no updater, so update to 0.4.0 once by hand with `./install.sh --with-panel` as before. From 0.4.0 on, the panel's "Update" button does it. If `install-args` is missing, the panel's update installs with `--with-panel`.
- `scripts/package.sh` writes `dist/SHA256SUMS` next to the tarball; attach both to the release.

## 0.3.1 (2026-09-27)

- The panel now starts in English unless Steam is set to Japanese. It used to always start in Japanese. Once you pick a language in the panel, that choice is kept.

## 0.3.0 (2026-09-27)

- Settings can now live in `~/.config/frameeyeosc/config.json`. It is checked once a second and changes apply without a restart. If the file is broken, the previous settings stay in use. Options in `env` still win over the file.
- New settings panel for the SteamVR dashboard, `frameeyeosc-panel`. It changes every setting from inside the headset, in Japanese or English. Install it with `./install.sh --with-panel`. frameeyeosc keeps sending without it.
- `--output etvr` sends in the format VRCFaceTracking's ETVR Tracking Module reads (UDP 8889), so the eyes can go through VRCFaceTracking together with other trackers. Widened eyes don't come through in this mode.
- Sending can be paused, the learned eyelid calibration can be reset, and the parameter prefix can be turned off (`--prefix ""`).
- frameeyeosc writes what it is doing to `$XDG_RUNTIME_DIR/frameeyeosc/status.json` ten times a second. The panel reads it.
- Small changes from 0.2.0: a `--target` host name that doesn't resolve is retried every 5 seconds instead of stopping the program, and errors about bad options use the setting names (`lid_closed must be below lid_open`).

## 0.2.0 (2026-09-26)

First release of this fork, based on [konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc) at `b9f0c01`.

- OSC now goes to the PC that Steam Link is streaming from. `--target auto` is the new default; it used to be `127.0.0.1:9000`.
- Gaze and eyelids go through One Euro filters. Gaze also has a small deadzone and stays put while the eyes are shut.
- Both eyes get the same gaze by default. Use `--independent-eyes` for per-eye gaze.
- Eyelid openness is converted to the VRCFT scale (closed, relaxed, widened), with a deadzone so widening doesn't trigger too easily.
- Each eye's relaxed openness is learned and saved, so small left/right differences even out. Winks still come through.
- It can run as a systemd user service that starts with SteamVR and restarts if it fails. When the eye tracker restarts, it reopens the shared memory.
- Added `install.sh`, which needs no sudo, and `scripts/package.sh` for building release tarballs.
