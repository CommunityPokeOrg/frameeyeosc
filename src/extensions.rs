//! The extensions bus: how separate apps on the Steam Frame find each other and exchange
//! messages while running side by side, without a central broker or a shared library.
//!
//! Everything lives under `$XDG_RUNTIME_DIR/frame-apps/` (falling back to `/run/user/<uid>`),
//! which is per-user and exists only while its owner is logged in, so nothing leaves the
//! device and nothing survives a reboot:
//!
//! ```text
//! frame-apps/
//!   frameeyeosc.json    descriptor: who the app is and which message kinds it accepts
//!   frameeyeosc.sock    its inbox: one Unix datagram socket per app
//!   frame-mic-tuner.json
//!   frame-mic-tuner.sock
//! ```
//!
//! An app registers by binding `<name>.sock` and writing `<name>.json` atomically (a
//! temporary file renamed over the last one, like `status.json`). It keeps `updated` fresh
//! as a heartbeat; a descriptor whose app is dead or whose heartbeat is older than
//! [`STALE_AFTER`] is ignored by [`discover`] and reclaimed by the next app with the name.
//!
//! A message is a single datagram of UTF-8 JSON, at most [`MAX_MESSAGE`] bytes:
//!
//! ```json
//! { "v": 1, "from": "frame-mic-tuner", "kind": "ping", "data": { "t": 1760000000.0 } }
//! ```
//!
//! Sending is fire-and-forget: the sender writes to the peer's inbox socket, and a peer
//! that is gone, busy or full simply drops the datagram, the same as the panel's gaze
//! dots. Replies go to the name in `from`, which reaches the sender's own inbox.
//! Any language with Unix datagram sockets and JSON can speak it; see docs/extensions.md.

use serde::{Deserialize, Serialize};
use std::fs::{self, DirBuilder};
use std::io;
use std::os::unix::fs::DirBuilderExt;
use std::os::unix::net::UnixDatagram;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

/// The folder extensions register in and look each other up in.
pub fn bus_dir() -> PathBuf {
    std::env::var_os("XDG_RUNTIME_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(format!("/run/user/{}", unsafe { libc::getuid() })))
        .join("frame-apps")
}

/// A name is lowercase letters, digits, `-` and `_`, starting with a letter or digit, at most
/// this long. It is also the app's socket and descriptor file names, so nothing else is allowed.
pub const MAX_NAME_LEN: usize = 32;

pub fn valid_name(name: &str) -> bool {
    let ok = name
        .chars()
        .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-' || c == '_');
    !name.is_empty()
        && name.len() <= MAX_NAME_LEN
        && ok
        && name.chars().next().is_some_and(|c| c != '-' && c != '_')
}

/// The wire format's version, so later revisions can coexist.
pub const PROTOCOL_VERSION: u32 = 1;

/// One message per datagram, capped here: bigger payloads belong in a file the message points at.
pub const MAX_MESSAGE: usize = 8 * 1024;

/// The descriptor is rewritten at least this often while the app runs.
pub const HEARTBEAT: Duration = Duration::from_secs(5);

/// A descriptor older than this (or with a dead pid) counts as gone.
pub const STALE_AFTER: Duration = Duration::from_secs(15);

/// What an app publishes about itself; other apps read it in [`discover`].
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Descriptor {
    pub v: u32,
    pub name: String,
    pub pid: u32,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub version: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub description: Option<String>,
    /// Unix time the app registered at.
    pub started: f64,
    /// Unix time the descriptor was last (re)written; the heartbeat.
    pub updated: f64,
    /// The message kinds the app answers; informational, senders can still send anything.
    #[serde(default)]
    pub accepts: Vec<String>,
}

/// What [`discover`] returns for one app.
#[derive(Clone, Debug)]
pub struct Peer {
    pub descriptor: Descriptor,
}

/// One message as it travels on the bus.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Envelope {
    pub v: u32,
    /// The sender's registered name; replies are sent to it.
    pub from: String,
    /// What the message is, agreed between the two apps ("ping", "get-status", ...).
    pub kind: String,
    /// The payload; any JSON value, `null` for none.
    #[serde(default)]
    pub data: serde_json::Value,
}

