//! Steam Frame 0.5.0 eye bridge using the private version-4 shared-memory ABI.

mod capture;
mod config;
mod replay;
mod status;

use capture::{Capture, CaptureResult, CaptureState};
use clap::{CommandFactory, FromArgMatches, Parser};
use config::{Config, LidFit, OutputKind, Reload, Settings};
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
// The sideways gaze hold (--gaze-down-hold-x-deg) fades in over this many degrees further down.
const DOWN_HOLD_FADE_DEG: f32 = 10.0;
// The eye fit measures openness with the eyes on targets this far up and down (15° of 45°).
const LID_FIT_PITCH: f32 = 15.0 / 45.0;
// A fitted eye counts as closed from this share of the way from its closed reading to its open one,
// so readings a little above the eyes-shut average still close the eyelid. On the 2026-09-27 worn
// recording, 0.3 closed more blinks than no fit (53 vs 50 of 60), and 0.1 or 0.2 fewer.
const LID_FIT_CLOSED_MARGIN: f32 = 0.3;
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
    assert!(offset_of!(EyeDataMmap, estimate_extra) == 0x81);
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
    #[arg(long, default_value_t = 0.02)]
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
    /// Ignore an eye's gaze while its covariance is above this; the other eye moves both,
    /// or the gaze is held if both are above. An optional safety net; 0 disables
    #[arg(long, default_value_t = 0.0)]
    gaze_quality_limit: f32,
    /// Keep a closed eyelid fully closed for at least this long, so short blinks reach other players. 0 disables
    #[arg(long, default_value_t = 80.0)]
    blink_hold_ms: f32,
    /// Turn off the 3-sample median that drops one-sample dropouts in gaze and openness
    #[arg(long)]
    no_despike: bool,
    /// Close both eyes when one is closed and the other is below this (VRCFT units); winks pass. 0 disables
    #[arg(long, default_value_t = 0.35)]
    blink_sync_below: f32,
    /// Gaze that counts as straight ahead, left/right (-0.5..0.5 on the -1..1 scale; 1.0 = 45°)
    #[arg(long, default_value_t = 0.0, allow_negative_numbers = true)]
    gaze_offset_x: f32,
    /// Gaze that counts as straight ahead, up/down (-0.5..0.5)
    #[arg(long, default_value_t = 0.0, allow_negative_numbers = true)]
    gaze_offset_y: f32,
    /// How far the gaze goes left and right, from --gaze-offset-x (0.5..2)
    #[arg(long, default_value_t = 1.0)]
    gaze_gain_x: f32,
    /// How far the gaze goes up, from --gaze-offset-y (0.5..2)
    #[arg(long, default_value_t = 1.0)]
    gaze_gain_up: f32,
    /// How far the gaze goes down, from --gaze-offset-y (0.5..2)
    #[arg(long, default_value_t = 1.0)]
    gaze_gain_down: f32,
    /// Hold the sideways gaze when looking more than this many degrees down (fully 10° further down),
    /// where the Frame's x jumps; 0 disables
    #[arg(long, default_value_t = 28.0)]
    gaze_down_hold_x_deg: f32,
    /// The left eye's own sideways zero point for per-eye gaze [default: --gaze-offset-x]
    #[arg(long, allow_negative_numbers = true)]
    gaze_offset_x_left: Option<f32>,
    /// The right eye's own sideways zero point for per-eye gaze [default: --gaze-offset-x]
    #[arg(long, allow_negative_numbers = true)]
    gaze_offset_x_right: Option<f32>,
    /// The left eye's own sideways gain for per-eye gaze [default: --gaze-gain-x]
    #[arg(long)]
    gaze_gain_x_left: Option<f32>,
    /// The right eye's own sideways gain for per-eye gaze [default: --gaze-gain-x]
    #[arg(long)]
    gaze_gain_x_right: Option<f32>,
    /// Settings file, re-read while running; options given here win over it
    /// [default: ~/.config/frameeyeosc/config.json]
    #[arg(long)]
    config: Option<PathBuf>,
    /// Write the eye server's raw samples to this CSV file until stopped, instead of sending anything
    #[arg(long, value_name = "FILE", conflicts_with = "replay")]
    record: Option<PathBuf>,
    /// Run a recorded CSV file through the processing and print how the output behaves, instead of sending
    #[arg(long, value_name = "FILE")]
    replay: Option<PathBuf>,
    /// With --replay, also write every processed sample to this CSV file
    #[arg(long, value_name = "FILE", requires = "replay")]
    replay_out: Option<PathBuf>,
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
    // The previous two samples' gaze angles and openness, for the 3-sample median.
    recent: [Option<[f32; 8]>; 2],
    // Sample time until which each eyelid is kept fully closed, and whether it went out closed last time.
    shut_until: [f64; 2],
    was_shut: [bool; 2],
    last_time: Option<f64>,
    // Left, right and combined gaze as last sent, to hold while the gaze is unreliable.
    last_gaze: [Option<[f32; 2]>; 3],
    // The up/down gaze the eyelid fit last used, kept while the gaze is held.
    lid_vertical: Option<f32>,
    // Left, right and combined raw x from the last sample above --gaze-down-hold-x-deg.
    down_hold_x: Option<[f32; 3]>,
}

