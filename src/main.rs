//! Steam Frame 0.5.0 eye bridge using the private version-4 shared-memory ABI.

mod config;
mod status;

use clap::{CommandFactory, FromArgMatches, Parser};
use config::{Config, OutputKind, Reload, Settings};
use memmap2::{MmapMut, MmapOptions};
use rosc::{OscMessage, OscPacket, OscType, encoder};
use status::{CalibrationStatus, RawValues, SentValues, Status, StatusFile};
use std::collections::{HashSet, VecDeque};
use std::error::Error;
use std::fs::{self, OpenOptions};
use std::os::unix::fs::MetadataExt;
use std::io;
use std::mem::{align_of, offset_of, size_of};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, ToSocketAddrs, UdpSocket};
use std::path::{Path, PathBuf};
use std::ptr;
use std::time::{Duration, Instant, SystemTime};

const SHM_VERSION: u32 = 4;
const SHM_SIZE: usize = 0x4f21a;
const SOURCE: &str = "/dev/shm/eye-server.mmap";
const TIMEOUT: Duration = Duration::from_secs(1);
// The eye server is waited on in slices this long, so the status file keeps updating while it is idle.
const POLL: Duration = Duration::from_millis(100);
// The status file's send rate counts the samples sent within this window.
const RATE_WINDOW: Duration = Duration::from_secs(1);
// Relaxed open is 0.75 in VRCFT units but 1.0 for the ETVR Tracking Module, which does not widen by default.
const ETVR_LID_SCALE: f32 = 1.0 / 0.75;
// The eye server produces samples at ~90 Hz.
const NOMINAL_DT: f32 = 1.0 / 90.0;
// Gaps longer than this restart the filters instead of smearing across them.
const MAX_GAP: f64 = 0.25;
// Blinks are fast, so eyelids track their speed with a quicker derivative filter than gaze.
const LID_D_CUTOFF: f32 = 1.0;
// How often the Steam Link PC is looked up again, to follow reconnects over another network.
const RESOLVE_INTERVAL: Duration = Duration::from_secs(5);
// Eyelid auto calibration keeps a decaying histogram of each eye's open readings (0.005 wide bins).
const CAL_BINS: usize = 300;
const CAL_BIN_WIDTH: f32 = 0.005;
// Readings fade with a 10 minute half-life at ~90 Hz, so a short squint barely moves the estimate.
const CAL_HALF_LIFE_SAMPLES: f32 = 90.0 * 600.0;
// Only readings above this fraction of the current estimate count as "open". Higher gates resist
// squints better but ratchet the estimate upward on eyes whose readings spread widely.
const CAL_GATE: f32 = 0.75;
// Skip this long after tracking starts, while the headset is still being put on and adjusted.
const CAL_SETTLE: Duration = Duration::from_secs(20);
// Weight (~10 s of open eyes) before the histogram overrides the saved or default estimate.
const CAL_WARMUP_WEIGHT: f32 = 900.0;
const CAL_SCALE_RANGE: (f32, f32) = (0.75, 1.33);
const CAL_SAVE_INTERVAL: Duration = Duration::from_secs(60);

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
    sample_flag: u8,
    sample_time: f64,
    // Left, right; after stereo fusion.
    gaze_direction: [[f32; 3]; 2],
    gaze_covariance_diag: [[f32; 3]; 2],
    // Head-relative, -Z forward, in metres.
    fixation_point: [f32; 3],
    pre_fusion_gaze: [[f32; 3]; 2],
    pre_fusion_cov_diag: [[f32; 3]; 2],
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
    assert!(offset_of!(EyeDataMmap, gaze_direction) == 0x0d);
    assert!(offset_of!(EyeDataMmap, gaze_covariance_diag) == 0x25);
    assert!(offset_of!(EyeDataMmap, fixation_point) == 0x3d);
    assert!(offset_of!(EyeDataMmap, pre_fusion_gaze) == 0x49);
    assert!(offset_of!(EyeDataMmap, pre_fusion_cov_diag) == 0x61);
    assert!(offset_of!(EyeDataMmap, openness) == 0x79);
    assert!(size_of::<EyeDataMmap>() == 0xebc);
    assert!(size_of::<EyeServerMmap>() <= SHM_SIZE);
    assert!(size_of::<libc::pthread_mutex_t>() <= 0x30);
    assert!(8 % align_of::<libc::pthread_mutex_t>() == 0);
};