pub fn encode(envelope: &Envelope) -> Result<Vec<u8>, String> {
    if !valid_name(&envelope.from) {
        return Err(format!("not a valid extension name: {:?}", envelope.from));
    }
    let bytes = serde_json::to_vec(envelope).map_err(|error| error.to_string())?;
    if bytes.len() > MAX_MESSAGE {
        return Err(format!(
            "message of {} bytes exceeds the {MAX_MESSAGE} byte limit",
            bytes.len()
        ));
    }
    Ok(bytes)
}

pub fn decode(datagram: &[u8]) -> Result<Envelope, String> {
    let envelope: Envelope = serde_json::from_slice(datagram)
        .map_err(|error| format!("not an extension message: {error}"))?;
    if envelope.v != PROTOCOL_VERSION {
        return Err(format!("unsupported protocol version {}", envelope.v));
    }
    if !valid_name(&envelope.from) {
        return Err(format!("not a valid sender name: {:?}", envelope.from));
    }
    Ok(envelope)
}

fn unix_now() -> f64 {
    let seconds = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0.0, |since| since.as_secs_f64());
    (seconds * 1000.0).round() / 1000.0
}

/// Whether the pid in a descriptor still belongs to a running process.
fn alive(pid: u32) -> bool {
    // kill(pid, 0) reports ESRCH for a dead pid and EPERM or 0 for a live one (EPERM can't
    // happen for same-user processes, but is a live process anyway).
    let result = unsafe { libc::kill(pid as libc::pid_t, 0) };
    result == 0 || io::Error::last_os_error().raw_os_error() == Some(libc::EPERM)
}

/// Whether a descriptor names an app that is still there: fresh heartbeat and a live pid.
pub fn is_live(descriptor: &Descriptor) -> bool {
    let fresh = unix_now() - descriptor.updated < STALE_AFTER.as_secs_f64();
    fresh && alive(descriptor.pid)
}

/// The error [`Extension::register`] returns when a live app already holds the name.
pub fn name_taken(name: &str) -> io::Error {
    io::Error::new(
        io::ErrorKind::AddrInUse,
        format!("extension name {name:?} is already registered"),
    )
}

/// Replace a file atomically (write `*.tmp`, rename), the way `status.json` is written.
fn write_atomic(path: &Path, contents: &[u8]) -> io::Result<()> {
    let temporary = path.with_extension("tmp");
    fs::write(&temporary, contents)?;
    fs::rename(temporary, path)
}

fn descriptor_path(bus: &Path, name: &str) -> PathBuf {
    bus.join(format!("{name}.json"))
}

fn socket_path(bus: &Path, name: &str) -> PathBuf {
    bus.join(format!("{name}.sock"))
}

fn read_descriptor(path: &Path) -> Option<Descriptor> {
    let descriptor: Descriptor = serde_json::from_slice(&fs::read(path).ok()?).ok()?;
    (descriptor.v == PROTOCOL_VERSION && valid_name(&descriptor.name)).then_some(descriptor)
}

/// The live extensions on the bus, in no particular order. Stale descriptors are skipped,
/// and a descriptor that can't be read at all counts as none.
pub fn discover(bus: &Path) -> Vec<Peer> {
    let mut peers = Vec::new();
    let Ok(entries) = fs::read_dir(bus) else {
        return peers;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension() != Some(std::ffi::OsStr::new("json")) {
            continue;
        }
        if let Some(descriptor) = read_descriptor(&path).filter(is_live) {
            peers.push(Peer { descriptor });
        }
    }
    peers
}

/// What an app registers with; fills in the parts of [`Descriptor`] only the bus can know.
#[derive(Clone, Debug, Default)]
pub struct Registration {
    pub version: Option<String>,
    pub description: Option<String>,
    /// The message kinds the app answers (e.g. `vec!["ping".into(), "get-status".into()]`).
    pub accepts: Vec<String>,
}

