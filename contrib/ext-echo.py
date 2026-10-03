#!/usr/bin/env python3
"""A complete extensions-bus app in Python (stdlib only): registers as ``ext-echo-py``,
prints every message it gets, and replies ``pong`` to ``ping`` and ``echo`` to the rest.

Try it with the Rust tool:

    python3 contrib/ext-echo.py &
    frameeyeosc-ext list
    frameeyeosc-ext ping ext-echo-py
    frameeyeosc-ext send ext-echo-py note '{"text": "hi"}'

Stop with Ctrl+C; the name is unregistered on the way out. docs/extensions.md describes
the whole protocol: this file follows it field for field, so it doubles as the reference
for apps in any language.
"""

import json
import os
import signal
import socket
import time

NAME = "ext-echo-py"
PROTOCOL_VERSION = 1
MAX_MESSAGE = 8 * 1024
HEARTBEAT = 5.0  # rewrite the descriptor at least this often
STALE_AFTER = 15.0  # older than this (or a dead pid) counts as gone


def bus_dir() -> str:
    base = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return os.path.join(base, "frame-apps")


def descriptor_path(bus: str, name: str) -> str:
    return os.path.join(bus, f"{name}.json")


def socket_path(bus: str, name: str) -> str:
    return os.path.join(bus, f"{name}.sock")


def live(descriptor: dict) -> bool:
    """A descriptor counts as live while its heartbeat is fresh and its pid runs."""
    if descriptor.get("v") != PROTOCOL_VERSION:
        return False
    if time.time() - descriptor.get("updated", 0) > STALE_AFTER:
        return False
    try:
        os.kill(descriptor["pid"], 0)
    except (ProcessLookupError, KeyError):
        return False
    except PermissionError:
        pass  # alive but not ours
    return True


def write_descriptor(bus: str, started: float):
    """Replace <name>.json atomically: write a .tmp file, then rename over."""
    descriptor = {
        "v": PROTOCOL_VERSION,
        "name": NAME,
        "pid": os.getpid(),
        "version": "1.0",
        "description": "extensions bus echo example (contrib/ext-echo.py)",
        "started": started,
        "updated": time.time(),
        "accepts": ["ping", "echo"],
    }
    tmp = descriptor_path(bus, NAME) + ".tmp"
    with open(tmp, "w") as file:
        json.dump(descriptor, file)
    os.rename(tmp, descriptor_path(bus, NAME))


def discover(bus: str):
    """The live extensions on the bus, as (name, descriptor) pairs."""
    peers = []
    for entry in os.listdir(bus) if os.path.isdir(bus) else []:
        if not entry.endswith(".json"):
            continue
        try:
            with open(os.path.join(bus, entry)) as file:
                descriptor = json.load(file)
        except (OSError, json.JSONDecodeError):
            continue
        if live(descriptor):
            peers.append(descriptor)
    return peers


def send(bus: str, sock: socket.socket, to: str, kind: str, data):
    """One message = one datagram to the peer's <name>.sock. Fire-and-forget."""
    envelope = {"v": PROTOCOL_VERSION, "from": NAME, "kind": kind, "data": data}
    payload = json.dumps(envelope).encode()
    if len(payload) > MAX_MESSAGE:
        raise ValueError("message too large")
    sock.sendto(payload, socket_path(bus, to))


def main():
    # SIGTERM exits through the finally block too, so the name is unregistered.
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(SystemExit(0)))
    bus = bus_dir()
    os.makedirs(bus, mode=0o700, exist_ok=True)

    # Refuse a live name; take over a stale one.
    existing = descriptor_path(bus, NAME)
    if os.path.exists(existing):
        try:
            with open(existing) as file:
                if live(json.load(file)):
                    raise SystemExit(f"{NAME!r} is already registered")
        except json.JSONDecodeError:
            pass
    path = socket_path(bus, NAME)
    if os.path.exists(path):
        os.remove(path)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(path)
    sock.setblocking(False)

    started = time.time()
    write_descriptor(bus, started)
    print(f"listening as {NAME} on {bus} (Ctrl+C to stop)")
    for peer in discover(bus):
        if peer["name"] != NAME:
            print(f"also on the bus: {peer['name']}")

    try:
        heartbeat = started
        while True:
            if time.time() - heartbeat >= HEARTBEAT:
                write_descriptor(bus, started)
                heartbeat = time.time()
            try:
                datagram = sock.recv(MAX_MESSAGE)
            except BlockingIOError:
                time.sleep(0.02)
                continue
            try:
                message = json.loads(datagram)
            except json.JSONDecodeError:
                continue  # undecodable datagrams are dropped
            print(f"{message.get('from')} -> {message.get('kind')}: {message.get('data')}")
            kind = "pong" if message.get("kind") == "ping" else "echo"
            try:
                send(bus, sock, message["from"], kind, message.get("data"))
            except (OSError, KeyError) as error:
                print(f"could not reply: {error}")
    finally:
        sock.close()
        for file in (socket_path(bus, NAME), descriptor_path(bus, NAME)):
            if os.path.exists(file):
                os.remove(file)


if __name__ == "__main__":
    main()
