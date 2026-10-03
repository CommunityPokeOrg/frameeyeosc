# Extensions API

How separate apps on the Steam Frame find each other and exchange messages — for example a
microphone tuner telling a perf overlay it is listening, or another app asking frameeyeosc
for the current gaze. There is no broker process and no shared library to link against:
the bus is a folder of files in `$XDG_RUNTIME_DIR`, and a message is one Unix datagram.
The whole thing is language-agnostic; a Rust implementation lives in `src/extensions.rs`
(frameeyeosc's library target), and `contrib/ext-echo.py` is a complete Python example.

Everything below is the wire protocol, so apps can be written in anything.

## The bus folder

`$XDG_RUNTIME_DIR/frame-apps/` (when `XDG_RUNTIME_DIR` is unset, `/run/user/<uid>/frame-apps/`).
`XDG_RUNTIME_DIR` is per-user, in memory and mode 0700, so messages never leave the device,
other users can't see them, and a reboot clears the folder. Apps create it with mode 0700
if it is missing.

One app is two files, named after its registered name:

```text
frame-apps/
    frameeyeosc.json     its descriptor
    frameeyeosc.sock     its inbox (a Unix datagram socket, SOCK_DGRAM)
    frame-mic-tuner.json
    frame-mic-tuner.sock
```

A **name** is lowercase ASCII letters, digits, `-` and `_`, starting with a letter or digit,
at most 32 characters. Because the name is also a file name, nothing else is allowed.

## Registering

An app joins the bus by binding `<name>.sock` and writing `<name>.json`:

```json
{
  "v": 1,
  "name": "frameeyeosc",
  "pid": 8791,
  "version": "0.7.0",
  "description": "Steam Frame eye tracking sent to VRChat over OSC",
  "started": 1791012944.04,
  "updated": 1791012954.079,
  "accepts": ["ping", "get-status", "get-eyes"]
}
```

- `v` is the protocol version (`1`).
- `pid` is the app's process id.
- `version`, `description` are optional; `accepts` lists the message kinds the app answers
  (informational only — it helps senders pick a kind, it does not filter anything).
- `started` / `updated` are Unix seconds (floats, milliseconds are enough).

Write the descriptor atomically — write `<name>.tmp`, then rename it over `<name>.json` —
so readers never see a half-written file (the same way `status.json` is written).

Registering fails while a **live** app already holds the name; a stale registration
(dead pid or heartbeat older than 15 s) is taken over: delete the old `<name>.sock` and
`<name>.json` and proceed.

## Staying registered

Rewrite the descriptor at least every **5 seconds**, refreshing `updated` (and anything else
that changed). This is the heartbeat. A descriptor counts as **live** while `updated` is no
older than **15 seconds** *and* the pid is still running (`kill(pid, 0)`; on Linux, reading
`/proc/<pid>` works too). Dead or expired descriptors are ignored by discovery, and the next
app with that name reclaims it — a crashed app leaves no residue that blocks a restart.

## Discovering other apps

List the bus folder, read every `*.json`, keep the live ones. That is all: no socket is
needed to see who is around, and reading files is safe mid-write because descriptors are
replaced atomically.

## Sending a message

A message is one datagram sent to the peer's `<name>.sock`, at most **8192 bytes** of UTF-8
JSON:

```json
{"v": 1, "from": "frame-mic-tuner", "kind": "ping", "data": {"t": 1791012945.5}}
```

- `from` is the sender's registered name; a reply is sent back to that name's inbox.
- `kind` says what the message is (`"ping"`, `"get-status"`, anything the two apps agree on).
- `data` is any JSON value (`null` for none).

Sending is **fire-and-forget**: the datagram either lands in the peer's socket queue or is
gone — a peer that has exited, is busy or never reads drops it. `sendto` fails with ENOENT
when the peer's socket file is missing. Apps that want replies must register an inbox of
their own; a fire-and-forget sender can use an unbound socket and any valid `from` name.

Bigger payloads don't belong in a datagram: write them to a file in the bus folder (or
elsewhere under `$XDG_RUNTIME_DIR`) and send its path.