/// A registered extension: its inbox socket and descriptor on the bus. While the handle is
/// alive the app can be found and sent messages; dropping it unregisters.
pub struct Extension {
    bus: PathBuf,
    name: String,
    socket: UnixDatagram,
    info: Registration,
    started: f64,
    /// When the descriptor was last written.
    heartbeat: std::time::Instant,
    /// Malformed datagrams seen; logged once per kind of failure, not per packet.
    bad_packets: u64,
}

impl Extension {
    /// Put `name` on the bus. Fails with [`io::ErrorKind::AddrInUse`] while another live app
    /// holds the name; a stale registration (crashed app) is taken over.
    pub fn register(name: &str, info: Registration) -> io::Result<Self> {
        Self::register_at(&bus_dir(), name, info)
    }

    pub fn register_at(bus: &Path, name: &str, info: Registration) -> io::Result<Self> {
        if !valid_name(name) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("not a valid extension name: {name:?}"),
            ));
        }
        DirBuilder::new().recursive(true).mode(0o700).create(bus)?;
        if let Some(old) = read_descriptor(&descriptor_path(bus, name))
            && is_live(&old)
        {
            return Err(name_taken(name));
        }
        let socket = socket_path(bus, name);
        let _ = fs::remove_file(&socket);
        let socket = UnixDatagram::bind(&socket)?;
        socket.set_nonblocking(true)?;
        let extension = Self {
            bus: bus.to_path_buf(),
            name: name.to_owned(),
            socket,
            info,
            started: unix_now(),
            // Written right away below, so the heartbeat clock starts now.
            heartbeat: std::time::Instant::now(),
            bad_packets: 0,
        };
        extension.write_descriptor()?;
        Ok(extension)
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    /// How the app presents itself.
    pub fn describe(&mut self, info: Registration) -> io::Result<()> {
        self.info = info;
        self.write_descriptor()
    }

    fn write_descriptor(&self) -> io::Result<()> {
        let descriptor = Descriptor {
            v: PROTOCOL_VERSION,
            name: self.name.clone(),
            pid: std::process::id(),
            version: self.info.version.clone(),
            description: self.info.description.clone(),
            started: self.started,
            updated: unix_now(),
            accepts: self.info.accepts.clone(),
        };
        write_atomic(
            &descriptor_path(&self.bus, &self.name),
            &serde_json::to_vec(&descriptor)?,
        )
    }

    /// Keep the heartbeat fresh; cheap when not due. Called from `poll`, so apps that only
    /// ever call `poll` still stay registered.
    pub fn beat(&mut self) {
        if self.heartbeat.elapsed() >= HEARTBEAT && self.write_descriptor().is_ok() {
            self.heartbeat = std::time::Instant::now();
        }
    }

    /// Take whatever has arrived in the inbox since the last call; the heartbeat is kept too.
    /// Undecodable datagrams are dropped (counted in `bad_packets`).
    pub fn poll(&mut self) -> Vec<Envelope> {
        self.beat();
        let mut received = Vec::new();
        let mut buffer = vec![0u8; MAX_MESSAGE];
        loop {
            match self.socket.recv(&mut buffer) {
                Ok(size) => match decode(&buffer[..size]) {
                    Ok(envelope) => received.push(envelope),
                    Err(error) => {
                        if self.bad_packets == 0 {
                            eprintln!("extensions: dropped a datagram ({error})");
                        }
                        self.bad_packets += 1;
                    }
                },
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                Err(_) => break,
            }
        }
        received
    }

    /// Send one message to the extension registered as `to`. Best-effort: fails with
    /// `NotFound` when the peer's socket is missing, otherwise the datagram is on its way
    /// (a peer that never reads still accepts it into its socket queue).
    pub fn send(&self, to: &str, kind: &str, data: serde_json::Value) -> io::Result<usize> {
        send_at(&self.bus, &self.name, to, kind, data)
    }

    /// Reply to a message received in `poll`: sends back to the sender's own inbox.
    pub fn reply(
        &self,
        to_message: &Envelope,
        kind: &str,
        data: serde_json::Value,
    ) -> io::Result<usize> {
        self.send(&to_message.from, kind, data)
    }

    /// Take the name off the bus (the socket and descriptor are removed).
    pub fn unregister(self) {
        // Drop below removes the files.
    }
}