#[derive(Parser)]
#[command(about = "Send Steam Frame eye tracking from shared memory over OSC")]
struct Args {
    /// What to send: VRChat avatar parameters, or VRCFaceTracking's ETVR Tracking Module format
    #[arg(long, value_enum, default_value_t = OutputKind::Vrchat)]
    output: OutputKind,
    /// OSC destination as HOST:PORT, or "auto" for the PC that Steam Link is streaming from
    #[arg(long, default_value = "auto")]
    target: String,
    /// OSC port used with --target auto [default: 9000 for vrchat, 8889 for etvr]
    #[arg(long)]
    port: Option<u16>,
    /// Parameter name prefix; "" or "/" for none
    #[arg(long, default_value = "/FT")]
    prefix: String,
    /// Send unsmoothed values (eyelid remapping still applies)
    #[arg(long)]
    raw: bool,
    /// One Euro minimum cutoff in Hz for gaze; lower is steadier at rest
    #[arg(long, default_value_t = 0.4)]
    gaze_min_cutoff: f32,
    /// One Euro beta for gaze; higher follows fast eye movements with less lag
    #[arg(long, default_value_t = 0.8)]
    gaze_beta: f32,
    /// One Euro derivative cutoff in Hz for gaze; lower keeps tracker noise from loosening the filter
    #[arg(long, default_value_t = 0.5)]
    gaze_d_cutoff: f32,
    /// Gaze changes smaller than this (1.0 = 45°) are ignored so the eyes stay put while fixating
    #[arg(long, default_value_t = 0.03)]
    gaze_deadzone: f32,
    /// Hold the gaze while either eye's Frame openness is below this; 0 disables
    #[arg(long, default_value_t = 0.5)]
    gaze_hold_below: f32,
    /// Send each eye's own gaze instead of the combined gaze for both eyes (jittery on the Frame)
    #[arg(long)]
    independent_eyes: bool,
    /// One Euro minimum cutoff in Hz for eyelids
    #[arg(long, default_value_t = 6.0)]
    lid_min_cutoff: f32,
    /// One Euro beta for eyelids
    #[arg(long, default_value_t = 5.0)]
    lid_beta: f32,
    /// Frame openness at or below this counts as fully closed
    #[arg(long, default_value_t = 0.30)]
    lid_closed: f32,
    /// Frame openness of a relaxed open eye (VRCFT 0.75)
    #[arg(long, default_value_t = 0.80)]
    lid_open: f32,
    /// Frame openness where widening begins; between --lid-open and this the eye stays at VRCFT 0.75
    #[arg(long, default_value_t = 0.92)]
    lid_widen_start: f32,
    /// Frame openness of a fully widened eye (VRCFT 1.0)
    #[arg(long, default_value_t = 1.00)]
    lid_wide: f32,
    /// Fixed multiplier on the left eye's Frame openness; overrides auto calibration for that eye
    #[arg(long)]
    lid_scale_left: Option<f32>,
    /// Fixed multiplier on the right eye's Frame openness; overrides auto calibration for that eye
    #[arg(long)]
    lid_scale_right: Option<f32>,
    /// Turn off learning each eye's relaxed openness (eyes without a fixed scale use 1.0)
    #[arg(long)]
    no_lid_calibration: bool,
    /// Where learned eyelid calibration is kept between runs [default: ~/.config/frameeyeosc/calibration]
    #[arg(long)]
    calibration_file: Option<PathBuf>,
    /// Pull both eyelids toward their average when they differ by less than this (VRCFT units);
    /// larger differences such as winks pass through untouched. 0 disables
    #[arg(long, default_value_t = 0.4)]
    lid_sync: f32,
    /// Settings file, re-read while running; options given here win over it
    /// [default: ~/.config/frameeyeosc/config.json]
    #[arg(long)]
    config: Option<PathBuf>,
}

/// One Euro filter: smooths hard while the signal is still and loosens up as it moves fast.
#[derive(Clone, Copy)]
struct OneEuro {
    min_cutoff: f32,
    beta: f32,
    d_cutoff: f32,
    value: Option<f32>,
    velocity: f32,
}

impl OneEuro {
    fn new(min_cutoff: f32, beta: f32, d_cutoff: f32) -> Self {
        Self {
            min_cutoff,
            beta,
            d_cutoff,
            value: None,
            velocity: 0.0,
        }
    }

    fn alpha(cutoff: f32, dt: f32) -> f32 {
        let tau = 1.0 / (2.0 * std::f32::consts::PI * cutoff);
        1.0 / (1.0 + tau / dt)
    }

    fn filter(&mut self, x: f32, dt: f32) -> f32 {
        let Some(prev) = self.value else {
            self.value = Some(x);
            return x;
        };
        self.velocity += Self::alpha(self.d_cutoff, dt) * ((x - prev) / dt - self.velocity);
        let cutoff = self.min_cutoff + self.beta * self.velocity.abs();
        let y = prev + Self::alpha(cutoff, dt) * (x - prev);
        self.value = Some(y);
        y
    }

    fn reset(&mut self) {
        self.value = None;
        self.velocity = 0.0;
    }
}

/// Backlash deadzone: the output only moves once the input drifts more than `width` away,
/// which pins the eyes during fixation without adding delay to large movements.
#[derive(Clone, Copy)]
struct Deadzone {
    width: f32,
    value: Option<f32>,
}

impl Deadzone {
    fn new(width: f32) -> Self {
        Self { width, value: None }
    }

    fn apply(&mut self, x: f32) -> f32 {
        let y = self
            .value
            .map_or(x, |held| held.clamp(x - self.width, x + self.width));
        self.value = Some(y);
        y
    }

    fn reset(&mut self) {
        self.value = None;
    }
}

/// Filters for the six gaze values and two eyelids, clocked by the eye server's sample time.
struct Smoother {
    gaze: [OneEuro; 6],
    deadzones: [Deadzone; 6],
    lids: [OneEuro; 2],
    last_time: Option<f64>,
    last_gaze: Option<[f32; 6]>,
}

impl Smoother {
    fn new(settings: &Settings) -> Self {
        Self {
            gaze: [OneEuro::new(settings.gaze_min_cutoff, settings.gaze_beta, settings.gaze_d_cutoff); 6],
            deadzones: [Deadzone::new(settings.gaze_deadzone); 6],
            lids: [OneEuro::new(settings.lid_min_cutoff, settings.lid_beta, LID_D_CUTOFF); 2],
            last_time: None,
            last_gaze: None,
        }
    }

    /// Take new filter parameters without dropping the filters' state, so the output does not jump.
    fn configure(&mut self, settings: &Settings) {
        for filter in &mut self.gaze {
            filter.min_cutoff = settings.gaze_min_cutoff;
            filter.beta = settings.gaze_beta;
            filter.d_cutoff = settings.gaze_d_cutoff;
        }
        for filter in &mut self.lids {
            filter.min_cutoff = settings.lid_min_cutoff;
            filter.beta = settings.lid_beta;
        }
        for deadzone in &mut self.deadzones {
            deadzone.width = settings.gaze_deadzone;
        }
    }

    /// `hold_gaze` keeps the previous gaze while the eyes are mostly shut, where the Frame's gaze jumps around.
    fn apply(&mut self, time: f64, gaze: &mut [f32; 6], lids: &mut [f32; 2], hold_gaze: bool) {
        let dt = match self.last_time {
            Some(last) if time > last && time - last < MAX_GAP => (time - last) as f32,
            Some(last) if time <= last => NOMINAL_DT,
            _ => {
                self.reset();
                NOMINAL_DT
            }
        };
        // Set after the match: reset() clears last_time, and every later sample would reset again.
        self.last_time = Some(time);
        match self.last_gaze {
            Some(last) if hold_gaze => *gaze = last,
            _ => {
                for ((value, filter), deadzone) in
                    gaze.iter_mut().zip(&mut self.gaze).zip(&mut self.deadzones)
                {
                    *value = deadzone.apply(filter.filter(*value, dt));
                }
                self.last_gaze = Some(*gaze);
            }
        }
        for (value, filter) in lids.iter_mut().zip(&mut self.lids) {
            *value = filter.filter(*value, dt);
        }
    }

