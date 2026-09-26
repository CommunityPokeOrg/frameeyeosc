# Changelog

## 0.2.0 (unreleased)

First release of this fork, based on [konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc) at `b9f0c01`.

- OSC now goes to the PC that Steam Link is streaming from. `--target auto` is the new default; it used to be `127.0.0.1:9000`.
- Gaze and eyelids go through One Euro filters. Gaze also has a small deadzone and stays put while the eyes are shut.
- Both eyes get the same gaze by default. Use `--independent-eyes` for per-eye gaze.
- Eyelid openness is converted to the VRCFT scale (closed, relaxed, widened), with a deadzone so widening doesn't trigger too easily.
- Each eye's relaxed openness is learned and saved, so small left/right differences even out. Winks still come through.
- It can run as a systemd user service that starts with SteamVR and restarts if it fails. When the eye tracker restarts, it reopens the shared memory.
- Added `install.sh`, which needs no sudo, and `scripts/package.sh` for building release tarballs.