impl Drop for Extension {
    fn drop(&mut self) {
        let _ = fs::remove_file(socket_path(&self.bus, &self.name));
        let _ = fs::remove_file(descriptor_path(&self.bus, &self.name));
    }
}

/// Send a message without registering, for one-shot tools. `from` still names a sender the
/// peer may reply to, so tools that want answers should register instead.
pub fn send_once(
    bus: &Path,
    from: &str,
    to: &str,
    kind: &str,
    data: serde_json::Value,
) -> io::Result<usize> {
    send_at(bus, from, to, kind, data)
}

fn send_at(
    bus: &Path,
    from: &str,
    to: &str,
    kind: &str,
    data: serde_json::Value,
) -> io::Result<usize> {
    if !valid_name(to) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("not a valid extension name: {to:?}"),
        ));
    }
    let envelope = Envelope {
        v: PROTOCOL_VERSION,
        from: from.to_owned(),
        kind: kind.to_owned(),
        data,
    };
    let bytes =
        encode(&envelope).map_err(|reason| io::Error::new(io::ErrorKind::InvalidInput, reason))?;
    let socket = UnixDatagram::unbound()?;
    // An unbound socket has no address to reply to; senders that want replies register and
    // send from their inbox socket instead (Extension::send does).
    socket.send_to(&bytes, socket_path(bus, to))
}

