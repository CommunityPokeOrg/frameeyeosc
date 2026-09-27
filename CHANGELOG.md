# Changelog

## 0.4.0 (unreleased)

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
