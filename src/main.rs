//! Steam Frame 0.5.0 eye bridge using the private version-4 shared-memory ABI.

use clap::Parser;
use memmap2::{MmapMut, MmapOptions};
use rosc::{OscMessage, OscPacket, OscType, encoder};
use std::error::Error;
use std::fs::OpenOptions;
use std::io;
use std::mem::{align_of, offset_of, size_of};
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::ptr;
use std::time::Duration;

const SHM_VERSION: u32 = 4;
const SHM_SIZE: usize = 0x4f21a;
const SOURCE: &str = "/dev/shm/eye-server.mmap";
const TIMEOUT: Duration = Duration::from_secs(1);

#[repr(C)]
struct EyeServerMmap {
    version: u32,
    initialized: u32,
    // The target glibc mutex slot is 48 bytes; host libc may define a smaller type.
    metadata_mutex: [u8; 0x30],
    sequence: u32,
    metadata_requested: u32,
    other_control_fields: [u8; 0x112],
    eye_data: EyeDataMmap,
}

// The record is packed, so its timestamp and vectors are not naturally aligned.
#[repr(C, packed)]
#[derive(Clone, Copy)]
struct EyeDataMmap {
    producer_state: u32,
    unknown_04: u8,
    sample_time: f64,
    eye_origins: [[f32; 3]; 2],
    eye_directions: [[f32; 3]; 2],
    fused_gaze: [f32; 3],
    other_eye_origins: [[f32; 3]; 2],
    other_eye_directions: [[f32; 3]; 2],
    openness: [f32; 2],
    estimate_extra: [f32; 8],
    reserved: [u8; 0xe1b],
}

const _: () = {
    assert!(offset_of!(EyeServerMmap, metadata_mutex) == 0x08);
    assert!(offset_of!(EyeServerMmap, sequence) == 0x38);
    assert!(offset_of!(EyeServerMmap, metadata_requested) == 0x3c);
    assert!(offset_of!(EyeServerMmap, eye_data) == 0x152);
    assert!(offset_of!(EyeDataMmap, sample_time) == 0x05);
    assert!(offset_of!(EyeDataMmap, eye_directions) == 0x25);
    assert!(offset_of!(EyeDataMmap, fused_gaze) == 0x3d);
    assert!(offset_of!(EyeDataMmap, openness) == 0x79);
    assert!(size_of::<EyeDataMmap>() == 0xebc);
    assert!(size_of::<EyeServerMmap>() <= SHM_SIZE);
    assert!(size_of::<libc::pthread_mutex_t>() <= 0x30);
    assert!(8 % align_of::<libc::pthread_mutex_t>() == 0);
};

#[derive(Parser)]
#[command(about = "Send Steam Frame eye tracking from shared memory over OSC")]
struct Args {
    #[arg(long, default_value = "127.0.0.1:9000")]
    target: String,
    #[arg(long, default_value = "/FT")]
    prefix: String,
}

struct EyeSource {
    map: MmapMut,
}

struct MutexGuard(*mut libc::pthread_mutex_t);

impl Drop for MutexGuard {
    fn drop(&mut self) {
        unsafe { libc::pthread_mutex_unlock(self.0) };
    }
}

struct EyeData {
    sample_time: f64,
    gaze: [[f32; 3]; 2],
    fused_gaze: [f32; 3],
    openness: [f32; 2],
}

impl EyeSource {
    fn open() -> Result<Self, Box<dyn Error>> {
        let file = OpenOptions::new().read(true).write(true).open(SOURCE)?;
        if file.metadata()?.len() < SHM_SIZE as u64 {
            return Err(format!("{SOURCE}: shared memory is too small").into());
        }
        let map = unsafe { MmapOptions::new().len(SHM_SIZE).map_mut(&file)? };
        let source = Self { map };
        let layout = source.layout();
        let version = u32::from_le(unsafe { ptr::read_volatile(&raw const (*layout).version) });
        if version != SHM_VERSION {
            return Err(format!(
                "unsupported eye shared-memory version {version}; expected {SHM_VERSION} (Frame 0.5.0)"
            )
            .into());
        }
        if u32::from_le(unsafe { ptr::read_volatile(&raw const (*layout).initialized) }) != 1 {
            return Err("eye shared memory is not initialized".into());
        }
        Ok(source)
    }

    fn layout(&self) -> *const EyeServerMmap {
        self.map.as_ptr().cast()
    }

    fn layout_mut(&mut self) -> *mut EyeServerMmap {
        self.map.as_mut_ptr().cast()
    }

    fn lock(&mut self) -> io::Result<MutexGuard> {
        let mutex = unsafe { (&raw mut (*self.layout_mut()).metadata_mutex).cast() };
        let code = unsafe { libc::pthread_mutex_lock(mutex) };
        if code == libc::EOWNERDEAD {
            let result = unsafe { libc::pthread_mutex_consistent(mutex) };
            if result != 0 {
                unsafe { libc::pthread_mutex_unlock(mutex) };
                return Err(io::Error::from_raw_os_error(result));
            }
        } else if code != 0 {
            return Err(io::Error::from_raw_os_error(code));
        }
        Ok(MutexGuard(mutex))
    }

