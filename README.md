# frameeyeosc

Sends the Steam Frame's eye tracking (gaze and eye openness) to VRChat over OSC, as VRCFaceTracking-style avatar parameters. It runs on the headset as a background service and works with PC VRChat streamed through Steam Link.

[日本語版はこちら](README.ja.md)

This is a fork of [konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc), which found that the Steam Frame tracks eye openness as well as gaze, and exposes it only through an internal shared-memory object (`/dev/shm/eye-server.mmap`), not through any public API.

## What this fork adds

- **Finds your PC by itself**: sends to the PC that Steam Link is streaming from. With the bundled wireless adapter this is the direct link, so your home network doesn't matter.
- **Smoothing**: One Euro filters, a small deadzone that keeps the eyes still while you fixate, and the gaze is held while your eyes are shut (the Frame's gaze jumps as the eyes reopen).
- **Both eyes share one gaze**: each eye's own gaze wobbles independently on the Frame, which looks like twitching eyes on an avatar. `--independent-eyes` restores per-eye gaze.
- **Eyelids on the VRCFT scale**: VRCFT expects 0 = closed, 0.75 = relaxed, 1 = widened. A held-shut eye reads about 0.2 on the Frame rather than 0, and a relaxed eye wanders between about 0.75 and 0.9; both are accounted for.
- **Eyelid auto-calibration**: learns how far each of your eyes opens when relaxed, so a face (or headset fit) that opens one eye more than the other still looks even. Winks are kept.
- **Runs as a service**: starts with SteamVR and restarts if it stops.

## Requirements

- A Steam Frame with Developer Mode on and SSH access (Settings > System > Developer Mode, then set a password under Developer). Choose a strong password: with SSH on, anyone on your network who knows it can log in to the headset.
- PC VRChat streamed with Steam Link, OSC enabled in VRChat (Action Menu > Options > OSC > Enabled).
- An avatar with VRCFaceTracking eye parameters (`FT/v2/EyeLeftX`, `EyeLidLeft`, ...) as floats. Avatars that pack parameters into binary bits are not supported yet.

## Install

Download the tarball from the releases page and copy it to the headset, for example from your PC:

```sh
scp frameeyeosc-*-steamframe-aarch64.tar.gz steamos@<headset-ip>:
```

Then on the headset (`ssh steamos@<headset-ip>`):

```sh
tar xzf frameeyeosc-*-steamframe-aarch64.tar.gz
cd frameeyeosc
./install.sh
```

No sudo is needed. Everything goes into your home directory (`~/.local/bin`, `~/.config`), so SteamOS updates don't remove it. Run the same command again to update.

**Then, on your PC, turn off Steam Link's own OSC output** (SteamVR settings > Steam Link > OSC). Steam Link sends its own, unsmoothed eye data to VRChat, and with both running the avatar's eyes get driven twice.

To remove it: `./install.sh --uninstall` (add `--purge` to also delete settings and calibration).

## Settings

Edit `~/.config/frameeyeosc/env`, then `systemctl --user restart frameeyeosc`. For example:

```sh
FRAMEEYEOSC_ARGS="--gaze-min-cutoff 0.3 --lid-sync 0.6"
```

| Option | Default | What it does |
|---|---|---|
| `--target HOST:PORT` | `auto` | Where to send. `auto` = the PC Steam Link is streaming from, on `--port` |
| `--port` | `9000` | Port used with `--target auto` |
| `--gaze-min-cutoff` | `0.4` | Lower = steadier gaze at rest, more lag |
| `--gaze-beta` | `0.8` | Higher = follows fast eye movements with less lag |
| `--gaze-deadzone` | `0.03` | Gaze changes smaller than this are ignored (1.0 = 45°) |
| `--independent-eyes` | off | Send each eye's own gaze instead of the shared one |
| `--lid-closed` / `--lid-open` / `--lid-widen-start` / `--lid-wide` | `0.30` / `0.80` / `0.92` / `1.00` | How Frame eye openness maps onto closed / relaxed / widened |
| `--lid-sync` | `0.4` | Evens out small left/right eyelid differences; larger ones (winks) pass through. `0` turns it off |
| `--lid-scale-left` / `--lid-scale-right` | learned | Fixed per-eye multiplier instead of the learned one |
| `--no-lid-calibration` | off | Stop learning eyelid calibration |
| `--raw` | off | No smoothing |

Run `~/.local/bin/frameeyeosc --help` for the full list.

## Calibration

Eyelid calibration is automatic. For the first 20 seconds after you put the headset on nothing is learned; after that each eye's relaxed openness is picked up within about 10 seconds and then follows slowly (the last ~10 minutes count most), so a short squint barely moves it. The result is saved every minute to `~/.config/frameeyeosc/calibration` and reused next time. Delete that file to start over.

## Troubleshooting

- Logs: `journalctl --user -u frameeyeosc -f`
- `No Steam Link connection found; waiting for one`: Steam Link isn't streaming yet, or use `--target` with your PC's address.
- The log says `Sending OSC to ...` but the avatar doesn't react: check that OSC is enabled in VRChat, then check Windows Firewall. VRChat's own inbound rule is often allowed for the "Public" profile only, so OSC from a "Private" home network gets dropped. Note the rule must be for `VRChat.exe`, not `launch.exe`. A narrow rule that fixes it (PowerShell as administrator):
  ```powershell
  New-NetFirewallRule -DisplayName "VRChat OSC (LAN UDP 9000)" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 9000 -RemoteAddress LocalSubnet -Program "C:\Program Files (x86)\Steam\steamapps\common\VRChat\VRChat.exe" -Profile Private
  ```
  The bundled wireless adapter shows up in Windows as its own network, usually with the "Public" profile.
- `Error: ... No such file or directory` right after the headset boots: harmless. The eye tracker isn't up yet, and the service retries a few seconds later.
- Nothing moves while the headset is off your face: expected, the Frame only tracks while worn.

## Known issues

- Avatars that use binary (bit-packed) VRCFT parameters are not supported yet.

## Privacy

- frameeyeosc sends gaze and eyelid values only to the destination above (your PC). It has no telemetry and doesn't talk to the internet.
- The only thing it stores is two numbers, each eye's learned relaxed openness, in `~/.config/frameeyeosc/calibration`. Eye data itself is never written to disk.
- The OSC messages are unencrypted, so other devices on the same network could read them.

## Disclaimer

- Use at your own risk. The software comes with no warranty (see [LICENSE](LICENSE)).
- This reads a **private, undocumented** shared-memory layout (version 4) of the headset's eye tracker. A SteamOS update can change it; the program then refuses to start with an "unsupported eye shared-memory version" error until it is updated.
- It doesn't modify SteamOS or any system files, and needs no root. It only accesses the eye tracker's shared memory the way a client of it does (taking its lock and asking for the next sample).
- This is an unofficial project, not affiliated with or endorsed by Valve Corporation or VRChat Inc. Steam, Steam Frame, SteamVR and Steam Link are trademarks of Valve Corporation; VRChat is a trademark of VRChat Inc. The names are used only to say what this works with.

## Development

Build and test on the headset (the binary must link against the headset's glibc; see `scripts/package.sh`):

```sh
cargo test --release
scripts/package.sh   # builds dist/frameeyeosc-<version>-steamframe-aarch64.tar.gz
```

The changes in this fork were written with an AI assistant (Claude) and checked with unit tests and on a real Steam Frame.

## License

MIT. See [LICENSE](LICENSE); the original work is by konsti219. Licenses of the bundled Rust crates are in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). Changes are listed in [CHANGELOG.md](CHANGELOG.md).