/// Send to the named extension and wait for the first reply, for request/response kinds like
/// "ping" and "get-status". Registers a temporary name (`req-<pid>-<n>`) so the peer has
/// somewhere to reply, and unregisters before returning.
///
/// Returns the first message received within `timeout`, or `None` when none arrives.
pub fn request(
    bus: &Path,
    to: &str,
    kind: &str,
    data: serde_json::Value,
    timeout: Duration,
) -> io::Result<Option<Envelope>> {
    static SEQ: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let seq = SEQ.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let name = format!("req-{}-{seq}", std::process::id());
    let mut probe = Extension::register_at(bus, &name, Registration::default())?;
    probe.send(to, kind, data)?;
    let deadline = std::time::Instant::now() + timeout;
    loop {
        let received = probe.poll();
        if let Some(envelope) = received.into_iter().next() {
            return Ok(Some(envelope));
        }
        if std::time::Instant::now() >= deadline {
            return Ok(None);
        }
        std::thread::sleep(Duration::from_millis(10));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bus(tag: &str) -> PathBuf {
        let dir =
            std::env::temp_dir().join(format!("frame-apps-test-{}-{tag}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        dir
    }

    fn registration() -> Registration {
        Registration {
            version: Some("0.0.0-test".into()),
            description: Some("a test extension".into()),
            accepts: vec!["ping".into(), "get-status".into()],
        }
    }

    #[test]
    fn names_are_limited_to_safe_file_characters() {
        assert!(valid_name("frameeyeosc"));
        assert!(valid_name("frame-jp-keyboard"));
        assert!(valid_name("mic_tuner2"));
        assert!(!valid_name(""));
        assert!(!valid_name("-leading"));
        assert!(!valid_name("Upper"));
        assert!(!valid_name("../escape"));
        assert!(!valid_name("with space"));
        assert!(!valid_name(&"x".repeat(MAX_NAME_LEN + 1)));
    }

    #[test]
    fn envelopes_read_back_and_bad_ones_are_rejected() {
        let envelope = Envelope {
            v: PROTOCOL_VERSION,
            from: "tester".into(),
            kind: "ping".into(),
            data: serde_json::json!({"t": 1.5}),
        };
        assert_eq!(decode(&encode(&envelope).unwrap()).unwrap(), envelope);
        assert!(decode(b"not json").is_err());
        assert!(decode(br#"{"v":99,"from":"a","kind":"k"}"#).is_err());
        assert!(decode(br#"{"v":1,"from":"BAD NAME","kind":"k"}"#).is_err());
        let mut big = envelope.clone();
        big.data = serde_json::json!("x".repeat(MAX_MESSAGE));
        assert!(encode(&big).is_err());
    }

    #[test]
    fn registering_makes_the_app_discoverable_until_it_unregisters() {
        let dir = bus("discover");
        let app = Extension::register_at(&dir, "app-one", registration()).unwrap();
        let peers = discover(&dir);
        assert_eq!(peers.len(), 1);
        assert_eq!(peers[0].descriptor.name, "app-one");
        assert_eq!(peers[0].descriptor.pid, std::process::id());
        assert_eq!(peers[0].descriptor.accepts, vec!["ping", "get-status"]);
        drop(app);
        assert!(discover(&dir).is_empty());
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_live_app_keeps_its_name_but_a_stale_one_is_taken_over() {
        let dir = bus("taken");
        let app = Extension::register_at(&dir, "app-two", registration()).unwrap();
        match Extension::register_at(&dir, "app-two", registration()) {
            Err(error) => assert_eq!(error.kind(), io::ErrorKind::AddrInUse),
            Ok(_) => panic!("a live name must not register twice"),
        }
        drop(app);
        // Leftover socket and descriptor from a "crashed" app don't block the next one.
        let crashed = Extension::register_at(&dir, "app-three", registration()).unwrap();
        let mut descriptor = read_descriptor(&descriptor_path(&dir, "app-three")).unwrap();
        descriptor.pid = 99_999_999; // almost certainly dead
        write_atomic(
            &descriptor_path(&dir, "app-three"),
            &serde_json::to_vec(&descriptor).unwrap(),
        )
        .unwrap();
        assert!(discover(&dir).is_empty());
        drop(crashed);
        Extension::register_at(&dir, "app-three", registration()).unwrap();
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn extensions_exchange_messages() {
        let dir = bus("chat");
        let mut alpha = Extension::register_at(&dir, "alpha", registration()).unwrap();
        let mut beta = Extension::register_at(&dir, "beta", registration()).unwrap();
        alpha
            .send("beta", "ping", serde_json::json!({"n": 1}))
            .unwrap();
        let received = beta.poll();
        assert_eq!(received.len(), 1);
        assert_eq!(received[0].from, "alpha");
        assert_eq!(received[0].kind, "ping");
        beta.reply(&received[0], "pong", serde_json::json!({"n": 2}))
            .unwrap();
        let replied = alpha.poll();
        assert_eq!(replied.len(), 1);
        assert_eq!(replied[0].kind, "pong");
        assert_eq!(replied[0].from, "beta");
        // Malformed datagrams don't disturb the socket.
        UnixDatagram::unbound()
            .unwrap()
            .send_to(b"garbage", socket_path(&dir, "alpha"))
            .unwrap();
        assert!(alpha.poll().is_empty());
        assert_eq!(alpha.bad_packets, 1);
        // Nobody listening: NotFound for a missing inbox, fine for a stale socket file.
        assert_eq!(
            alpha
                .send("ghost", "ping", serde_json::Value::Null)
                .unwrap_err()
                .kind(),
            io::ErrorKind::NotFound
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn request_waits_for_a_reply() {
        let dir = bus("request");
        let dir2 = dir.clone();
        let server = std::thread::spawn(move || {
            let mut echo = Extension::register_at(&dir2, "echo-server", registration()).unwrap();
            for _ in 0..100 {
                for message in echo.poll() {
                    echo.reply(&message, "answer", serde_json::json!({"ok": true}))
                        .unwrap();
                }
                std::thread::sleep(Duration::from_millis(10));
            }
        });
        std::thread::sleep(Duration::from_millis(50));
        let reply = request(
            &dir,
            "echo-server",
            "ping",
            serde_json::Value::Null,
            Duration::from_secs(2),
        )
        .unwrap();
        assert_eq!(reply.unwrap().kind, "answer");
        server.join().unwrap();
        // Nobody there: None, not an error forever.
        let reply = request(
            &dir,
            "ghost",
            "ping",
            serde_json::Value::Null,
            Duration::from_millis(100),
        );
        assert!(matches!(reply, Err(error) if error.kind() == io::ErrorKind::NotFound));
        let _ = fs::remove_dir_all(&dir);
    }
}