    fn reset(&mut self) {
        self.gaze.iter_mut().chain(&mut self.lids).for_each(OneEuro::reset);
        self.deadzones.iter_mut().for_each(Deadzone::reset);
        self.last_time = None;
        self.last_gaze = None;
    }
}

/// Learns each eye's relaxed openness while in use, so a face that opens one eye less than the
/// other still maps both eyes' normal state onto --lid-open.
struct LidCalibration {
    histograms: [Vec<f32>; 2],
    relaxed: [f32; 2],
    decay: f32,
    path: Option<PathBuf>,
    saved: [f32; 2],
    last_save: Instant,
}

impl LidCalibration {
    /// Start from the saved calibration if there is one, else assume both eyes relax at `default`.
    fn load(path: Option<PathBuf>, default: f32) -> Self {
        let saved = path
            .as_deref()
            .and_then(|path| fs::read_to_string(path).ok())
            .map_or([default; 2], |text| parse_calibration(&text, default));
        Self {
            histograms: [vec![0.0; CAL_BINS], vec![0.0; CAL_BINS]],
            relaxed: saved,
            decay: 0.5_f32.powf(1.0 / CAL_HALF_LIFE_SAMPLES),
            path,
            saved,
            last_save: Instant::now(),
        }
    }

    fn observe(&mut self, openness: [f32; 2]) {
        let decay = self.decay;
        for ((histogram, relaxed), reading) in self
            .histograms
            .iter_mut()
            .zip(&mut self.relaxed)
            .zip(openness)
        {
            histogram.iter_mut().for_each(|weight| *weight *= decay);
            if reading > CAL_GATE * *relaxed && reading < 1.0 {
                histogram[((reading / CAL_BIN_WIDTH) as usize).min(CAL_BINS - 1)] += 1.0;
            }
            let total: f32 = histogram.iter().sum();
            if total < CAL_WARMUP_WEIGHT {
                continue;
            }
            let mut cumulative = 0.0;
            if let Some(bin) = histogram.iter().position(|weight| {
                cumulative += weight;
                cumulative >= total / 2.0
            }) {
                *relaxed = (bin as f32 + 0.5) * CAL_BIN_WIDTH;
            }
        }
    }

    /// Per-eye multipliers that bring each eye's relaxed openness to `lid_open`.
    fn scales(&self, lid_open: f32) -> [f32; 2] {
        self.relaxed
            .map(|relaxed| (lid_open / relaxed).clamp(CAL_SCALE_RANGE.0, CAL_SCALE_RANGE.1))
    }

    fn save_if_due(&mut self) {
        if self.path.is_none() || self.last_save.elapsed() < CAL_SAVE_INTERVAL {
            return;
        }
        self.last_save = Instant::now();
        if self
            .relaxed
            .iter()
            .zip(&self.saved)
            .all(|(now, saved)| (now - saved).abs() < 0.001)
        {
            return;
        }
        self.save();
    }

    fn save(&mut self) {
        let Some(path) = &self.path else {
            return;
        };
        match write_calibration(path, self.relaxed) {
            Ok(()) => {
                let [left, right] = self.relaxed;
                eprintln!("Saved eyelid calibration: left relaxes at {left:.3}, right at {right:.3}");
                self.saved = self.relaxed;
            }
            Err(error) => eprintln!("Could not save {}: {error}", path.display()),
        }
    }

    /// Forget what was learned and start over from `default`, saving that right away.
    fn reset(&mut self, default: f32) {
        self.histograms.iter_mut().for_each(|histogram| histogram.fill(0.0));
        self.relaxed = [default; 2];
        self.save();
    }
}

/// Saved as `left_relaxed=0.818` / `right_relaxed=0.777` lines; anything unreadable falls back to `default`.
fn parse_calibration(text: &str, default: f32) -> [f32; 2] {
    let value = |key: &str| {
        text.lines()
            .find_map(|line| line.strip_prefix(key)?.strip_prefix('=')?.trim().parse::<f32>().ok())
            .filter(|value| (0.3..=1.2).contains(value))
            .unwrap_or(default)
    };
    [value("left_relaxed"), value("right_relaxed")]
}

fn write_calibration(path: &Path, [left, right]: [f32; 2]) -> io::Result<()> {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    let temporary = path.with_extension("tmp");
    fs::write(
        &temporary,
        format!("left_relaxed={left:.4}\nright_relaxed={right:.4}\n"),
    )?;
    fs::rename(temporary, path)
}

/// Blend the two eyelids together in proportion to how close they already are:
/// equal lids stay equal, small asymmetries fade out, and a wink (large difference) is left alone.
fn sync_lids([left, right]: [f32; 2], threshold: f32) -> [f32; 2] {
    if threshold <= 0.0 {
        return [left, right];
    }
    let weight = (1.0 - (left - right).abs() / threshold).clamp(0.0, 1.0);
    let average = (left + right) / 2.0;
    [left + weight * (average - left), right + weight * (average - right)]
}

/// Map Frame eye openness onto VRCFT EyeLid, where 0 is closed, 0.75 relaxed open and 1 widened.
/// A held-closed eye reads ~0.2 on the Frame rather than 0, hence the closed threshold.
/// A relaxed eye wanders between ~0.75 and ~0.9, so widening only starts past a deadzone.
fn lid_to_vrcft(openness: f32, settings: &Settings) -> f32 {
    let Settings {
        lid_closed,
        lid_open,
        lid_widen_start,
        lid_wide,
        ..
    } = *settings;
    if openness <= lid_open {
        0.75 * ((openness - lid_closed) / (lid_open - lid_closed)).clamp(0.0, 1.0)
    } else if openness > lid_widen_start && lid_wide > lid_widen_start {
        let widen = (openness - lid_widen_start) / (lid_wide - lid_widen_start);
        0.75 + 0.25 * widen.clamp(0.0, 1.0)
    } else {
        0.75
    }
}

/// The ETVR Tracking Module treats 1.0 as a relaxed open eye, so widening is cut off there.
fn lid_to_etvr(vrcft: f32) -> f32 {
    (vrcft * ETVR_LID_SCALE).clamp(0.0, 1.0)
}

