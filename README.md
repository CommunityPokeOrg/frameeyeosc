# frameeyeosc

Sends the Steam Frame's eye tracking (gaze and eye openness) to VRChat over OSC, as VRCFaceTracking-style avatar parameters. It runs on the headset as a background service and works with PC VRChat streamed through Steam Link. It can also send to VRCFaceTracking on the PC, so the eyes can be combined with other trackers.

[日本語版はこちら](README.ja.md)

This is a fork of [konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc). The Frame's public APIs only give you a combined gaze direction. konsti219 found that the eye tracker also measures how open each eye is and keeps it in an internal shared-memory object (`/dev/shm/eye-server.mmap`), and that is where this tool reads it from.

## What this fork adds

- It finds your PC on its own by sending to whichever PC Steam Link is streaming from. With the bundled wireless adapter that's the adapter's direct link, so your home network doesn't matter.
- Gaze and eyelids are smoothed with One Euro filters. A small deadzone keeps the eyes still while you fixate, and the gaze is held while your eyes are shut, because the Frame's gaze jumps as the eyes reopen.
- Both eyes share one gaze. On the Frame each eye's gaze wobbles on its own, which makes an avatar's eyes twitch. `--independent-eyes` switches back to per-eye gaze.
- Eyelid values are mapped onto the VRCFT scale (0 closed, 0.75 relaxed, 1 widened). A held-shut eye reads about 0.2 on the Frame, and a relaxed eye wanders between about 0.75 and 0.9.
- Eyelids calibrate themselves. It learns how far each of your eyes opens when relaxed, so if your face or the headset fit makes one eye look more open, the avatar still looks even. Winks still come through.
- It runs as a service that starts with SteamVR and restarts if it stops.
- Settings live in a file that is picked up while running, and an optional panel on the SteamVR dashboard changes them from inside the headset.
- It can send in the format the ETVR Tracking Module for VRCFaceTracking reads (see [VRCFaceTracking (ETVR) mode](#vrcfacetracking-etvr-mode)).

## Requirements

- A Steam Frame with Developer Mode on and SSH access (Settings > System > Developer Mode, then set a password under Developer). Choose a strong password: with SSH on, anyone on your network who knows it can log in to the headset.
- PC VRChat streamed with Steam Link, OSC enabled in VRChat (Action Menu > Options > OSC > Enabled).
- An avatar with VRCFaceTracking eye parameters (`FT/v2/EyeLeftX`, `EyeLidLeft`, ...) as floats. Avatars that pack parameters into binary bits are not supported when sending to VRChat directly.
- For the VRCFaceTracking (ETVR) mode: VRCFaceTracking on the PC with the ETVR Tracking Module.

## Install

Download the tarball from the releases page and copy it to the headset, for example from your PC:

```sh
scp frameeyeosc-*-steamframe-aarch64.tar.gz steamos@<headset-ip>:
```

Then on the headset (`ssh steamos@<headset-ip>`):

```sh
tar xzf frameeyeosc-*-steamframe-aarch64.tar.gz
cd frameeyeosc
./install.sh               # frameeyeosc only
./install.sh --with-panel  # frameeyeosc and the dashboard panel
```

No sudo is needed. Everything goes into your home directory (`~/.local/bin`, `~/.config`, `~/.local/share`), so SteamOS updates don't remove it. Run the same command again to update. Without `--with-panel` an installed panel is left as it is.

After that, turn off Steam Link's own OSC output on your PC (SteamVR settings > Steam Link > OSC). Steam Link sends its own unsmoothed eye data to VRChat, and with both running, two sources fight over the avatar's eyes. This is needed in the ETVR mode too, where VRCFaceTracking drives the avatar's eyes.

### Updating from a version before 0.4.0

Versions before 0.4.0 have no updater, so update to 0.4.0 once by hand: copy and unpack the new tarball as above and run `./install.sh --with-panel` (or `./install.sh` without the panel). Your `~/.config/frameeyeosc/env` and the learned eyelid calibration are kept, and the service restarts on the new version.

Options in `FRAMEEYEOSC_ARGS` in `env` still work as before. But anything set there is locked in the panel ("Locked by command line"). To change it from the panel, remove it from `env`, run `systemctl --user restart frameeyeosc`, and set the value again in the panel.

To remove it: `./install.sh --uninstall` (removes the panel too; add `--purge` to also delete settings and calibration).

### Updating from the panel (0.4.0 and later)

From 0.4.0 on, the panel's "Update" button does the update. The panel's Advanced page shows the installed version. At start and then at most once a day, the panel asks GitHub whether a newer release exists. That holds while checks succeed: after a failed check it tries again an hour later. "Check now" asks right away. When a newer release exists, "Update" downloads it, checks it against the release's `SHA256SUMS`, and runs its `install.sh` with the options of your last install (kept in `~/.config/frameeyeosc/install-args`). frameeyeosc and the panel restart on the new version. If anything fails before `install.sh` runs, nothing changes; the log is in `~/.cache/frameeyeosc/update.log`. Turn "Check for updates" off to stop the daily check (the "Check now" button still works). The update itself only runs when you press the button.

`SHA256SUMS` is a checksum file from the same release, not a signature. It catches a corrupted or incomplete download. It can't catch a release that was replaced on GitHub, because the checksum would be replaced along with it.

## Panel

`./install.sh --with-panel` adds an "Eye" panel to the SteamVR dashboard. It starts together with SteamVR from the next SteamVR start; to open it right away, pick "frameeyeosc panel" under Launch program (+) on the dashboard.

- The left column always shows what frameeyeosc is doing: sending or paused, where it sends to, messages per second, both eyelids and the gaze (raw and sent), and a config error if there is one.
- Basic: pause sending, VRChat or VRCFaceTracking (ETVR), target PC (automatic, or fixed to the PC it sends to now, so there's no IP to type in VR), port, language (Japanese / English), start with SteamVR, reset all, quit.
- Gaze: smoothing on or off, light / medium / strong presets and the three filter values, deadzone, holding the gaze while blinking, per-eye gaze, skipping unreliable gaze, removing one-sample glitches.
- Eyelids: auto calibration and its learned values, per-eye scales, the four openness marks drawn over each eye's live openness (blink and open wide to set them), left/right sync, keeping blinks visible (hold time and closing both eyes), eyelid smoothing.
- Advanced: parameter prefix, the version with checking for and installing updates, file locations, options locked by the command line.

The panel writes `config.json` and reads the status file. To pick its default language, it also reads the `language` line of Steam's `~/.steam/registry.vdf` once at startup (read only). For updates it runs `~/.local/share/frameeyeosc/frame-update.sh` (see above). Closing it, quitting it, or not installing it doesn't stop frameeyeosc. While it isn't open on the dashboard it draws nothing. Besides running the update check, the only thing it reads then is the update state file (`~/.cache/frameeyeosc/update-state.json`), about twice a second. Its "Start with SteamVR" switch enables or disables its systemd user unit (`frameeyeosc-panel.service`). Build notes and debugging options are in [panel/README.md](panel/README.md) (Japanese).

## Settings

Settings are in `~/.config/frameeyeosc/config.json`. The panel writes it, and you can also edit it by hand. frameeyeosc checks it once a second and applies changes without a restart. Missing keys use the defaults and unknown keys are ignored. If the file is broken or a value is out of range, frameeyeosc keeps the previous settings and reports the error (in the panel and in the status file).

```json
{ "output": "vrchat", "gaze_min_cutoff": 0.3, "lid_sync": 0.6 }
```

| Key | Option | Default | What it does |
|---|---|---|---|
| `sending` | | `true` | `false` pauses sending (in VRChat mode `EyeTrackingActive=false` is sent once) |
| `output` | `--output` | `"vrchat"` | `"vrchat"` sends avatar parameters to VRChat, `"etvr"` sends to VRCFaceTracking's ETVR Tracking Module |
| `host` | `--target` | `"auto"` | `"auto"` = the PC Steam Link is streaming from, else an IP address or host name without a port |
| `port` | `--port`, `--target` | `null` | `null` = 9000 for `vrchat`, 8889 for `etvr` |
| `prefix` | `--prefix` | `"/FT"` | Parameter name prefix; `""` for none |
| `raw` | `--raw` | `false` | No smoothing, and none of the time-based steps (glitch removal, gaze holding, the quality check, blink hold) |
| `gaze_min_cutoff` | `--gaze-min-cutoff` | `0.4` | Lower = steadier gaze at rest, more lag |
| `gaze_beta` | `--gaze-beta` | `0.8` | Higher = follows fast eye movements with less lag |
| `gaze_d_cutoff` | `--gaze-d-cutoff` | `0.5` | Lower = tracker noise loosens the gaze filter less |
| `gaze_deadzone` | `--gaze-deadzone` | `0.02` | Gaze changes smaller than this are ignored (1.0 = 45°) |
| `gaze_hold_below` | `--gaze-hold-below` | `0.5` | Hold the gaze while either eye's openness is below this; `0` turns it off |
| `independent_eyes` | `--independent-eyes` | `false` | Send each eye's own gaze instead of the shared one |
| `gaze_quality_limit` | `--gaze-quality-limit` | `0` (off) | Optional safety net: ignore an eye's gaze while the Frame's own uncertainty (covariance) for it is above this (for example `0.03`). The other eye moves both, and if both are above it the gaze is held. Eyelids aren't affected. On a well-fitted headset it made no measurable difference, because the uncertainty only rises while the eyes are mostly shut, where `gaze_hold_below` already holds the gaze |
| `despike` | `--no-despike` | `true` | Remove one-sample glitches in gaze and openness (median of 3 samples; everything arrives ~11 ms later) |
| `lid_min_cutoff` / `lid_beta` | `--lid-min-cutoff` / `--lid-beta` | `6.0` / `5.0` | Eyelid smoothing, the same way as for gaze |
| `lid_closed` / `lid_open` / `lid_widen_start` / `lid_wide` | `--lid-closed` ... | `0.30` / `0.80` / `0.92` / `1.00` | How Frame eye openness maps onto closed / relaxed / widened |
| `lid_scale_left` / `lid_scale_right` | `--lid-scale-left` / `--lid-scale-right` | `null` (learned) | Fixed per-eye multiplier instead of the learned one |
| `lid_calibration` | `--no-lid-calibration` | `true` | Learn eyelid calibration |
| `lid_sync` | `--lid-sync` | `0.4` | Evens out small left/right eyelid differences; larger ones (winks) pass through. `0` turns it off |
| `blink_hold_ms` | `--blink-hold-ms` | `80` | Once an eye is closed, it is sent fully closed for at least this long, so short blinks reach other players. `0` turns it off |
| `blink_sync_below` | `--blink-sync-below` | `0.35` | When one eye is closed and the other is below this (VRCFT scale), both are sent closed. Winks, with the other eye open, pass through. `0` turns it off |
| `calibration_reset` | | `0` | Increase it to make the eyelid calibration start over |
| `language` | | Steam's language | Panel language, `"ja"` or `"en"`. Without it, the panel is in Japanese if Steam is set to Japanese and in English otherwise |
| `update_check` | | `true` | The panel looks for a new release on GitHub at start and once a day (an hour later after a failed check). frameeyeosc itself ignores it |

Command-line options win over the file. They go in `~/.config/frameeyeosc/env` (then `systemctl --user restart frameeyeosc`):

```sh
FRAMEEYEOSC_ARGS="--gaze-min-cutoff 0.3 --lid-sync 0.6"
```

Whatever is set there can't be changed from the file, and the panel shows it as "Locked by command line". Run `~/.local/bin/frameeyeosc --help` for all options, including `--config` for another settings file.

## Status file

frameeyeosc writes what it is doing to `$XDG_RUNTIME_DIR/frameeyeosc/status.json` (usually `/run/user/1000/frameeyeosc/status.json`) ten times a second: whether it is sending, the destination, messages per second, the latest raw and sent values, the calibration, the settings in effect, which of them are locked by the command line, and any config error. The panel reads it. The folder is readable only by you, lives in memory, and is gone after a reboot. Only the latest values are kept.

## VRCFaceTracking (ETVR) mode

frameeyeosc can send in the format that the ETVR Tracking Module for VRCFaceTracking reads. VRCFaceTracking then drives the avatar, so the Frame's eyes can be combined with other trackers such as a mouth tracker. The ETVR Tracking Module is a third-party module ([EyeTrackVR/ETVRTrackingModule](https://github.com/EyeTrackVR/ETVRTrackingModule)); frameeyeosc is not part of it.

1. On the PC, install VRCFaceTracking and add the ETVR Tracking Module from its module registry. By default it listens on UDP 8889.
2. Switch the output to "VRCFaceTracking (ETVR)" in the panel, or set `"output": "etvr"` (or `--output etvr`). The destination works as usual (the Steam Link PC or a fixed host) on port 8889.

Notes:

- It sends six values: `EyeLeftX`, `EyeLeftY`, `EyeRightX`, `EyeRightY`, `EyeLidLeft`, `EyeLidRight`. `EyeX` / `EyeY` are left out, because receiving them puts the module into a single-eye mode that reads an eyelid value that isn't sent, and the eyelids freeze open.
- The module treats eyelid 1.0 as a relaxed open eye, so widened eyes don't come through in this mode (values stop at 1.0).
- The module smooths the eyelids itself. When you switch in the panel, it offers lighter eyelid smoothing on the frameeyeosc side. Because of that smoothing, a blink held closed for `blink_hold_ms` may not reach fully closed on the avatar; raise it (for example to 120) if short blinks still look half-closed.
- After VRCFaceTracking starts, its window can show "Not Responding" for close to two minutes while the module loads. It isn't broken; wait.
- The PC has to accept UDP 8889. VRCFaceTracking's ModuleProcess usually has an inbound firewall rule already.

## Calibration

Eyelid calibration is automatic. For the first 20 seconds after you put the headset on nothing is learned; after that each eye's relaxed openness is picked up within about 10 seconds and then follows slowly (the last ~10 minutes count most), so a short squint barely moves it. The result is saved every minute to `~/.config/frameeyeosc/calibration` and reused next time. To start over, press Reset in the panel (or increase `calibration_reset`).

## Troubleshooting

- Logs: `journalctl --user -u frameeyeosc -f` (the panel: `journalctl --user -u frameeyeosc-panel -f`)
- `No Steam Link connection found; waiting for one`: Steam Link isn't streaming yet, or set a fixed host.
- The log says `Sending OSC to ...` but the avatar doesn't react: check that OSC is enabled in VRChat, then check Windows Firewall. VRChat's own inbound rule is often allowed for the "Public" profile only, so OSC from a "Private" home network gets dropped. Note the rule must be for `VRChat.exe`, not `launch.exe`. A narrow rule that fixes it (PowerShell as administrator):
  ```powershell
  New-NetFirewallRule -DisplayName "VRChat OSC (LAN UDP 9000)" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 9000 -RemoteAddress LocalSubnet -Program "C:\Program Files (x86)\Steam\steamapps\common\VRChat\VRChat.exe" -Profile Private
  ```
  The bundled wireless adapter shows up in Windows as its own network, usually with the "Public" profile.
- `Error: ... No such file or directory` right after the headset boots: harmless. The eye tracker isn't up yet, and the service retries a few seconds later.
- Nothing moves while the headset is off your face: expected, the Frame only tracks while worn.
- The panel says "frameeyeosc is not running": check `systemctl --user status frameeyeosc`. Changes made in the panel are still saved and apply once it runs.

## Known issues

- Avatars that use binary (bit-packed) VRCFT parameters are not supported when sending to VRChat directly. In the ETVR mode, the avatar side is up to VRCFaceTracking.

## Privacy

- frameeyeosc sends gaze and eyelid values only to the destination above (your PC). It has no telemetry and doesn't talk to the internet.
- The panel asks GitHub (`api.github.com`) for the latest release at start and at most once a day (an hour after a failed check), unless "Check for updates" is off. Like any web request, this shows GitHub your IP address. Nothing else is sent, and downloads only come from GitHub.
- On disk it keeps:
  - your settings (`~/.config/frameeyeosc/config.json`) and two numbers, each eye's learned relaxed openness (`~/.config/frameeyeosc/calibration`)
  - from `install.sh`: the update script `~/.local/share/frameeyeosc/frame-update.sh` and your install options `~/.config/frameeyeosc/install-args`
  - from the update check and updates, in `~/.cache/frameeyeosc/`: `update-check.json` (GitHub's last answer), `update-state.json` (progress of the last update), `update.log` (log of the last update), the `update/` work folder (emptied after each run, except for the copy of the update script it keeps), and the `update.lock/` folder while a check or update runs

  No eye data is stored. The latest eye values are in the status file, which is in memory, readable only by you, and overwritten ten times a second; no history is kept.
- The OSC messages are unencrypted, so other devices on the same network could read them.

## Disclaimer

- Use at your own risk. The changes in this fork were made with Claude Opus 5.5, an AI model. I've tested them with unit tests and on my own Steam Frame, but I can't take responsibility for what happens on yours, so please read the code and check it yourself before you run it. The software comes with no warranty (see [LICENSE](LICENSE)).
- It reads the eye tracker's private, undocumented shared-memory layout (version 4). A SteamOS update can change that layout. If it does, the program stops with an "unsupported eye shared-memory version" error until frameeyeosc is updated.
- It needs no root and doesn't change any SteamOS files or settings. The only thing it writes is a "send me the next sample" flag in the eye tracker's shared memory, and it takes the lock there the same way the tracker's own clients do. The panel only writes frameeyeosc's settings file.
- Reading Valve's undocumented internal data may conflict with the Steam Subscriber Agreement, which restricts reverse engineering. Decide for yourself whether you're comfortable with that before using it.
- This is an unofficial project with no affiliation with or endorsement from Valve Corporation, VRChat Inc., the VRCFaceTracking project or the EyeTrackVR project. Steam, Steam Frame, SteamVR and Steam Link are trademarks of Valve Corporation, and VRChat is a trademark of VRChat Inc. The names are used here only to say what this works with.

## Development

Build and test on the headset (the binary must link against the headset's glibc, and the panel against SteamVR's OpenVR library; see `scripts/package.sh`):

```sh
cargo test --release
cmake -G Ninja -S panel -B panel/build && ninja -C panel/build
scripts/package.sh   # builds dist/frameeyeosc-<version>-steamframe-aarch64.tar.gz with both, and dist/SHA256SUMS
```

`vendor/frame-updater/` is a copy of the update checker shared by my Steam Frame apps. Don't edit it here: `scripts/package.sh` stops if it differs from what the copy's `MANIFEST.sha256` records.

To publish a release, attach both files. The panel's "Update" button refuses releases without `SHA256SUMS` and asks for a manual update instead:

```sh
gh release create v0.4.0 --title v0.4.0 --notes-file notes.md
gh release upload v0.4.0 dist/frameeyeosc-0.4.0-steamframe-aarch64.tar.gz dist/SHA256SUMS
```

To tune the eye processing against real data, record the eye tracker's raw samples (nothing is sent while recording, so it can run next to the service), then replay the file. The replay prints a few numbers for the current settings next to the same settings with the 0.4.0 steps turned off; settings come from `config.json` and options as usual. Recordings are personal data, so keep them out of the repository.

```sh
frameeyeosc --record ~/eyes.csv              # stop with Ctrl+C
frameeyeosc --replay ~/eyes.csv --blink-hold-ms 120 --replay-out ~/processed.csv   # also writes the processed values
```

## License

MIT. See [LICENSE](LICENSE); the original work is by konsti219. `vendor/frame-updater/` is not third-party code: it is sasaken1102r's own update checker, shared by their Steam Frame apps and copied here under this repository's MIT license. Licenses of the bundled Rust crates and of the OpenVR SDK header used by the panel are in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). Changes are listed in [CHANGELOG.md](CHANGELOG.md).

## Thanks

Thanks to konsti219 for frameeyeosc and for finding where the Frame keeps its eyelid data. This fork is built on that work.
