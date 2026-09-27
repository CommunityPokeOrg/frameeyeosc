//! The panel's debug gaze dots. While `gaze_debug_dots` is on, every processed sample's gaze as sent
//! goes to the panel as one datagram on a Unix socket in the status folder, where the panel listens.
//! Nothing leaves the device, and nothing is sent while it is off.
//!
//! A datagram is 40 bytes, little-endian: b"FEOD", version 1, flags (bit 0: per-eye gaze is sent),
//! 2 reserved zero bytes, the sample time (f64), and the six gaze values as sent (f32: left x/y,
//! right x/y, combined x/y; 1.0 = 45°).

use std::os::unix::net::UnixDatagram;
use std::path::{Path, PathBuf};

pub const SOCKET_NAME: &str = "gaze-dots.sock";
const MAGIC: &[u8; 4] = b"FEOD";
const VERSION: u8 = 1;
pub const PACKET_SIZE: usize = 40;

/// One sample for the dots.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DotSample {
    pub time: f64,
    pub gaze: [f32; 6],
    pub independent: bool,
}

pub fn encode(sample: &DotSample) -> [u8; PACKET_SIZE] {
    let mut packet = [0u8; PACKET_SIZE];
    packet[..4].copy_from_slice(MAGIC);
    packet[4] = VERSION;
    packet[5] = u8::from(sample.independent);
    packet[8..16].copy_from_slice(&sample.time.to_le_bytes());
    for (i, value) in sample.gaze.iter().enumerate() {
        packet[16 + i * 4..20 + i * 4].copy_from_slice(&value.to_le_bytes());
    }
    packet
}

#[cfg(test)]
pub fn decode(packet: &[u8]) -> Option<DotSample> {
    if packet.len() != PACKET_SIZE || &packet[..4] != MAGIC || packet[4] != VERSION {
        return None;
    }
    let time = f64::from_le_bytes(packet[8..16].try_into().ok()?);
    let gaze = std::array::from_fn(|i| f32::from_le_bytes(packet[16 + i * 4..20 + i * 4].try_into().unwrap()));
    Some(DotSample {
        time,
        gaze,
        independent: packet[5] & 1 != 0,
    })
}

/// Sends the dots while they are on; holds no socket while they are off.
pub struct DotStream {
    path: PathBuf,
    socket: Option<UnixDatagram>,
}

impl DotStream {
    /// `dir` is the status folder ($XDG_RUNTIME_DIR/frameeyeosc), where the panel binds the socket.
    pub fn new(dir: &Path) -> Self {
        Self {
            path: dir.join(SOCKET_NAME),
            socket: None,
        }
    }

    pub fn send(&mut self, enabled: bool, sample: &DotSample) {
        if !enabled {
            self.socket = None;
            return;
        }
        if self.socket.is_none() {
            self.socket = UnixDatagram::unbound()
                .and_then(|socket| socket.set_nonblocking(true).map(|()| socket))
                .ok();
        }
        // No panel listening (or it is busy) is normal: the sample is simply dropped.
        if let Some(socket) = &self.socket {
            let _ = socket.send_to(&encode(sample), &self.path);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn packets_read_back_exactly() {
        let sample = DotSample {
            time: 19892.729813125,
            gaze: [0.1, -0.2, 0.3, -0.4, 0.5, -0.6],
            independent: true,
        };
        let packet = encode(&sample);
        assert_eq!(&packet[..8], b"FEOD\x01\x01\x00\x00");
        assert_eq!(decode(&packet), Some(sample));
        let mut wrong = packet;
        wrong[0] = b'X';
        assert_eq!(decode(&wrong), None);
        assert_eq!(decode(&packet[..39]), None);
    }

    #[test]
    fn the_panel_gets_each_sample_only_while_on() {
        let dir = std::env::temp_dir().join(format!("frameeyeosc-dots-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let panel = UnixDatagram::bind(dir.join(SOCKET_NAME)).unwrap();
        panel.set_nonblocking(true).unwrap();
        let mut stream = DotStream::new(&dir);
        let sample = DotSample {
            time: 1.0,
            gaze: [0.0; 6],
            independent: false,
        };
        stream.send(false, &sample);
        let mut buffer = [0u8; 64];
        assert!(panel.recv(&mut buffer).is_err());
        stream.send(true, &sample);
        let size = panel.recv(&mut buffer).unwrap();
        assert_eq!(decode(&buffer[..size]), Some(sample));
        drop(panel);
        // Nobody listening is fine.
        std::fs::remove_dir_all(&dir).unwrap();
        stream.send(true, &sample);
    }
}