struct EyeSource {
    map: MmapMut,
    inode: u64,
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
    fixation_point: [f32; 3],
    openness: [f32; 2],
}

impl EyeData {
    fn is_finite(&self) -> bool {
        self.sample_time.is_finite()
            && self.gaze.iter().flatten().all(|value| value.is_finite())
            && self.fixation_point.iter().all(|value| value.is_finite())
            && self.openness.iter().all(|value| value.is_finite())
    }
}

enum Next {
    /// Nothing new within the timeout.
    Waiting,
    /// A new record, but the eye tracker is not producing.
    Stopped,
    Sample(EyeData),
}

impl EyeSource {
    fn open() -> Result<Self, Box<dyn Error>> {
        let file = OpenOptions::new().read(true).write(true).open(SOURCE)?;
        let metadata = file.metadata()?;
        if metadata.len() < SHM_SIZE as u64 {
            return Err(format!("{SOURCE}: shared memory is too small").into());
        }
        let map = unsafe { MmapOptions::new().len(SHM_SIZE).map_mut(&file)? };
        let source = Self {
            map,
            inode: metadata.ino(),
        };
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

    /// True when the path now points at a different file (or none) than the one we mapped.
    fn is_stale(&self) -> bool {
        fs::metadata(SOURCE).map_or(true, |metadata| metadata.ino() != self.inode)
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

    fn next(&mut self, timeout: Duration) -> io::Result<Next> {
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
                Next::Sample(EyeData {
                    sample_time: record.sample_time,
                    gaze: record.gaze_direction,
                    fixation_point: record.fixation_point,
                    openness: record.openness,
                })
            } else {
                Next::Stopped
            }
        } else {
            Next::Waiting
        };
        drop(guard);
        Ok(data)
    }
}

/// Where OSC goes: a fixed host, or the PC that Steam Link is currently streaming from.
#[derive(Clone, PartialEq)]
enum Target {
    Fixed { host: String, port: u16 },
    SteamLink { port: u16 },
}

impl Target {
    fn of(settings: &Settings) -> Self {
        let port = settings.port();
        if settings.host == "auto" {
            Self::SteamLink { port }
        } else {
            Self::Fixed {
                host: settings.host.clone(),
                port,
            }
        }
    }
}

/// UDP sender that re-resolves its target periodically and reconnects when it changes.
struct Output {
    target: Target,
    socket: Option<(UdpSocket, SocketAddr)>,
    last_resolve: Option<Instant>,
}

impl Output {
    fn new(target: Target) -> Self {
        Self {
            target,
            socket: None,
            last_resolve: None,
        }
    }

    /// Switch to another target, looking it up on the next refresh instead of up to 5 s later.
    fn set_target(&mut self, target: Target) {
        if target != self.target {
            self.target = target;
            self.last_resolve = None;
        }
    }

    fn refresh(&mut self) -> io::Result<()> {
        if let Some(resolved) = self.last_resolve {
            // A fixed host is looked up once (like before the config file existed) unless that failed.
            let settled = matches!(self.target, Target::Fixed { .. }) && self.socket.is_some();
            if settled || resolved.elapsed() < RESOLVE_INTERVAL {
                return Ok(());
            }
        }
        self.last_resolve = Some(Instant::now());
        let wanted = match &self.target {
            Target::Fixed { host, port } => match (host.as_str(), *port).to_socket_addrs() {
                Ok(mut addrs) => addrs.next(),
                Err(error) => {
                    eprintln!("Could not resolve {host}: {error}; retrying");
                    None
                }
            },
            Target::SteamLink { port } => steam_link_peer().map(|ip| SocketAddr::new(ip, *port)),
        };
        if wanted == self.socket.as_ref().map(|(_, addr)| *addr) {
            return Ok(());
        }
        self.socket = match wanted {
            Some(addr) => {
                let socket = UdpSocket::bind(if addr.is_ipv4() { "0.0.0.0:0" } else { "[::]:0" })?;
                socket.connect(addr)?;
                eprintln!("Sending OSC to {addr}");
                Some((socket, addr))
            }
            None if matches!(self.target, Target::SteamLink { .. }) => {
                eprintln!("No Steam Link connection found; waiting for one");
                None
            }
            None => None,
        };
        Ok(())
    }

    fn addr(&self) -> Option<SocketAddr> {
        self.socket.as_ref().map(|(_, addr)| *addr)
    }

    fn send(&self, addr: String, args: Vec<OscType>) -> Result<(), Box<dyn Error>> {
        let Some((socket, _)) = &self.socket else {
            return Ok(());
        };
        let packet = OscPacket::Message(OscMessage { addr, args });
        match socket.send(&encoder::encode(&packet)?) {
            // Nothing is listening yet (e.g. VRChat is closed); keep running rather than exit.
            Err(error) if error.kind() == io::ErrorKind::ConnectionRefused => Ok(()),
            result => result.map(drop).map_err(Into::into),
        }
    }
}

/// The PC Steam Link is streaming from: the remote end of the `vrlink` client's connected UDP socket.
/// Over the bundled wireless adapter this is the PC's side of the direct link, not its home LAN address.
fn steam_link_peer() -> Option<IpAddr> {
    let inodes = vrlink_socket_inodes();
    if inodes.is_empty() {
        return None;
    }
    ["/proc/net/udp", "/proc/net/udp6"].iter().find_map(|path| {
        let table = fs::read_to_string(path).ok()?;
        connected_udp_peer(&table, &inodes)
    })
}

/// Socket inodes held by the Steam Link client process. Matched by executable name, because its
/// main thread renames itself (comm reads "vrlinkrunthread").
fn vrlink_socket_inodes() -> HashSet<u64> {
    let Ok(processes) = fs::read_dir("/proc") else {
        return HashSet::new();
    };
    processes
        .flatten()
        .filter(|process| {
            fs::read_link(process.path().join("exe"))
                .is_ok_and(|exe| exe.file_name().is_some_and(|name| name == "vrlink"))
        })
        .filter_map(|process| fs::read_dir(process.path().join("fd")).ok())
        .flatten()
        .flatten()
        .filter_map(|fd| {
            let link = fs::read_link(fd.path()).ok()?;
            link.to_str()?
                .strip_prefix("socket:[")?
                .strip_suffix(']')?
                .parse()
                .ok()
        })
        .collect()
}

