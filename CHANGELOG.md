# Changelog

## 0.2.0 (unreleased)

First release of this fork, based on [konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc) at `b9f0c01`.

- The OSC destination is found automatically: the PC that Steam Link is streaming from (`--target auto`, the new default).
- Smoothing: One Euro filters for gaze and eyelids, a gaze deadzone, and the gaze is held while the eyes are shut.
- Both eyes share one gaze by default (`--independent-eyes` restores per-eye gaze).
- Eyelid openness is mapped onto the VRCFT scale (closed / relaxed / widened), with a deadzone before widening.
- Eyelid auto-calibration learns each eye's relaxed openness and saves it; small left/right differences are evened out, winks are kept.
- Runs as a systemd user service that starts with SteamVR, reopens the shared memory when the eye tracker restarts, and restarts on failure.
- `install.sh` (no sudo) and `scripts/package.sh` for release tarballs.