impl Smoother {
    fn new(settings: &Settings) -> Self {
        Self {
            gaze: [OneEuro::new(settings.gaze_min_cutoff, settings.gaze_beta, settings.gaze_d_cutoff); 6],
            deadzones: [Deadzone::new(settings.gaze_deadzone); 6],
            lids: [OneEuro::new(settings.lid_min_cutoff, settings.lid_beta, LID_D_CUTOFF); 2],
            recent: [None; 2],
            shut_until: [f64::NEG_INFINITY; 2],
            was_shut: [false; 2],
            last_time: None,
            last_gaze: [None; 3],
            lid_vertical: None,
            down_hold_x: None,
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

    /// Move the clock to `time` and return the time since the previous sample; a gap starts the filters over.
    fn advance(&mut self, time: f64) -> f32 {
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
        dt
    }

    /// Median of this sample and the previous two: a one-sample dropout disappears, and everything
    /// else comes out one sample (~11 ms) later.
    fn despike(&mut self, values: [f32; 8]) -> [f32; 8] {
        let median = match self.recent {
            [Some(older), Some(old)] => std::array::from_fn(|i| median3(older[i], old[i], values[i])),
            _ => values,
        };
        self.recent = [self.recent[1], Some(values)];
        median
    }

    /// `hold` keeps the left, right and combined gaze as last sent while it is unreliable: when the eyes
    /// are mostly shut, where the Frame's gaze jumps around, or when its covariance says so.
    fn filter(&mut self, dt: f32, gaze: &mut [f32; 6], lids: &mut [f32; 2], hold: [bool; 3]) {
        for (pair, held) in hold.into_iter().enumerate() {
            let values = &mut gaze[2 * pair..2 * pair + 2];
            match self.last_gaze[pair] {
                Some(last) if held => values.copy_from_slice(&last),
                _ => {
                    for (index, value) in (2 * pair..).zip(values.iter_mut()) {
                        *value = self.deadzones[index].apply(self.gaze[index].filter(*value, dt));
                    }
                    self.last_gaze[pair] = Some([values[0], values[1]]);
                }
            }
        }
        for (value, filter) in lids.iter_mut().zip(&mut self.lids) {
            *value = filter.filter(*value, dt);
        }
    }

    /// Which eyelids go out fully closed: `closed` ones (at or past --lid-closed), and both when one is
    /// closed and the other nearly so. Once an eye goes out closed it stays so for at least
    /// --blink-hold-ms, so a 30 ms blink is sent as 80 ms and a 200 ms one as 200 ms. Their filters
    /// restart from closed, so the eye opens smoothly afterwards.
    fn hold_shut(&mut self, time: f64, closed: [bool; 2], lids: [f32; 2], settings: &Settings) -> [bool; 2] {
        let hold = f64::from(settings.blink_hold_ms) / 1000.0;
        let held = [0, 1].map(|eye| closed[eye] || time < self.shut_until[eye]);
        let shut = sync_blinks(held, lids, settings.blink_sync_below);
        for (eye, shut) in shut.into_iter().enumerate() {
            if shut && !self.was_shut[eye] {
                self.shut_until[eye] = time + hold;
            }
            if shut {
                self.lids[eye].value = Some(0.0);
            }
        }
        self.was_shut = shut;
        shut
    }

    fn reset(&mut self) {
        self.gaze.iter_mut().chain(&mut self.lids).for_each(OneEuro::reset);
        self.deadzones.iter_mut().for_each(Deadzone::reset);
        self.recent = [None; 2];
        self.shut_until = [f64::NEG_INFINITY; 2];
        self.was_shut = [false; 2];
        self.last_time = None;
        self.last_gaze = [None; 3];
        self.lid_vertical = None;
        self.down_hold_x = None;
    }

    /// Below --gaze-down-hold-x-deg the Frame's sideways gaze jumps (by ~19° to the right when looking
    /// ~40° down, 2026-09-28), so the left, right and combined x fade into their values from just before
    /// the gaze went that low: not at all at the threshold, fully 10° further down. Judged on the raw
    /// combined vertical gaze, before the zero point and gains, so the threshold is the tracker's own
    /// degrees whatever the fit. Starting out that low, the held x is straight ahead as fitted.
    fn hold_down_x(&mut self, raw_gaze: [f32; 6], settings: &Settings) -> [f32; 6] {
        let threshold = settings.gaze_down_hold_x_deg;
        let x = [raw_gaze[0], raw_gaze[2], raw_gaze[4]];
        let down_deg = -raw_gaze[5] * 45.0;
        if threshold <= 0.0 || down_deg <= threshold {
            self.down_hold_x = Some(x);
            return raw_gaze;
        }
        let held = self.down_hold_x.unwrap_or([settings.gaze_offset_x; 3]);
        let live = (1.0 - (down_deg - threshold) / DOWN_HOLD_FADE_DEG).clamp(0.0, 1.0);
        let mut out = raw_gaze;
        for (pair, (held, x)) in held.into_iter().zip(x).enumerate() {
            out[pair * 2] = live * x + (1.0 - live) * held;
        }
        out
    }
}

fn median3(a: f32, b: f32, c: f32) -> f32 {
    a.max(b).min(a.min(b).max(c))
}

/// Learns each eye's relaxed openness while in use, so a face that opens one eye less than the
/// other still maps both eyes' normal state onto --lid-open.
#[derive(Clone)]
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

/// A fitted eye's expected open reading for an up/down gaze (-1..1): a line through its down, straight
/// ahead and up readings, carried on past them, and never below half the straight-ahead one.
fn expected_open(fit: &LidFit, vertical: f32) -> f32 {
    let slope = if vertical >= 0.0 { fit.up - fit.open } else { fit.open - fit.down };
    (fit.open + slope * vertical / LID_FIT_PITCH).max(0.5 * fit.open)
}

/// A fitted eye's openness on the --lid-closed / --lid-open scale: its closed reading (plus a margin)
/// maps to --lid-closed and its expected open reading for where the eyes look maps to --lid-open.
/// Higher readings go on past --lid-open toward widening as before.
fn fitted_openness(openness: f32, vertical: f32, fit: &LidFit, settings: &Settings) -> f32 {
    let range = (expected_open(fit, vertical) - fit.closed).max(0.05);
    let fraction = (openness - fit.closed) / range;
    let fraction = (fraction - LID_FIT_CLOSED_MARGIN) / (1.0 - LID_FIT_CLOSED_MARGIN);
    settings.lid_closed + fraction * (settings.lid_open - settings.lid_closed)
}

/// Each eye's openness on the --lid-* scale: the eye fit when there is one, else the per-eye scale.
fn lid_inputs(openness: [f32; 2], vertical: f32, scales: [f32; 2], settings: &Settings) -> [f32; 2] {
    let fits = settings.lid_fit();
    [0, 1].map(|eye| match &fits[eye] {
        Some(fit) => fitted_openness(openness[eye], vertical, fit, settings),
        None => openness[eye] * scales[eye],
    })
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

/// Close both eyes when one is `shut` and the other's eyelid is below `below`: a blink the Frame caught
/// fully in one eye only. A wink, with the other eye open, passes through. 0 disables.
fn sync_blinks(shut: [bool; 2], lids: [f32; 2], below: f32) -> [bool; 2] {
    let [left, right] = shut;
    if below > 0.0 && ((left && lids[1] < below) || (right && lids[0] < below)) {
        [true; 2]
    } else {
        shut
    }
}

/// Whether each eye's gaze is reliable enough to use: both variances of its own (pre-fusion) estimate
/// must be at most `limit`. A missing or non-finite covariance counts as unreliable. 0 disables.
fn gaze_quality(data: &EyeData, limit: f32) -> [bool; 2] {
    if limit <= 0.0 {
        return [true; 2];
    }
    data.pre_fusion_covariance.map(|[x, y, _]| x <= limit && y <= limit)
}

/// The six gaze values to send (left x/y, right x/y, combined x/y) from the same layout of readings.
/// Each eye wobbles on its own (L/R changes correlate only ~0.35), so both share the combined gaze
/// unless --independent-eyes is given. An eye with unreliable gaze is left out of the combined gaze and the other eye stands in for it;
/// without --independent-eyes that also moves both eyes.
fn choose_gaze(angles: [f32; 6], reliable: [bool; 2], independent: bool) -> [f32; 6] {
    let [left_x, left_y, right_x, right_y, x, y] = angles;
    let [x, y] = match reliable {
        [true, false] => [left_x, left_y],
        [false, true] => [right_x, right_y],
        _ => [x, y],
    };
    if independent {
        [left_x, left_y, right_x, right_y, x, y]
    } else {
        [x, y, x, y, x, y]
    }
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

#[derive(Clone, Copy, Debug, Default, PartialEq)]
struct EyeData {
    sample_time: f64,
    gaze: [[f32; 3]; 2],
    // Diagonal of each eye's gaze covariance, after and before stereo fusion.
    gaze_covariance: [[f32; 3]; 2],
    fixation_point: [f32; 3],
    pre_fusion_gaze: [[f32; 3]; 2],
    pre_fusion_covariance: [[f32; 3]; 2],
    openness: [f32; 2],
    // Not yet understood; only recorded.
    extra: [f32; 8],
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
                    gaze_covariance: record.gaze_covariance_diag,
                    fixation_point: record.fixation_point,
                    pre_fusion_gaze: record.pre_fusion_gaze,
                    pre_fusion_covariance: record.pre_fusion_cov_diag,
                    openness: record.openness,
                    extra: record.estimate_extra,
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
    if !hex.len().is_multiple_of(8) {
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

/// Move each gaze pair's zero point to --gaze-offset-x/y and scale how far it goes from there;
/// up and down have their own gains, and each eye's x may have its own zero point and gain (the
/// combined x always uses the shared ones). The defaults leave the gaze exactly as it is.
fn correct_gaze(angles: [f32; 6], settings: &Settings) -> [f32; 6] {
    let eye_offsets = [settings.gaze_offset_x_left, settings.gaze_offset_x_right];
    let eye_gains = [settings.gaze_gain_x_left, settings.gaze_gain_x_right];
    std::array::from_fn(|i| {
        let value = angles[i];
        let corrected = if i % 2 == 0 {
            let eye = i / 2;
            let offset = eye_offsets.get(eye).copied().flatten().unwrap_or(settings.gaze_offset_x);
            let gain = eye_gains.get(eye).copied().flatten().unwrap_or(settings.gaze_gain_x);
            (value - offset) * gain
        } else {
            let from_center = value - settings.gaze_offset_y;
            let gain = if from_center >= 0.0 {
                settings.gaze_gain_up
            } else {
                settings.gaze_gain_down
            };
            from_center * gain
        };
        corrected.clamp(-1.0, 1.0)
    })
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
    // Whether each eye's gaze passed the quality check.
    reliable: [bool; 2],
    // Whether the combined gaze was held (eyes mostly shut, or both eyes unreliable).
    gaze_held: bool,
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

/// Let the eyelid calibration learn from a sample once `settled`, then work the sample through.
fn step(
    settings: &Settings,
    smoother: &mut Smoother,
    calibration: &mut LidCalibration,
    data: &EyeData,
    settled: bool,
) -> Sample {
    // Openness read while the gaze is unreliable is suspect too, so it does not teach the calibration.
    // Fitted eyes do not use it.
    if settings.lid_calibration
        && settled
        && settings.lid_fit().iter().any(Option::is_none)
        && gaze_quality(data, settings.gaze_quality_limit) == [true; 2]
    {
        calibration.observe(data.openness);
    }
    process(settings, smoother, lid_scales(settings, calibration), data)
}

fn process(settings: &Settings, smoother: &mut Smoother, scales: [f32; 2], data: &EyeData) -> Sample {
    let [x, y] = gaze_angles(data.fixation_point);
    let [left, right] = data.gaze.map(gaze_angles);
    let raw_gaze = [left[0], left[1], right[0], right[1], x, y];
    let reliable = gaze_quality(data, settings.gaze_quality_limit);
    let blink_stages = settings.blink_hold_ms > 0.0 || settings.blink_sync_below > 0.0;
    let (gaze, lids, gaze_held, openness_scaled) = if settings.raw {
        let corrected = correct_gaze(raw_gaze, settings);
        let gaze = choose_gaze(corrected, [true; 2], settings.independent_eyes);
        let openness_scaled = lid_inputs(data.openness, corrected[5], scales, settings);
        let mapped = openness_scaled.map(|openness| lid_to_vrcft(openness, settings));
        let shut = sync_blinks(mapped.map(|lid| lid <= 0.0), mapped, settings.blink_sync_below);
        let mut lids = sync_lids(mapped, settings.lid_sync);
        if blink_stages {
            shut_lids(&mut lids, shut);
        }
        let shut_eyes = data.openness.iter().any(|openness| *openness < settings.gaze_hold_below);
        (gaze, lids, shut_eyes, openness_scaled)
    } else {
        let dt = smoother.advance(data.sample_time);
        // Before anything else, so the filters see the gaze the way it will be sent.
        let corrected = correct_gaze(smoother.hold_down_x(raw_gaze, settings), settings);
        let readings: [f32; 8] =
            std::array::from_fn(|i| if i < 6 { corrected[i] } else { data.openness[i - 6] });
        // Fed even while off, so turning it on does not start from an empty history.
        let despiked = smoother.despike(readings);
        let readings = if settings.despike { despiked } else { readings };
        let angles: [f32; 6] = std::array::from_fn(|i| readings[i]);
        let openness = [readings[6], readings[7]];
        let mut gaze = choose_gaze(angles, reliable, settings.independent_eyes);
        let hold = if openness.iter().any(|openness| *openness < settings.gaze_hold_below)
            || reliable == [false; 2]
        {
            [true; 3]
        } else if settings.independent_eyes {
            [!reliable[0], !reliable[1], false]
        } else {
            [false; 3]
        };
        // Where the eyes look up or down, for the eyelid fit: kept while blinking, like the gaze.
        let vertical = match smoother.lid_vertical {
            Some(held) if hold[2] => held,
            _ => gaze[5],
        };
        smoother.lid_vertical = Some(vertical);
        let mapped = lid_inputs(openness, vertical, scales, settings).map(|openness| lid_to_vrcft(openness, settings));
        let mut lids = mapped;
        smoother.filter(dt, &mut gaze, &mut lids, hold);
        let mut lids = sync_lids(lids, settings.lid_sync);
        if blink_stages {
            let shut = smoother.hold_shut(data.sample_time, mapped.map(|lid| lid <= 0.0), mapped, settings);
            shut_lids(&mut lids, shut);
        }
        (gaze, lids, hold[2], lid_inputs(data.openness, vertical, scales, settings))
    };
    Sample {
        openness: data.openness,
        openness_scaled,
        raw_gaze,
        gaze,
        lids,
        reliable,
        gaze_held,
    }
}

fn shut_lids(lids: &mut [f32; 2], shut: [bool; 2]) {
    for (lid, shut) in lids.iter_mut().zip(shut) {
        if shut {
            *lid = 0.0;
        }
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

/// Why the samples stopped, for the log line when tracking is lost.
fn lost_reason(next: &Next) -> String {
    match next {
        Next::Stopped => "eye server not producing".into(),
        Next::Sample(_) => "unreadable sample".into(),
        Next::Waiting => format!("no new samples for {} s", TIMEOUT.as_secs()),
    }
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
    // The gaze capture the panel asked for, while it runs, and the latest one's result.
    capture: Option<Capture>,
    capture_result: Option<CaptureResult>,
}

impl Bridge {
    /// Take changed settings without restarting.
    fn apply(&mut self, reload: Reload) -> Result<(), Box<dyn Error>> {
        let Reload {
            settings,
            reset_calibration,
            gaze_capture,
        } = reload;
        if let Some(request) = gaze_capture {
            eprintln!("Gaze capture {} ({}) asked for", request.id, request.target);
            let capture = Capture::new(request);
            self.capture_result = Some(capture.result(CaptureState::Running));
            self.capture = Some(capture);
        }
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
        let previous = self.last_data.replace(now);
        if self.active_since.is_none() {
            match previous {
                Some(last) => eprintln!(
                    "Eye tracking resumed after {:.1} s",
                    now.duration_since(last).as_secs_f32()
                ),
                None => eprintln!("Eye tracking started"),
            }
        }
        let since = *self.active_since.get_or_insert(now);
        let settled = since.elapsed() >= CAL_SETTLE;
        let sample = step(&self.settings, &mut self.smoother, &mut self.calibration, &data, settled);
        if self.settings.lid_calibration && settled {
            self.calibration.save_if_due();
        }
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
        if let Some(capture) = &mut self.capture {
            let gaze = [sample.raw_gaze[4], sample.raw_gaze[5]];
            let eye_x = [sample.raw_gaze[0], sample.raw_gaze[2]];
            if capture.add(data.sample_time, gaze, eye_x, data.openness, !sample.gaze_held) {
                self.finish_capture();
            }
        }
        self.latest = Some(sample);
        Ok(())
    }

    /// End the running gaze capture, if it has waited too long for samples.
    fn check_capture(&mut self) {
        if self.capture.as_ref().is_some_and(Capture::timed_out) {
            self.finish_capture();
        }
    }

    fn finish_capture(&mut self) {
        if let Some(capture) = self.capture.take() {
            let result = capture.result(CaptureState::Done);
            eprintln!("{}", result.log_line());
            self.capture_result = Some(result);
        }
    }

    fn on_lost(&mut self, reason: &str) -> Result<(), Box<dyn Error>> {
        eprintln!("Eye tracking stopped ({reason})");
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
                fitted: settings.lid_fit().map(|fit| fit.is_some()),
                learning: settings.lid_calibration
                    && settings.lid_fit().iter().any(Option::is_none)
                    && self
                        .active_since
                        .is_some_and(|since| since.elapsed() >= CAL_SETTLE),
            },
            config_path: self.config.path.as_deref(),
            calibration_path: self.calibration.path.as_deref(),
            config_error: self.config.error.as_deref(),
            locked: &self.config.locked,
            effective: settings,
            gaze_capture: self.capture_result.as_ref(),
        }
    }
}

/// Write every sample the eye server produces to `path` until stopped. Nothing is sent, and the status
/// and calibration files are left alone, so this can run next to the installed service.
fn record(path: &Path) -> Result<(), Box<dyn Error>> {
    let mut recorder = replay::Recorder::create(path)?;
    let mut source = EyeSource::open()?;
    eprintln!("Recording {SOURCE} to {}; stop with Ctrl+C", path.display());
    let mut last_flush = Instant::now();
    let mut reported = 0;
    loop {
        match source.next(POLL)? {
            Next::Sample(data) => recorder.write(&data)?,
            Next::Waiting | Next::Stopped if source.is_stale() => {
                eprintln!("{SOURCE} was replaced; reopening");
                source = EyeSource::open()?;
            }
            _ => {}
        }
        // Flushed every second, so stopping with Ctrl+C loses at most that much.
        if last_flush.elapsed() >= Duration::from_secs(1) {
            recorder.flush()?;
            last_flush = Instant::now();
            if recorder.count / 900 != reported {
                reported = recorder.count / 900;
                eprintln!("{} samples", recorder.count);
            }
        }
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let matches = Args::command().get_matches();
    let args = Args::from_arg_matches(&matches)?;
    if let Some(path) = &args.record {
        return record(path);
    }
    if args.target != "auto" && config::split_target(&args.target).is_none() {
        return Err("--target must be HOST:PORT or auto".into());
    }
    let in_config_dir = |name: &str| Some(config::config_dir()?.join(name));
    let calibration_path = args.calibration_file.clone().or_else(|| in_config_dir("calibration"));
    let config_path = args.config.clone().or_else(|| in_config_dir("config.json"));
    let replay = args.replay.clone().map(|input| (input, args.replay_out.clone()));
    let mut config = Config::new(config_path, args, config::given_options(&matches));
    let settings = config.load()?;
    if let Some((input, output)) = replay {
        // Starts from the saved calibration like a real run, but never writes it back.
        let mut calibration = LidCalibration::load(calibration_path, settings.lid_open);
        calibration.path = None;
        return replay::run(&input, output.as_deref(), &settings, &calibration);
    }
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
        capture: None,
        capture_result: None,
    };
    let mut status_file = StatusFile::new(status::status_path());
    let mut source = EyeSource::open()?;
    eprintln!("Reading {SOURCE}");
    // Whether the last attempt to reattach to a replaced shared memory failed, so it is logged once.
    let mut reopen_failed = false;
    loop {
        if let Some(reload) = bridge.config.poll() {
            bridge.apply(reload)?;
        }
        bridge.output.refresh()?;
        match source.next(POLL)? {
            Next::Sample(data) if data.is_finite() => bridge.on_sample(data)?,
            // Short waits are normal; only a whole second without data means tracking stopped.
            Next::Waiting if bridge.last_data.is_some_and(|last| last.elapsed() < TIMEOUT) => {}
            next if bridge.active_since.is_some() => bridge.on_lost(&lost_reason(&next))?,
            // Idle: if the eye server recreated its shared memory, our mapping would go silent forever.
            // While the new one is missing or not set up yet, keep trying instead of exiting.
            _ if source.is_stale() => match EyeSource::open() {
                Ok(reopened) => {
                    eprintln!("{SOURCE} was replaced; reattached");
                    source = reopened;
                    reopen_failed = false;
                }
                Err(error) if !reopen_failed => {
                    eprintln!("{SOURCE} was replaced and can't be opened yet ({error}); retrying");
                    reopen_failed = true;
                }
                Err(_) => {}
            },
            _ => {}
        }
        bridge.check_capture();
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

    fn apply(smoother: &mut Smoother, time: f64, gaze: &mut [f32; 6], lids: &mut [f32; 2], hold: bool) {
        let dt = smoother.advance(time);
        smoother.filter(dt, gaze, lids, [hold; 3]);
    }

    /// Both eyes looking along (x, y) at sample `index`, with the given Frame openness.
    fn reading(index: usize, [x, y]: [f32; 2], openness: [f32; 2]) -> EyeData {
        let direction = [x, y, -1.0];
        EyeData {
            sample_time: index as f64 * f64::from(NOMINAL_DT),
            gaze: [direction; 2],
            fixation_point: direction,
            pre_fusion_gaze: [direction; 2],
            openness,
            ..EyeData::default()
        }
    }

    fn run(settings: &Settings, readings: &[EyeData]) -> Vec<Sample> {
        let mut smoother = Smoother::new(settings);
        readings
            .iter()
            .map(|data| process(settings, &mut smoother, [1.0; 2], data))
            .collect()
    }

    /// Open eyes, except for the samples listed with their openness.
    fn openness_track(length: usize, changes: &[(usize, [f32; 2])]) -> Vec<EyeData> {
        (0..length)
            .map(|i| {
                let openness = changes.iter().find(|(at, _)| *at == i).map_or([0.8; 2], |(_, open)| *open);
                reading(i, [0.0, 0.0], openness)
            })
            .collect()
    }

    fn without_new_stages() -> Settings {
        Settings {
            gaze_quality_limit: 0.0,
            blink_hold_ms: 0.0,
            despike: false,
            blink_sync_below: 0.0,
            ..settings()
        }
    }

    #[test]
    fn gaze_quality_reads_each_eyes_own_covariance() {
        let mut data = reading(0, [0.0, 0.0], [0.8; 2]);
        data.pre_fusion_covariance = [[0.01, 0.02, 0.5], [0.01, 0.05, 0.0]];
        // The z variance does not count.
        assert_eq!(gaze_quality(&data, 0.03), [true, false]);
        assert_eq!(gaze_quality(&data, 0.0), [true, true]);
        data.pre_fusion_covariance[0][0] = f32::NAN;
        assert_eq!(gaze_quality(&data, 0.03), [false, false]);
    }

    #[test]
    fn unreliable_eye_gives_way_to_the_other() {
        let angles = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0];
        assert_eq!(choose_gaze(angles, [true; 2], false), [5.0, 6.0, 5.0, 6.0, 5.0, 6.0]);
        assert_eq!(choose_gaze(angles, [true, false], false), [1.0, 2.0, 1.0, 2.0, 1.0, 2.0]);
        assert_eq!(choose_gaze(angles, [false, true], true), [1.0, 2.0, 3.0, 4.0, 3.0, 4.0]);
        assert_eq!(choose_gaze(angles, [false; 2], false), [5.0, 6.0, 5.0, 6.0, 5.0, 6.0]);
    }

    #[test]
    fn gaze_is_held_while_both_eyes_are_unreliable() {
        let settings = Settings {
            despike: false,
            gaze_quality_limit: 0.03,
            ..settings()
        };
        let mut readings: Vec<EyeData> = (0..10).map(|i| reading(i, [0.2, 0.0], [0.8; 2])).collect();
        let mut wild = reading(10, [-0.5, 0.3], [0.8; 2]);
        wild.pre_fusion_covariance = [[1.0; 3]; 2];
        readings.push(wild);
        let sent = run(&settings, &readings);
        assert_eq!(sent[10].gaze, sent[9].gaze);
        assert_eq!(sent[10].reliable, [false; 2]);

        // With per-eye gaze only the unreliable eye is held; the other one moves both combined values.
        let independent = Settings {
            independent_eyes: true,
            gaze_deadzone: 0.0,
            ..settings
        };
        readings[10].pre_fusion_covariance = [[1.0; 3], [0.0; 3]];
        let sent = run(&independent, &readings);
        assert_eq!(sent[10].gaze[..2], sent[9].gaze[..2]);
        assert!(sent[10].gaze[2] < sent[9].gaze[2] && sent[10].gaze[4] < sent[9].gaze[4]);
    }

    #[test]
    fn blink_hold_keeps_a_short_blink_closed() {
        let settings = Settings {
            despike: false,
            ..settings()
        };
        let readings = openness_track(30, &[(10, [0.2; 2])]);
        let sent = run(&settings, &readings);
        // 80 ms is 7.2 samples: closed at sample 10 and the seven after it.
        assert!(sent[10..18].iter().all(|sample| sample.lids == [0.0; 2]));
        assert!(sent[18].lids[0] > 0.0 && sent[18].lids[0] < 0.75);
        // It is a minimum, not an extension: a blink longer than the hold opens as soon as it ends.
        let long: Vec<(usize, [f32; 2])> = (10..30).map(|i| (i, [0.2; 2])).collect();
        let sent = run(&settings, &openness_track(40, &long));
        assert!(sent[10..30].iter().all(|sample| sample.lids == [0.0; 2]));
        assert!(sent[30].lids[0] > 0.0);
        // Without the new stages the filters never quite get there.
        let sent = run(&without_new_stages(), &readings);
        assert!(sent.iter().all(|sample| sample.lids[0] > 0.2), "{:?}", sent[10].lids);
    }

    #[test]
    fn despike_drops_a_one_sample_dropout() {
        let dropout = openness_track(30, &[(10, [0.2, 0.8])]);
        let sent = run(&settings(), &dropout);
        assert!(sent.iter().all(|sample| sample.lids[0] > 0.7));
        let sent = run(&without_new_stages(), &dropout);
        assert!(sent.iter().any(|sample| sample.lids[0] < 0.5));
        // Two samples are a real (short) blink, and come through one sample late.
        let blink = openness_track(30, &[(10, [0.2; 2]), (11, [0.2; 2])]);
        let sent = run(&settings(), &blink);
        assert!(sent[10].lids[0] > 0.7 && sent[11].lids == [0.0; 2]);
    }

    #[test]
    fn blink_sync_closes_both_but_keeps_winks() {
        assert_eq!(sync_blinks([true, false], [0.0, 0.2], 0.35), [true; 2]);
        assert_eq!(sync_blinks([false, true], [0.3, 0.0], 0.35), [true; 2]);
        assert_eq!(sync_blinks([true, false], [0.0, 0.75], 0.35), [true, false]);
        assert_eq!(sync_blinks([true, false], [0.0, 0.2], 0.0), [true, false]);
        // A wink stays a wink through the whole pipeline.
        let wink: Vec<(usize, [f32; 2])> = (10..30).map(|i| (i, [0.2, 0.8])).collect();
        let sent = run(&settings(), &openness_track(40, &wink));
        assert!(sent[20].lids[0] == 0.0 && sent[20].lids[1] > 0.7);
    }

    #[test]
    fn raw_mode_skips_the_timed_stages() {
        let raw = Settings {
            raw: true,
            ..settings()
        };
        let sent = run(&raw, &openness_track(20, &[(10, [0.2; 2])]));
        assert_eq!(sent[10].lids, [0.0; 2]);
        assert_eq!(sent[11].lids, [0.75; 2]);
    }

    #[test]
    fn gaze_correction_moves_the_zero_point_and_scales_each_direction() {
        let angles = [0.2, 0.3, -0.2, -0.3, 0.1, -0.1];
        assert_eq!(correct_gaze(angles, &settings()), angles);
        let fitted = Settings {
            gaze_offset_x: 0.1,
            gaze_offset_y: -0.1,
            gaze_gain_x: 2.0,
            gaze_gain_up: 1.5,
            gaze_gain_down: 0.5,
            ..settings()
        };
        let corrected = correct_gaze(angles, &fitted);
        let expected = [0.2, 0.6, -0.6, -0.1, 0.0, 0.0];
        for (value, expected) in corrected.iter().zip(expected) {
            assert!((value - expected).abs() < 1e-6, "{corrected:?}");
        }
        // Still within -1..1.
        assert_eq!(correct_gaze([1.0, 1.0, -1.0, -1.0, 0.0, 0.0], &fitted)[..4], [1.0, 1.0, -1.0, -0.45]);
    }

    #[test]
    fn corrected_gaze_is_sent_and_raw_gaze_reported() {
        let fitted = Settings {
            gaze_offset_y: -0.1,
            gaze_deadzone: 0.0,
            despike: false,
            ..settings()
        };
        let readings: Vec<EyeData> = (0..200).map(|i| reading(i, [0.0, 0.0], [0.8; 2])).collect();
        let sent = run(&fitted, &readings);
        let last = &sent[199];
        assert_eq!(last.raw_gaze[5], 0.0);
        assert!((last.gaze[5] - 0.1).abs() < 1e-3, "{:?}", last.gaze);
        let raw = run(&Settings { raw: true, ..fitted }, &readings);
        assert!((raw[0].gaze[5] - 0.1).abs() < 1e-6);
    }

    fn fitted() -> Settings {
        Settings {
            lid_fit_closed_left: Some(0.15),
            lid_fit_up_left: Some(0.95),
            lid_fit_open_left: Some(0.9),
            lid_fit_down_left: Some(0.7),
            lid_fit_closed_right: Some(0.25),
            lid_fit_up_right: Some(0.85),
            lid_fit_open_right: Some(0.8),
            lid_fit_down_right: Some(0.6),
            ..settings()
        }
    }

    #[test]
    fn expected_openness_follows_the_gaze_up_and_down() {
        let fit = fitted().lid_fit()[0].unwrap();
        assert!((expected_open(&fit, 0.0) - 0.9).abs() < 1e-6);
        assert!((expected_open(&fit, LID_FIT_PITCH) - 0.95).abs() < 1e-6);
        assert!((expected_open(&fit, -LID_FIT_PITCH) - 0.7).abs() < 1e-6);
        // Carried on past the down reading, but never below half the straight-ahead one.
        assert!((expected_open(&fit, -1.5 * LID_FIT_PITCH) - 0.6).abs() < 1e-6);
        assert!((expected_open(&fit, -1.0) - 0.45).abs() < 1e-6);
    }

    #[test]
    fn fitted_eyelids_stay_open_when_looking_down() {
        let settings = fitted();
        let fit = settings.lid_fit()[0].unwrap();
        // Looking 15° down, the reading drops to the fitted down value: still relaxed open.
        let down = fitted_openness(0.7, -LID_FIT_PITCH, &fit, &settings);
        assert!((lid_to_vrcft(down, &settings) - 0.75).abs() < 1e-5, "{down}");
        // The same reading straight ahead is a squint.
        let ahead = lid_to_vrcft(fitted_openness(0.7, 0.0, &fit, &settings), &settings);
        assert!(ahead > 0.4 && ahead < 0.6, "{ahead}");
        // Near the closed reading the eye is shut, straight ahead or looking down.
        assert_eq!(lid_to_vrcft(fitted_openness(0.2, 0.0, &fit, &settings), &settings), 0.0);
        assert_eq!(lid_to_vrcft(fitted_openness(0.2, -LID_FIT_PITCH, &fit, &settings), &settings), 0.0);
        // Wide open still widens.
        assert!(lid_to_vrcft(fitted_openness(1.2, 0.0, &fit, &settings), &settings) > 0.9);
        // Without a fit, the scale applies as before.
        assert_eq!(lid_inputs([0.7, 0.7], -0.3, [1.0, 1.1], &Settings::default()), [0.7, 0.7 * 1.1]);
    }

    #[test]
    fn fitted_eyelids_use_the_held_gaze_while_blinking() {
        let settings = Settings {
            despike: false,
            gaze_deadzone: 0.0,
            ..fitted()
        };
        // Looking 15° down (the fixation point at -tan 15°), then a blink where the gaze jumps up.
        let down = (15.0_f32).to_radians().tan();
        let mut readings: Vec<EyeData> = (0..20).map(|i| reading(i, [0.0, -down], [0.7, 0.6])).collect();
        readings.push(reading(20, [0.0, 0.5], [0.3, 0.3]));
        readings.push(reading(21, [0.0, 0.5], [0.3, 0.3]));
        let sent = run(&settings, &readings);
        assert!((sent[19].openness_scaled[0] - settings.lid_open).abs() < 0.01, "{:?}", sent[19].openness_scaled);
        // During the blink the fit keeps using "down": 0.3 is well below the down reading either way.
        assert!(sent[21].gaze_held && sent[21].openness_scaled[0] < settings.lid_closed + 0.1);
        let unfitted = run(&Settings { despike: false, ..Settings::default() }, &readings);
        assert!(unfitted[19].openness_scaled[0] < settings.lid_open);
    }

    #[test]
    fn fitted_eyes_are_not_learned() {
        let mut calibration = LidCalibration::load(None, 0.80);
        let mut smoother = Smoother::new(&fitted());
        for i in 0..900 {
            step(&fitted(), &mut smoother, &mut calibration, &reading(i, [0.0, 0.0], [0.6, 0.6]), true);
        }
        assert!(calibration.histograms.iter().flatten().all(|weight| *weight == 0.0));
    }

    #[test]
    fn samples_say_when_the_gaze_is_held() {
        let readings = openness_track(20, &[(10, [0.2; 2]), (11, [0.2; 2])]);
        let sent = run(&without_new_stages(), &readings);
        assert!(!sent[9].gaze_held && sent[10].gaze_held && !sent[12].gaze_held);
        let raw = run(&Settings { raw: true, ..settings() }, &readings);
        assert!(raw[10].gaze_held && !raw[12].gaze_held);
    }

    #[test]
    fn each_eye_may_have_its_own_sideways_fit() {
        let angles = [0.2, 0.1, -0.2, 0.1, 0.0, 0.1];
        let per_eye = Settings {
            gaze_offset_x: 0.01,
            gaze_gain_x: 2.0,
            gaze_offset_x_left: Some(0.1),
            gaze_gain_x_right: Some(0.5),
            ..settings()
        };
        let corrected = correct_gaze(angles, &per_eye);
        // Left: its own offset, the shared gain; right: the shared offset, its own gain; combined: shared.
        let expected = [0.2, 0.1, -0.105, 0.1, -0.02, 0.1];
        for (value, expected) in corrected.iter().zip(expected) {
            assert!((value - expected).abs() < 1e-6, "{corrected:?}");
        }
        // Unset per-eye values change nothing.
        let shared = Settings {
            gaze_offset_x: 0.01,
            gaze_gain_x: 2.0,
            ..settings()
        };
        assert_eq!(correct_gaze(angles, &shared)[0], (0.2 - 0.01) * 2.0);
    }

    #[test]
    fn looking_far_down_holds_the_sideways_gaze() {
        let settings = settings();
        let mut smoother = Smoother::new(&settings);
        let at = |down_deg: f32, x: f32| {
            let y = -down_deg / 45.0;
            [x, y, x + 0.1, y, x + 0.05, y]
        };
        // Above the threshold nothing changes, and the x values are remembered.
        assert_eq!(smoother.hold_down_x(at(20.0, 0.1), &settings), at(20.0, 0.1));
        assert_eq!(smoother.hold_down_x(at(27.5, 0.1), &settings), at(27.5, 0.1));
        // Halfway into the fade, halfway to the held values; fully held 10° below the threshold.
        let half = smoother.hold_down_x(at(33.0, 0.5), &settings);
        assert!((half[0] - 0.3).abs() < 1e-5 && (half[2] - 0.4).abs() < 1e-5 && (half[4] - 0.35).abs() < 1e-5);
        let held = smoother.hold_down_x(at(42.0, 0.5), &settings);
        assert!((held[0] - 0.1).abs() < 1e-6 && (held[2] - 0.2).abs() < 1e-6 && (held[4] - 0.15).abs() < 1e-6);
        // The vertical gaze is left alone.
        assert_eq!([held[1], held[3], held[5]], [at(42.0, 0.5)[1]; 3]);
        // Starting out that low, straight ahead (as fitted) is held; after a reset too.
        smoother.reset();
        let fitted = Settings {
            gaze_offset_x: 0.02,
            ..settings.clone()
        };
        assert!((smoother.hold_down_x(at(42.0, 0.5), &fitted)[4] - 0.02).abs() < 1e-6);
        // 0 turns it off.
        let off = Settings {
            gaze_down_hold_x_deg: 0.0,
            ..settings
        };
        assert_eq!(smoother.hold_down_x(at(42.0, 0.5), &off), at(42.0, 0.5));
    }

    #[test]
    fn lost_tracking_says_why() {
        assert_eq!(lost_reason(&Next::Stopped), "eye server not producing");
        assert_eq!(lost_reason(&Next::Waiting), "no new samples for 1 s");
        let unreadable = EyeData {
            sample_time: f64::NAN,
            ..EyeData::default()
        };
        assert_eq!(lost_reason(&Next::Sample(unreadable)), "unreadable sample");
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
        apply(&mut smoother, 0.0, &mut gaze, &mut lids, false);
        let mut jumped = [0.5; 6];
        apply(&mut smoother, NOMINAL_DT as f64, &mut jumped, &mut lids, false);
        assert!(jumped[0] < 0.5, "a filtered step must not pass through untouched: {}", jumped[0]);
    }

    #[test]
    fn gaze_is_held_while_eyes_are_shut() {
        let mut smoother = Smoother::new(&settings());
        let mut lids = [0.75; 2];
        let mut gaze = [0.2; 6];
        apply(&mut smoother, 0.0, &mut gaze, &mut lids, false);
        let before = gaze;
        let mut jumped = [-0.4; 6];
        apply(&mut smoother, NOMINAL_DT as f64, &mut jumped, &mut lids, true);
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
            reliable: [true; 2],
            gaze_held: false,
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
        apply(&mut smoother, 0.0, &mut gaze, &mut lids, false);
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