/// Remote address of the first connected (state 01), non-loopback socket in a /proc/net/udp{,6}
/// table whose inode is in `inodes`.
fn connected_udp_peer(table: &str, inodes: &HashSet<u64>) -> Option<IpAddr> {
    table.lines().skip(1).find_map(|line| {
        let fields: Vec<&str> = line.split_whitespace().collect();
        let (remote, state, inode) = (fields.get(2)?, fields.get(3)?, fields.get(9)?);
        if *state != "01" || !inodes.contains(&inode.parse().ok()?) {
            return None;
        }
        let ip = parse_proc_ip(remote.split(':').next()?)?;
        (!ip.is_loopback() && !ip.is_unspecified()).then_some(ip)
    })
}

/// /proc/net writes addresses as 32-bit words in host (little-endian) byte order, in hex.
fn parse_proc_ip(hex: &str) -> Option<IpAddr> {
    if hex.len() % 8 != 0 {
        return None;
    }
    let words = (0..hex.len() / 8)
        .map(|i| u32::from_str_radix(hex.get(i * 8..i * 8 + 8)?, 16).ok())
        .collect::<Option<Vec<u32>>>()?;
    match words[..] {
        [word] => Some(IpAddr::V4(Ipv4Addr::from(word.to_le_bytes()))),
        [_, _, _, _] => {
            let mut bytes = [0u8; 16];
            for (chunk, word) in bytes.chunks_mut(4).zip(&words) {
                chunk.copy_from_slice(&word.to_le_bytes());
            }
            let ip = Ipv6Addr::from(bytes);
            Some(ip.to_ipv4_mapped().map_or(IpAddr::V6(ip), IpAddr::V4))
        }
        _ => None,
    }
}

// Like Steam Link's OSC sender, ±45° maps to ±1; +Y is up (VRCFT convention, unverified on hardware).
fn gaze_angles([x, y, z]: [f32; 3]) -> [f32; 2] {
    let scale = 4.0 / std::f32::consts::PI;
    [
        (x.atan2(-z) * scale).clamp(-1.0, 1.0),
        (y.atan2(-z) * scale).clamp(-1.0, 1.0),
    ]
}

/// One eye-server sample worked through the eyelid mapping and the filters.
struct Sample {
    openness: [f32; 2],
    // After each eye's scale: what the --lid-* thresholds are compared against.
    openness_scaled: [f32; 2],
    // Left x/y, right x/y and combined x/y in -1..1, before smoothing.
    raw_gaze: [f32; 6],
    // The same layout, as sent.
    gaze: [f32; 6],
    // VRCFT eyelids, as sent to VRChat.
    lids: [f32; 2],
}

/// Per-eye multipliers on Frame openness: the fixed ones, else the learned ones, else 1.
fn lid_scales(settings: &Settings, calibration: &LidCalibration) -> [f32; 2] {
    let learned = if settings.lid_calibration {
        calibration.scales(settings.lid_open)
    } else {
        [1.0; 2]
    };
    [
        settings.lid_scale_left.unwrap_or(learned[0]),
        settings.lid_scale_right.unwrap_or(learned[1]),
    ]
}

fn process(settings: &Settings, smoother: &mut Smoother, scales: [f32; 2], data: &EyeData) -> Sample {
    let [x, y] = gaze_angles(data.fixation_point);
    let [left, right] = data.gaze.map(gaze_angles);
    let raw_gaze = [left[0], left[1], right[0], right[1], x, y];
    // Each eye wobbles on its own (L/R changes correlate only ~0.35), so share the combined gaze by default.
    let mut gaze = if settings.independent_eyes {
        raw_gaze
    } else {
        [x, y, x, y, x, y]
    };
    let openness_scaled = [0, 1].map(|eye| data.openness[eye] * scales[eye]);
    let mut lids = openness_scaled.map(|openness| lid_to_vrcft(openness, settings));
    if !settings.raw {
        let hold_gaze = data
            .openness
            .iter()
            .any(|openness| *openness < settings.gaze_hold_below);
        smoother.apply(data.sample_time, &mut gaze, &mut lids, hold_gaze);
    }
    Sample {
        openness: data.openness,
        openness_scaled,
        raw_gaze,
        gaze,
        lids: sync_lids(lids, settings.lid_sync),
    }
}

/// Eyelids on the scale of the given output.
fn output_lids(output: OutputKind, lids: [f32; 2]) -> [f32; 2] {
    match output {
        OutputKind::Vrchat => lids,
        OutputKind::Etvr => lids.map(lid_to_etvr),
    }
}

/// The OSC messages for one sample. VRChat gets the full VRCFT v2 eye set. The ETVR Tracking Module
/// gets per-eye values only: EyeX/EyeY switch it to a single-eye mode that reads an eyelid we do not send.
fn osc_messages(settings: &Settings, sample: &Sample) -> Vec<(String, OscType)> {
    let prefix = format!("/avatar/parameters{}", settings.prefix);
    let [left_x, left_y, right_x, right_y, x, y] = sample.gaze;
    let [lid_left, lid_right] = output_lids(settings.output, sample.lids);
    let mut values = vec![
        ("EyeLeftX", left_x),
        ("EyeLeftY", left_y),
        ("EyeRightX", right_x),
        ("EyeRightY", right_y),
        ("EyeLidLeft", lid_left),
        ("EyeLidRight", lid_right),
    ];
    let mut messages = Vec::with_capacity(9);
    if settings.output == OutputKind::Vrchat {
        messages.push((format!("{prefix}/EyeTrackingActive"), OscType::Bool(true)));
        values.extend([("EyeX", x), ("EyeY", y)]);
    }
    messages.extend(
        values
            .into_iter()
            .map(|(suffix, value)| (format!("{prefix}/v2/{suffix}"), OscType::Float(value))),
    );
    messages
}

fn send_inactive(output: &Output, prefix: &str) -> Result<(), Box<dyn Error>> {
    output.send(
        format!("/avatar/parameters{prefix}/EyeTrackingActive"),
        vec![OscType::Bool(false)],
    )
}