    fn next(&mut self, timeout: Duration) -> io::Result<Option<EyeData>> {
        let guard = self.lock()?;
        let sequence_ptr = unsafe { &raw const (*self.layout()).sequence };
        let sequence = unsafe { ptr::read_volatile(sequence_ptr) };
        let request_ptr = unsafe { &raw mut (*self.layout_mut()).metadata_requested };
        unsafe { ptr::write_volatile(request_ptr, 1) };
        drop(guard);

        let timespec = libc::timespec {
            tv_sec: timeout.as_secs() as libc::time_t,
            tv_nsec: timeout.subsec_nanos() as libc::c_long,
        };
        let result = unsafe {
            libc::syscall(
                libc::SYS_futex,
                sequence_ptr,
                libc::FUTEX_WAIT,
                sequence,
                &timespec as *const libc::timespec,
            )
        };
        if result == -1 {
            let error = io::Error::last_os_error();
            if !matches!(
                error.raw_os_error(),
                Some(libc::EAGAIN | libc::EINTR | libc::ETIMEDOUT)
            ) {
                return Err(error);
            }
        }

        let guard = self.lock()?;
        let data = if unsafe { ptr::read_volatile(sequence_ptr) } != sequence {
            let record_ptr = unsafe { &raw const (*self.layout()).eye_data };
            let record = unsafe { ptr::read_unaligned(record_ptr) };
            if record.producer_state == 1 {
                Some(EyeData {
                    sample_time: record.sample_time,
                    gaze: record.eye_directions,
                    fused_gaze: record.fused_gaze,
                    openness: record.openness,
                })
            } else {
                None
            }
        } else {
            None
        };
        drop(guard);
        Ok(data)
    }
}

fn send(socket: &UdpSocket, addr: String, args: Vec<OscType>) -> Result<(), Box<dyn Error>> {
    let packet = OscPacket::Message(OscMessage { addr, args });
    socket.send(&encoder::encode(&packet)?)?;
    Ok(())
}

fn send_eye_data(socket: &UdpSocket, args: &Args, data: EyeData) -> Result<(), Box<dyn Error>> {
    let prefix = format!("/avatar/parameters{}", args.prefix.trim_end_matches('/'));
    send(
        socket,
        format!("{prefix}/EyeTrackingActive"),
        vec![OscType::Bool(true)],
    )?;
    // Eye order and vertical-axis sign remain provisional until checked on hardware.
    let left_x = data.gaze[0][0].clamp(-1.0, 1.0);
    let left_y = (-data.gaze[0][1]).clamp(-1.0, 1.0);
    let right_x = data.gaze[1][0].clamp(-1.0, 1.0);
    let right_y = (-data.gaze[1][1]).clamp(-1.0, 1.0);
    let x = data.fused_gaze[0].clamp(-1.0, 1.0);
    let y = (-data.fused_gaze[1]).clamp(-1.0, 1.0);
    for (suffix, value) in [
        ("EyeLeftX", left_x),
        ("EyeLeftY", left_y),
        ("EyeRightX", right_x),
        ("EyeRightY", right_y),
        ("EyeLidLeft", data.openness[0].clamp(0.0, 1.0)),
        ("EyeLidRight", data.openness[1].clamp(0.0, 1.0)),
        ("EyeX", x),
        ("EyeY", y),
    ] {
        send(
            socket,
            format!("{prefix}/v2/{suffix}"),
            vec![OscType::Float(value)],
        )?;
    }
    Ok(())
}

fn send_inactive(socket: &UdpSocket, prefix: &str) -> Result<(), Box<dyn Error>> {
    send(
        socket,
        format!(
            "/avatar/parameters{}/EyeTrackingActive",
            prefix.trim_end_matches('/')
        ),
        vec![OscType::Bool(false)],
    )
}

fn main() -> Result<(), Box<dyn Error>> {
    let args = Args::parse();
    if !args.prefix.starts_with('/') || args.prefix.trim_matches('/').is_empty() {
        return Err("--prefix must be a nonempty OSC path starting with /".into());
    }
    let target: SocketAddr = args
        .target
        .to_socket_addrs()?
        .next()
        .ok_or("--target did not resolve to an address")?;
    let socket = UdpSocket::bind(if target.is_ipv4() {
        "0.0.0.0:0"
    } else {
        "[::]:0"
    })?;
    socket.connect(target)?;
    let mut source = EyeSource::open()?;
    eprintln!("Reading {SOURCE} and sending OSC to {target}");
    let mut active = false;
    loop {
        match source.next(TIMEOUT)? {
            Some(data)
                if data.sample_time.is_finite()
                    && data.gaze.iter().flatten().all(|value| value.is_finite())
                    && data.fused_gaze.iter().all(|value| value.is_finite())
                    && data.openness.iter().all(|value| value.is_finite()) =>
            {
                send_eye_data(&socket, &args, data)?;
                active = true;
            }
            _ if active => {
                send_inactive(&socket, &args.prefix)?;
                active = false;
            }
            _ => {}
        }
    }
}
