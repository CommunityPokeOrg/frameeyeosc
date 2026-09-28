//! Averaging the raw gaze and openness for a moment while the user looks at a target (or keeps the
//! eyes shut), on the panel's request. The panel turns the averages into the gaze zero point and
//! gains and each eye's lid fit.

use crate::config::GazeCapture;
#[cfg(test)]
use crate::config::{CAPTURE_SECONDS, CAPTURE_SKIP};
use serde::Serialize;
use std::time::{Duration, Instant};

// Samples at the start are skipped while the eyes settle on the target (the request's `skip`), and the capture
// ends with the first sample its `seconds` after the first one (sample time). Without samples (tracking lost), it
// gives up this long on the clock after it should have ended.
const TIMEOUT_EXTRA: Duration = Duration::from_secs(3);
// The target that asks for the eyes-shut capture: every sample counts and the gaze is not used.
pub const CLOSED_TARGET: &str = "closed";
// A gaze sample counts when both eyes read at least this open (clearly open; a shut eye reads about
// 0.15-0.26), whatever gaze_hold_below is: an eye that reads less open looking down (0.45 for a face
// that reads 0.65 straight ahead) must still be measured there.
const OPEN_FLOOR: f32 = 0.3;

/// Whether a sample's gaze counts in a capture: both eyes open enough and at least one eye's gaze reliable.
pub fn usable(openness: [f32; 2], reliable: [bool; 2]) -> bool {
    openness.iter().all(|openness| *openness >= OPEN_FLOOR) && reliable.contains(&true)
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum CaptureState {
    Running,
    Done,
}

/// What the status file reports about the latest capture. The gaze averages are on the -1..1 scale,
/// before the gaze zero point and gains are applied; openness is the Frame's, before any scale.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct CaptureResult {
    pub id: i64,
    pub target: String,
    pub state: CaptureState,
    /// Averages and spread (standard deviation of both axes together); None without samples, and
    /// for the eyes-shut capture.
    pub x: Option<f32>,
    pub y: Option<f32>,
    pub spread: Option<f32>,
    /// Each eye's own sideways gaze averages (left, right); None like x.
    pub x_left: Option<f32>,
    pub x_right: Option<f32>,
    /// Each eye's average openness; None without samples.
    pub openness: Option<[f32; 2]>,
    /// Samples that went into the average.
    pub samples: u32,
}

/// One capture in progress.
pub struct Capture {
    request: GazeCapture,
    closed: bool,
    started: Instant,
    first_time: Option<f64>,
    sum: [f64; 2],
    sum_squares: [f64; 2],
    eye_x: [f64; 2],
    openness: [f64; 2],
    samples: u32,
}

impl Capture {
    pub fn new(request: GazeCapture) -> Self {
        Self {
            closed: request.target == CLOSED_TARGET,
            request,
            started: Instant::now(),
            first_time: None,
            sum: [0.0; 2],
            sum_squares: [0.0; 2],
            eye_x: [0.0; 2],
            openness: [0.0; 2],
            samples: 0,
        }
    }

    /// Take one sample's combined raw gaze, each eye's raw x and openness; `usable` is false while the
    /// eyes are shut or the gaze is held (ignored by the eyes-shut capture). Returns true once the
    /// capture is over.
    pub fn add(&mut self, time: f64, gaze: [f32; 2], eye_x: [f32; 2], openness: [f32; 2], usable: bool) -> bool {
        let elapsed = time - *self.first_time.get_or_insert(time);
        if elapsed >= self.request.seconds {
            return true;
        }
        let finite = gaze.iter().chain(&eye_x).chain(&openness).all(|value| value.is_finite());
        if elapsed >= self.request.skip && (usable || self.closed) && finite {
            for ((sum, squares), value) in self.sum.iter_mut().zip(&mut self.sum_squares).zip(gaze) {
                let value = f64::from(value);
                *sum += value;
                *squares += value * value;
            }
            for (sum, value) in self.eye_x.iter_mut().zip(eye_x) {
                *sum += f64::from(value);
            }
            for (sum, value) in self.openness.iter_mut().zip(openness) {
                *sum += f64::from(value);
            }
            self.samples += 1;
        }
        false
    }

    /// Whether it has waited too long for samples.
    pub fn timed_out(&self) -> bool {
        self.started.elapsed() >= Duration::from_secs_f64(self.request.seconds) + TIMEOUT_EXTRA
    }

    pub fn result(&self, state: CaptureState) -> CaptureResult {
        let count = f64::from(self.samples);
        let mean = self.sum.map(|sum| sum / count);
        let variance: f64 = (0..2)
            .map(|axis| (self.sum_squares[axis] / count - mean[axis] * mean[axis]).max(0.0))
            .sum();
        let known = state == CaptureState::Done && self.samples > 0;
        let gaze = known && !self.closed;
        CaptureResult {
            id: self.request.id,
            target: self.request.target.clone(),
            state,
            x: gaze.then_some(mean[0] as f32),
            y: gaze.then_some(mean[1] as f32),
            spread: gaze.then_some(variance.sqrt() as f32),
            x_left: gaze.then_some((self.eye_x[0] / count) as f32),
            x_right: gaze.then_some((self.eye_x[1] / count) as f32),
            openness: known.then(|| self.openness.map(|sum| (sum / count) as f32)),
            samples: if state == CaptureState::Done { self.samples } else { 0 },
        }
    }
}