/// Where VRChat is being told that eye tracking is active, if anywhere. When this changes,
/// the old destination gets a final "inactive" so the avatar's eyes do not freeze.
fn vrchat_stream(settings: &Settings) -> Option<(&str, u16, &str)> {
    (settings.sending && settings.output == OutputKind::Vrchat)
        .then(|| (settings.host.as_str(), settings.port(), settings.prefix.as_str()))
}

/// Everything the main loop keeps between samples.
struct Bridge {
    config: Config,
    settings: Settings,
    output: Output,
    smoother: Smoother,
    calibration: LidCalibration,
    started: SystemTime,
    active_since: Option<Instant>,
    last_data: Option<Instant>,
    latest: Option<Sample>,
    // When the samples of the last RATE_WINDOW went out.
    sent: VecDeque<Instant>,
}

impl Bridge {
    /// Take changed settings without restarting.
    fn apply(&mut self, reload: Reload) -> Result<(), Box<dyn Error>> {
        let Reload {
            settings,
            reset_calibration,
        } = reload;
        let stream = vrchat_stream(&self.settings);
        if self.active_since.is_some() && stream.is_some() && stream != vrchat_stream(&settings) {
            send_inactive(&self.output, &self.settings.prefix)?;
        }
        self.output.set_target(Target::of(&settings));
        self.smoother.configure(&settings);
        if reset_calibration {
            eprintln!("Starting eyelid calibration over");
            self.calibration.reset(settings.lid_open);
        }
        self.settings = settings;
        Ok(())
    }

    fn on_sample(&mut self, data: EyeData) -> Result<(), Box<dyn Error>> {
        let now = Instant::now();
        self.last_data = Some(now);
        let since = *self.active_since.get_or_insert(now);
        if self.settings.lid_calibration && since.elapsed() >= CAL_SETTLE {
            self.calibration.observe(data.openness);
            self.calibration.save_if_due();
        }
        let scales = lid_scales(&self.settings, &self.calibration);
        let sample = process(&self.settings, &mut self.smoother, scales, &data);
        if self.settings.sending {
            for (addr, arg) in osc_messages(&self.settings, &sample) {
                self.output.send(addr, vec![arg])?;
            }
            if self.output.addr().is_some() {
                self.sent.push_back(now);
            }
        }
        while self
            .sent
            .front()
            .is_some_and(|sent| sent.elapsed() >= RATE_WINDOW)
        {
            self.sent.pop_front();
        }
        self.latest = Some(sample);
        Ok(())
    }

    fn on_lost(&mut self) -> Result<(), Box<dyn Error>> {
        if vrchat_stream(&self.settings).is_some() {
            send_inactive(&self.output, &self.settings.prefix)?;
        }
        self.smoother.reset();
        self.active_since = None;
        self.latest = None;
        Ok(())
    }