## Replies and conventions

Replies are ordinary messages to `from`. The reply's `kind` is up to the two apps; the
convention used here is a noun for the answer (`"pong"` for `"ping"`, `"status"` for
`"get-status"`) and `"error"` with `data.reason` for a kind the app doesn't know — replying
with an error instead of staying silent lets a sender tell "no such kind" from "app gone".

## frameeyeosc on the bus

While the `extensions` setting is on (default; `--no-extensions` or `"extensions": false`
turns it off), frameeyeosc registers as `frameeyeosc` and answers:

| kind | reply kind | reply `data` |
|---|---|---|
| `ping` | `pong` | `{"time": <unix>}` |
| `get-status` | `status` | sending, output, tracking, target, rate, tracker_rate, openness_saturated, config_error, time |
| `get-eyes` | `eyes` | the latest sample as sent: `gaze` / `gaze_left` / `gaze_right` (1.0 = 45°), `lids` (output scale), `lids_vrcft`, `openness` (raw Frame); `{"tracking": false}` without eye data |
| anything else | `error` | `{"reason": "unknown kind ..."}` |

For streams (eye data at 90 Hz rather than on request), send a subscription message and let
the answering app datagram values back — the dots socket (`gaze-dots.sock`, see
`src/dots.rs`) is an example of that pattern with a binary packet.

## Using the API

### From Rust

`frameeyeosc`'s library target exposes `frameeyeosc::extensions`:

```rust
use frameeyeosc::extensions::{discover, bus_dir, Extension, Registration};

let mut app = Extension::register("my-app", Registration {
    version: Some(env!("CARGO_PKG_VERSION").into()),
    description: Some("my overlay".into()),
    accepts: vec!["ping".into()],
})?;

// In the app's main loop:
for message in app.poll() {           // poll() also keeps the heartbeat fresh
    if message.kind == "ping" {
        app.reply(&message, "pong", serde_json::json!({}))?;
    }
}
let peers = discover(&bus_dir());     // who else is on the bus
app.send("frameeyeosc", "get-eyes", serde_json::Value::Null)?;
```

`extensions::request()` is a helper for one-shot request/response (it registers a temporary
name, sends, waits for one reply). Dropping the `Extension` unregisters the app.

### From other languages

Any language that can bind a Unix datagram socket and write JSON files can join the bus —
the whole protocol is the two files and the envelope above. `contrib/ext-echo.py` is a
complete example in Python (stdlib only): run it, then in another terminal
`frameeyeosc-ext ping ext-echo-py`.

### The frameeyeosc-ext tool

`cargo build --release --bin frameeyeosc-ext` builds a small tool that doubles as a usage
example (it is a development helper; `install.sh` doesn't install it):

```sh
frameeyeosc-ext list                          # who is on the bus and what they accept
frameeyeosc-ext send frame-mic-tuner mute '{}'    # fire-and-forget
frameeyeosc-ext ask frameeyeosc get-status    # send and print the first reply (2 s)
frameeyeosc-ext ping frameeyeosc              # shorthand for ask ... ping
frameeyeosc-ext echo                          # register as ext-echo and echo everything
```

`--bus PATH` overrides the bus folder (used in tests).

## Notes and limits

- The bus is trusted within the user account: any process running as you can register any
  name and send to any inbox. On the Frame everything user-installed runs as `steamos`, so
  treat message contents as untrusted input anyway — validate kinds and payloads.
- There is no ordering or delivery guarantee; design kinds to be idempotent, and for values
  that change constantly send the newest rather than queueing.
- Discovery is pull-based (re-read the folder when you need it). If an app wants to be
  notified of arrivals, it can watch the folder with inotify, or simply rescan on demand —
  descriptors are small.
