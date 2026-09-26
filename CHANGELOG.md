# Changelog

## 0.3.0 (unreleased)

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