    fn status(&self) -> Status<'_> {
        let settings = &self.settings;
        let pair = |values: [f32; 6], first: usize| status::round([values[first], values[first + 1]]);
        Status {
            version: 1,
            pid: std::process::id(),
            time: status::unix_time(SystemTime::now()),
            started: status::unix_time(self.started),
            sending: settings.sending,
            output: settings.output,
            target_mode: if settings.host == "auto" { "auto" } else { "fixed" },
            target: self.output.addr().map(|addr| addr.to_string()),
            rate: self
                .sent
                .iter()
                .filter(|sent| sent.elapsed() < RATE_WINDOW)
                .count() as f32,
            tracking: self.active_since.is_some(),
            raw: self.latest.as_ref().map(|sample| RawValues {
                openness: status::round(sample.openness),
                openness_scaled: status::round(sample.openness_scaled),
                gaze: pair(sample.raw_gaze, 4),
                gaze_left: pair(sample.raw_gaze, 0),
                gaze_right: pair(sample.raw_gaze, 2),
            }),
            sent: self.latest.as_ref().map(|sample| SentValues {
                lids: status::round(output_lids(settings.output, sample.lids)),
                lids_vrcft: status::round(sample.lids),
                gaze: pair(sample.gaze, 4),
                gaze_left: pair(sample.gaze, 0),
                gaze_right: pair(sample.gaze, 2),
            }),
            calibration: CalibrationStatus {
                enabled: settings.lid_calibration,
                relaxed: status::round(self.calibration.relaxed),
                scales: status::round(lid_scales(settings, &self.calibration)),
                learning: settings.lid_calibration
                    && self
                        .active_since
                        .is_some_and(|since| since.elapsed() >= CAL_SETTLE),
            },
            config_path: self.config.path.as_deref(),
            calibration_path: self.calibration.path.as_deref(),
            config_error: self.config.error.as_deref(),
            locked: &self.config.locked,
            effective: settings,
        }
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let matches = Args::command().get_matches();
    let args = Args::from_arg_matches(&matches)?;
    if args.target != "auto" && config::split_target(&args.target).is_none() {
        return Err("--target must be HOST:PORT or auto".into());
    }
    let in_config_dir = |name: &str| Some(config::config_dir()?.join(name));
    let calibration_path = args.calibration_file.clone().or_else(|| in_config_dir("calibration"));
    let config_path = args.config.clone().or_else(|| in_config_dir("config.json"));
    let mut config = Config::new(config_path, args, config::given_options(&matches));
    let settings = config.load()?;
    let mut bridge = Bridge {
        output: Output::new(Target::of(&settings)),
        smoother: Smoother::new(&settings),
        calibration: LidCalibration::load(calibration_path, settings.lid_open),
        config,
        settings,
        started: SystemTime::now(),
        active_since: None,
        last_data: None,
        latest: None,
        sent: VecDeque::new(),
    };
    let mut status_file = StatusFile::new(status::status_path());
    let mut source = EyeSource::open()?;
    eprintln!("Reading {SOURCE}");
    loop {
        if let Some(reload) = bridge.config.poll() {
            bridge.apply(reload)?;
        }
        bridge.output.refresh()?;
        match source.next(POLL)? {
            Next::Sample(data) if data.is_finite() => bridge.on_sample(data)?,
            // Short waits are normal; only a whole second without data means tracking stopped.
            Next::Waiting if bridge.last_data.is_some_and(|last| last.elapsed() < TIMEOUT) => {}
            _ if bridge.active_since.is_some() => bridge.on_lost()?,
            // Idle: if the eye server recreated its shared memory, our mapping would go silent forever.
            _ if source.is_stale() => {
                eprintln!("{SOURCE} was replaced; reopening");
                source = EyeSource::open()?;
            }
            _ => {}
        }
        if status_file.due() {
            status_file.write(&bridge.status());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn settings() -> Settings {
        Settings::default()
    }

    #[test]
    fn deadzone_ignores_small_moves_and_follows_large_ones() {
        let mut deadzone = Deadzone::new(0.03);
        assert_eq!(deadzone.apply(0.10), 0.10);
        assert_eq!(deadzone.apply(0.12), 0.10);
        assert_eq!(deadzone.apply(0.08), 0.10);
        assert!((deadzone.apply(0.30) - 0.27).abs() < 1e-6);
        assert!((deadzone.apply(0.29) - 0.27).abs() < 1e-6);
    }

    #[test]
    fn one_euro_damps_noise() {
        let mut filter = OneEuro::new(0.4, 0.8, 0.5);
        let mut last = 0.0;
        for i in 0..90 {
            let noise = if i % 2 == 0 { 0.02 } else { -0.02 };
            last = filter.filter(noise, NOMINAL_DT);
        }
        assert!(last.abs() < 0.005, "{last}");
    }

    #[test]
    fn smoother_keeps_filter_state_between_samples() {
        let mut smoother = Smoother::new(&Settings {
            gaze_deadzone: 0.0,
            ..settings()
        });
        let mut lids = [0.75; 2];
        let mut gaze = [0.0; 6];
        smoother.apply(0.0, &mut gaze, &mut lids, false);
        let mut jumped = [0.5; 6];
        smoother.apply(NOMINAL_DT as f64, &mut jumped, &mut lids, false);
        assert!(jumped[0] < 0.5, "a filtered step must not pass through untouched: {}", jumped[0]);
    }

    #[test]
    fn gaze_is_held_while_eyes_are_shut() {
        let mut smoother = Smoother::new(&settings());
        let mut lids = [0.75; 2];
        let mut gaze = [0.2; 6];
        smoother.apply(0.0, &mut gaze, &mut lids, false);
        let before = gaze;
        let mut jumped = [-0.4; 6];
        smoother.apply(NOMINAL_DT as f64, &mut jumped, &mut lids, true);
        assert_eq!(jumped, before);
    }

    #[test]
    fn proc_net_addresses_decode_in_host_byte_order() {
        assert_eq!(parse_proc_ip("1A4E230A"), Some(IpAddr::V4(Ipv4Addr::new(10, 35, 78, 26))));
        assert_eq!(
            parse_proc_ip("0000000000000000FFFF00001A4E230A"),
            Some(IpAddr::V4(Ipv4Addr::new(10, 35, 78, 26)))
        );
        assert_eq!(parse_proc_ip("xyz"), None);
    }

    #[test]
    fn steam_link_peer_is_the_connected_socket_of_vrlink() {
        let table = "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n\
            \x20 10: 014E230A:28A0 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 111 2 0 0\n\
            \x20 11: 0100007F:28A0 0100007F:1F90 01 00000000:00000000 00:00000000 00000000  1000        0 222 2 0 0\n\
            \x20 12: 014E230A:28A0 1A4E230A:28A0 01 00000000:00000000 00:00000000 00000000  1000        0 333 2 0 0\n";
        let inodes: HashSet<u64> = [111, 222, 333].into();
        assert_eq!(
            connected_udp_peer(table, &inodes),
            Some(IpAddr::V4(Ipv4Addr::new(10, 35, 78, 26)))
        );
        assert_eq!(connected_udp_peer(table, &[111, 222].into()), None);
    }

    fn wobble(i: usize) -> f32 {
        [-0.02, 0.0, 0.02][i % 3]
    }

    #[test]
    fn lid_calibration_learns_each_eyes_relaxed_openness() {
        let mut calibration = LidCalibration::load(None, 0.80);
        for i in 0..90 * 30 {
            calibration.observe([0.82 + wobble(i), 0.78 + wobble(i)]);
        }
        let [left, right] = calibration.relaxed;
        assert!((left - 0.82).abs() < 0.01 && (right - 0.78).abs() < 0.01, "{left} {right}");
        let [scale_left, scale_right] = calibration.scales(0.80);
        assert!((0.82 * scale_left - 0.78 * scale_right).abs() < 0.01);
    }

    #[test]
    fn lid_calibration_shrugs_off_a_minute_of_squinting() {
        let mut calibration = LidCalibration::load(None, 0.80);
        for i in 0..90 * 600 {
            calibration.observe([0.82 + wobble(i); 2]);
        }
        for _ in 0..90 * 60 {
            calibration.observe([0.70, 0.55]);
        }
        let [left, right] = calibration.relaxed;
        // A light squint counts toward the estimate but barely moves it; a deep one is ignored.
        assert!((left - 0.82).abs() < 0.02, "{left}");
        assert!((right - 0.82).abs() < 0.01, "{right}");
    }

    #[test]
    fn lid_calibration_does_not_ratchet_up_on_widely_spread_readings() {
        let mut calibration = LidCalibration::load(None, 0.80);
        let spread = [0.63, 0.70, 0.76, 0.82, 0.87, 0.93, 0.97];
        for i in 0..90 * 120 {
            calibration.observe([spread[i % spread.len()]; 2]);
        }
        assert!((calibration.relaxed[0] - 0.82).abs() < 0.01, "{}", calibration.relaxed[0]);
    }

    #[test]
    fn lid_calibration_file_round_trips() {
        let dir = std::env::temp_dir().join(format!("frameeyeosc-test-{}", std::process::id()));
        let path = dir.join("calibration");
        write_calibration(&path, [0.818, 0.777]).unwrap();
        assert_eq!(LidCalibration::load(Some(path), 0.80).relaxed, [0.818, 0.777]);
        assert_eq!(parse_calibration("left_relaxed=abc\nright_relaxed=5\n", 0.80), [0.80, 0.80]);
        fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn lid_sync_evens_small_differences_but_keeps_winks() {
        let [left, right] = sync_lids([0.70, 0.75], 0.4);
        assert!((left - right).abs() < 0.01, "{left} {right}");
        assert_eq!(sync_lids([0.0, 0.75], 0.4), [0.0, 0.75]);
        assert_eq!(sync_lids([0.70, 0.75], 0.0), [0.70, 0.75]);
    }

    #[test]
    fn eyelids_map_onto_vrcft_scale() {
        let args = settings();
        assert_eq!(lid_to_vrcft(0.22, &args), 0.0);
        assert_eq!(lid_to_vrcft(0.80, &args), 0.75);
        assert_eq!(lid_to_vrcft(0.88, &args), 0.75);
        assert_eq!(lid_to_vrcft(1.00, &args), 1.0);
        assert!(lid_to_vrcft(0.55, &args) > 0.0 && lid_to_vrcft(0.55, &args) < 0.75);
    }

    #[test]
    fn etvr_eyelids_treat_relaxed_as_fully_open() {
        assert_eq!(lid_to_etvr(0.0), 0.0);
        assert_eq!(lid_to_etvr(0.375), 0.5);
        assert_eq!(lid_to_etvr(0.75), 1.0);
        // Widening does not reach the ETVR Tracking Module.
        assert_eq!(lid_to_etvr(1.0), 1.0);
        assert_eq!(output_lids(OutputKind::Vrchat, [0.375, 1.0]), [0.375, 1.0]);
        assert_eq!(output_lids(OutputKind::Etvr, [0.375, 1.0]), [0.5, 1.0]);
    }

    fn sample() -> Sample {
        Sample {
            openness: [0.8; 2],
            openness_scaled: [0.8; 2],
            raw_gaze: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6],
            gaze: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6],
            lids: [0.375, 0.75],
        }
    }

    fn sent(settings: &Settings) -> Vec<(String, OscType)> {
        osc_messages(settings, &sample())
    }

    #[test]
    fn vrchat_gets_the_full_eye_set() {
        let messages = sent(&settings());
        let addrs: Vec<&str> = messages.iter().map(|(addr, _)| addr.as_str()).collect();
        assert_eq!(
            addrs,
            [
                "/avatar/parameters/FT/EyeTrackingActive",
                "/avatar/parameters/FT/v2/EyeLeftX",
                "/avatar/parameters/FT/v2/EyeLeftY",
                "/avatar/parameters/FT/v2/EyeRightX",
                "/avatar/parameters/FT/v2/EyeRightY",
                "/avatar/parameters/FT/v2/EyeLidLeft",
                "/avatar/parameters/FT/v2/EyeLidRight",
                "/avatar/parameters/FT/v2/EyeX",
                "/avatar/parameters/FT/v2/EyeY",
            ]
        );
        assert_eq!(messages[0].1, OscType::Bool(true));
        assert_eq!(messages[6].1, OscType::Float(0.75));
    }

    #[test]
    fn etvr_gets_only_the_six_per_eye_values() {
        let messages = sent(&Settings {
            output: OutputKind::Etvr,
            ..settings()
        });
        let addrs: Vec<&str> = messages.iter().map(|(addr, _)| addr.as_str()).collect();
        assert_eq!(
            addrs,
            [
                "/avatar/parameters/FT/v2/EyeLeftX",
                "/avatar/parameters/FT/v2/EyeLeftY",
                "/avatar/parameters/FT/v2/EyeRightX",
                "/avatar/parameters/FT/v2/EyeRightY",
                "/avatar/parameters/FT/v2/EyeLidLeft",
                "/avatar/parameters/FT/v2/EyeLidRight",
            ]
        );
        assert_eq!(messages[4].1, OscType::Float(0.5));
        assert_eq!(messages[5].1, OscType::Float(1.0));
    }

    #[test]
    fn empty_prefix_sends_bare_names() {
        let messages = sent(&Settings {
            prefix: String::new(),
            ..settings()
        });
        assert_eq!(messages[0].0, "/avatar/parameters/EyeTrackingActive");
        assert_eq!(messages[1].0, "/avatar/parameters/v2/EyeLeftX");
    }

    #[test]
    fn each_output_has_its_default_port() {
        assert_eq!(settings().port(), 9000);
        let etvr = Settings {
            output: OutputKind::Etvr,
            ..settings()
        };
        assert_eq!(etvr.port(), 8889);
        assert_eq!(Settings { port: Some(9100), ..etvr }.port(), 9100);
        assert!(Target::of(&settings()) == Target::SteamLink { port: 9000 });
    }

    #[test]
    fn stopping_or_moving_the_vrchat_stream_is_noticed() {
        let base = settings();
        let paused = Settings {
            sending: false,
            ..settings()
        };
        let etvr = Settings {
            output: OutputKind::Etvr,
            ..settings()
        };
        let moved = Settings {
            port: Some(9001),
            ..settings()
        };
        assert!(vrchat_stream(&base).is_some());
        assert!(vrchat_stream(&paused).is_none() && vrchat_stream(&etvr).is_none());
        assert_ne!(vrchat_stream(&base), vrchat_stream(&moved));
        let tweaked = Settings {
            lid_open: 0.85,
            ..settings()
        };
        assert_eq!(vrchat_stream(&base), vrchat_stream(&tweaked));
    }

    #[test]
    fn lid_calibration_reset_starts_over_from_lid_open() {
        let mut calibration = LidCalibration::load(None, 0.80);
        for i in 0..90 * 30 {
            calibration.observe([0.90 + wobble(i); 2]);
        }
        calibration.reset(0.80);
        assert_eq!(calibration.relaxed, [0.80; 2]);
        assert!(calibration.histograms.iter().flatten().all(|weight| *weight == 0.0));
    }

    #[test]
    fn filters_take_new_parameters_without_losing_state() {
        let mut smoother = Smoother::new(&settings());
        let mut lids = [0.75; 2];
        let mut gaze = [0.0; 6];
        smoother.apply(0.0, &mut gaze, &mut lids, false);
        smoother.configure(&Settings {
            lid_min_cutoff: 10.0,
            gaze_deadzone: 0.0,
            ..settings()
        });
        assert_eq!(smoother.lids[0].min_cutoff, 10.0);
        assert_eq!(smoother.deadzones[0].width, 0.0);
        assert_eq!(smoother.lids[0].value, Some(0.75));
    }
}
