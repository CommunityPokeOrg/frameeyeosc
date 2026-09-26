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

- A Steam Frame with Developer Mode on and SSH access (Settings > System > Developer Mode, then set a password under Developer).
- PC VRChat streamed with Steam Link, OSC enabled in VRChat (Action Menu > Options > OSC > Enabled).
- An avatar with VRCFaceTracking eye parameters (`FT/v2/EyeLeftX`, `EyeLidLeft`, ...) as floats. Avatars that pack parameters into binary bits are not supported yet.

## Install

Copy the release tarball to the headset, then on the headset:

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
- Nothing moves while the headset is off your face: expected, the Frame only tracks while worn.

## Caveats

- This reads a **private, undocumented** shared-memory layout (version 4). A SteamOS update can change it; the program then refuses to start with an "unsupported eye shared-memory version" error until it is updated.
- Not affiliated with or endorsed by Valve.
- Eye data is sent unencrypted over your local network to your PC.

## Development

Build and test on the headset (the binary must link against the headset's glibc; see `scripts/package.sh`):

```sh
cargo test --release
scripts/package.sh   # builds dist/frameeyeosc-<version>-steamframe-aarch64.tar.gz
```

The changes in this fork were written with an AI assistant (Claude) and checked with unit tests and on a real Steam Frame.

## License

MIT. See [LICENSE](LICENSE); the original work is by konsti219.