impl CaptureResult {
    /// One line for the journal, so a calibration can be read back later.
    pub fn log_line(&self) -> String {
        let Some([left, right]) = self.openness else {
            return format!("Gaze capture {} ({}): no usable samples", self.id, self.target);
        };
        let gaze = match (self.x, self.y, self.spread, self.x_left, self.x_right) {
            (Some(x), Some(y), Some(spread), Some(left), Some(right)) => {
                format!("x {x:+.4}, y {y:+.4}, spread {spread:.4}, eye x L {left:+.4} R {right:+.4}, ")
            }
            _ => String::new(),
        };
        format!(
            "Gaze capture {} ({}): {gaze}openness L {left:.3} R {right:.3} from {} samples",
            self.id, self.target, self.samples
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn capture() -> Capture {
        Capture::new(GazeCapture {
            id: 7,
            target: "up".into(),
            seconds: CAPTURE_SECONDS,
            skip: CAPTURE_SKIP,
        })
    }

    #[test]
    fn averages_only_the_settled_open_eyed_samples() {
        let mut capture = capture();
        let mut over = false;
        for i in 0..=180 {
            let time = 100.0 + f64::from(i) / 90.0;
            // Wild gaze while settling, alternating around (0.1, 0.3) after, and a blink in between.
            let settled = time - 100.0 >= CAPTURE_SKIP;
            let gaze = if !settled {
                [0.9, -0.9]
            } else if i % 2 == 0 {
                [0.11, 0.31]
            } else {
                [0.09, 0.29]
            };
            let blinking = (90..100).contains(&i);
            let (gaze, openness) = if blinking { ([-1.0, -1.0], [0.1, 0.1]) } else { (gaze, [0.9, 0.8]) };
            over = capture.add(time, gaze, [gaze[0] + 0.02, gaze[0] - 0.02], openness, !blinking);
            if over {
                break;
            }
        }
        assert!(over);
        let result = capture.result(CaptureState::Done);
        assert_eq!((result.id, result.target.as_str(), result.state), (7, "up", CaptureState::Done));
        // 1.5 s of samples at 90 Hz, less the 10 blinking ones.
        assert!((124..=126).contains(&result.samples), "{}", result.samples);
        assert!((result.x.unwrap() - 0.10).abs() < 1e-3 && (result.y.unwrap() - 0.30).abs() < 1e-3);
        let [left, right] = result.openness.unwrap();
        assert!((left - 0.9).abs() < 1e-4 && (right - 0.8).abs() < 1e-4);
        assert!((result.x_left.unwrap() - 0.12).abs() < 1e-3 && (result.x_right.unwrap() - 0.08).abs() < 1e-3);
        let spread = result.spread.unwrap();
        // Each axis is off by 0.01 either way.
        assert!((spread - 0.0002_f32.sqrt()).abs() < 1e-4, "{spread}");
        let line = result.log_line();
        let expected = format!("from {} samples", result.samples);
        assert!(line.starts_with("Gaze capture 7 (up): x +0.") && line.ends_with(&expected), "{line}");
    }

    #[test]
    fn eyes_that_read_less_open_are_still_measured() {
        // Whatever gaze_hold_below is: 0.45 looking down (0.65 straight ahead) counts.
        assert!(usable([0.45, 0.45], [true; 2]));
        assert!(usable([0.3, 0.9], [true, false]));
        // A shut or closing eye does not, nor a sample with no reliable eye.
        assert!(!usable([0.25, 0.6], [true; 2]));
        assert!(!usable([0.8, 0.8], [false; 2]));
        // A whole capture from such a face: every open sample counts.
        let mut capture = capture();
        for i in 0..=180 {
            let openness = [0.45, 0.42];
            if capture.add(f64::from(i) / 90.0, [0.0, -0.33], [0.0, 0.0], openness, usable(openness, [true; 2])) {
                break;
            }
        }
        assert!(capture.result(CaptureState::Done).samples >= 134);
    }

    #[test]
    fn nothing_usable_reports_no_average() {
        let mut capture = capture();
        for i in 0..200 {
            if capture.add(f64::from(i) / 90.0, [0.0, 0.0], [0.0, 0.0], [0.2, 0.2], false) {
                break;
            }
        }
        let result = capture.result(CaptureState::Done);
        assert_eq!((result.samples, result.x, result.spread, result.openness), (0, None, None, None));
        assert_eq!(result.log_line(), "Gaze capture 7 (up): no usable samples");
        let running = capture.result(CaptureState::Running);
        assert_eq!((running.state, running.x, running.samples), (CaptureState::Running, None, 0));
    }

    #[test]
    fn the_eyes_shut_capture_keeps_every_sample_and_reports_only_openness() {
        let mut capture = Capture::new(GazeCapture {
            id: 9,
            target: CLOSED_TARGET.into(),
            seconds: 3.0,
            skip: 0.5,
        });
        let mut over_at = None;
        for i in 0..400 {
            // The gaze is held (not usable) all along, as it is with the eyes shut.
            if capture.add(f64::from(i) / 90.0, [0.5, -0.5], [0.5, 0.5], [0.15, 0.26], false) {
                over_at = Some(i);
                break;
            }
        }
        // As long as asked (3 s at 90 Hz), less the first half second
        assert_eq!(over_at, Some(270));
        let result = capture.result(CaptureState::Done);
        assert!((224..=226).contains(&result.samples), "{}", result.samples);
        assert_eq!((result.x, result.y, result.spread, result.x_left), (None, None, None, None));
        let [left, right] = result.openness.unwrap();
        assert!((left - 0.15).abs() < 1e-4 && (right - 0.26).abs() < 1e-4);
        assert_eq!(result.log_line(), format!("Gaze capture 9 (closed): openness L 0.150 R 0.260 from {} samples", result.samples));
    }
}
