//! Recording the eye server's samples to CSV (--record), and running a recording back through the same
//! processing (--replay) to compare settings by a few numbers.

use crate::config::Settings;
use crate::{CAL_SETTLE, EyeData, LidCalibration, MAX_GAP, NOMINAL_DT, Sample, Smoother, TIMEOUT, step};
use std::error::Error;
use std::fs::{self, File};
use std::io::{self, BufWriter, Write};
use std::path::Path;

// A sample counts as part of a blink while both eyes' Frame openness is below this.
const BLINK_BELOW: f32 = 0.5;
// ...and as open while both are at least this.
const OPEN_ABOVE: f32 = 0.6;
// Blinks this few samples apart are one blink.
const BLINK_MERGE: usize = 2;
// How long after a blink its eyelids may still be closing, given the filters' lag.
const BLINK_TAIL: f64 = 0.15;
// Sent eyelids (VRCFT) at or below this look closed on an avatar.
const LID_SHUT: f32 = 0.05;
// Gaze jumps are looked for this close to a sample where either eye is below BLINK_BELOW.
const BLINK_NEAR: f64 = 0.1;
// Flicker is only measured this far from any sample where either eye is below BLINK_BELOW.
const OPEN_CLEAR: f64 = 0.2;
// Gaze jitter is the spread within windows this long.
const JITTER_WINDOW: f64 = 0.3;
// Gaze values of 1.0 are 45°.
const GAZE_DEGREES: f32 = 45.0;

/// Column names of a recording, in the order `values` lists them.
fn columns() -> Vec<String> {
    let mut names = vec!["sample_time".to_owned()];
    let per_eye = |names: &mut Vec<String>, name: &str| {
        for eye in ["left", "right"] {
            names.extend(["x", "y", "z"].map(|axis| format!("{name}_{eye}_{axis}")));
        }
    };
    per_eye(&mut names, "gaze");
    per_eye(&mut names, "gaze_cov");
    names.extend(["x", "y", "z"].map(|axis| format!("fixation_{axis}")));
    per_eye(&mut names, "pre_gaze");
    per_eye(&mut names, "pre_cov");
    names.extend(["openness_left".to_owned(), "openness_right".to_owned()]);
    names.extend((0..8).map(|i| format!("extra_{i}")));
    names
}

/// Every value of a sample, in column order.
fn values(data: &EyeData) -> Vec<f64> {
    let mut values = vec![data.sample_time];
    let floats = [
        data.gaze.as_flattened(),
        data.gaze_covariance.as_flattened(),
        &data.fixation_point,
        data.pre_fusion_gaze.as_flattened(),
        data.pre_fusion_covariance.as_flattened(),
        &data.openness,
        &data.extra,
    ];
    values.extend(floats.into_iter().flatten().map(|value| f64::from(*value)));
    values
}

/// The sample `values` came from.
fn from_values(values: &[f64]) -> EyeData {
    let mut floats = values[1..].iter().map(|value| *value as f32);
    let mut next = || floats.next().unwrap_or(f32::NAN);
    let mut vector = || [next(), next(), next()];
    let gaze = [vector(), vector()];
    let gaze_covariance = [vector(), vector()];
    let fixation_point = vector();
    let pre_fusion_gaze = [vector(), vector()];
    let pre_fusion_covariance = [vector(), vector()];
    let openness = [next(), next()];
    let extra = std::array::from_fn(|_| next());
    EyeData {
        sample_time: values[0],
        gaze,
        gaze_covariance,
        fixation_point,
        pre_fusion_gaze,
        pre_fusion_covariance,
        openness,
        extra,
    }
}

/// Writes samples to a CSV file, one line each.
pub struct Recorder {
    out: BufWriter<File>,
    pub count: u64,
}

impl Recorder {
    pub fn create(path: &Path) -> io::Result<Self> {
        let mut out = BufWriter::new(File::create(path)?);
        writeln!(out, "{}", columns().join(","))?;
        Ok(Self { out, count: 0 })
    }

    pub fn write(&mut self, data: &EyeData) -> io::Result<()> {
        // Floats print in their shortest form that reads back exactly.
        let line = values(data)
            .iter()
            .enumerate()
            .map(|(i, value)| if i == 0 { value.to_string() } else { (*value as f32).to_string() })
            .collect::<Vec<_>>()
            .join(",");
        writeln!(self.out, "{line}")?;
        self.count += 1;
        Ok(())
    }

    pub fn flush(&mut self) -> io::Result<()> {
        self.out.flush()
    }
}

/// Read a recording. Columns are found by name, so their order does not matter and extra ones are ignored.
fn parse(text: &str) -> Result<Vec<EyeData>, String> {
    let mut lines = text.lines().enumerate().filter(|(_, line)| !line.trim().is_empty());
    let (_, header) = lines.next().ok_or("the file is empty")?;
    let header: Vec<&str> = header.split(',').map(str::trim).collect();
    let positions = columns()
        .iter()
        .map(|name| {
            header
                .iter()
                .position(|column| column == name)
                .ok_or_else(|| format!("column {name} is missing"))
        })
        .collect::<Result<Vec<_>, _>>()?;
    lines
        .map(|(index, line)| {
            let fields: Vec<&str> = line.split(',').map(str::trim).collect();
            let values = positions
                .iter()
                .map(|position| fields.get(*position)?.parse::<f64>().ok())
                .collect::<Option<Vec<_>>>()
                .ok_or_else(|| format!("line {}: not a row of numbers", index + 1))?;
            Ok(from_values(&values))
        })
        .collect()
}

/// Everything sent for each sample, as the live loop would have sent it. The calibration settles and
/// restarts on sample time the way the live loop does on the clock.
fn replay(samples: &[EyeData], settings: &Settings, mut calibration: LidCalibration) -> Vec<Sample> {
    let mut smoother = Smoother::new(settings);
    let mut since = None;
    let mut last: Option<f64> = None;
    samples
        .iter()
        .map(|data| {
            let time = data.sample_time;
            if last.is_some_and(|last| time - last >= TIMEOUT.as_secs_f64()) {
                since = None;
            }
            last = Some(time);
            let settled = time - *since.get_or_insert(time) >= CAL_SETTLE.as_secs_f64();
            step(settings, &mut smoother, &mut calibration, data, settled)
        })
        .collect()
}

/// The settings with this version's new stages turned off, to compare against.
fn without_new_stages(settings: &Settings) -> Settings {
    Settings {
        gaze_quality_limit: 0.0,
        blink_hold_ms: 0.0,
        despike: false,
        blink_sync_below: 0.0,
        ..settings.clone()
    }
}

/// How the output behaved over a recording.
#[derive(Debug)]
struct Metrics {
    // Blinks where both eyes' Frame openness went below BLINK_BELOW together.
    blinks: usize,
    // ...of which both sent eyelids reached LID_SHUT at the same time.
    blinks_shut: usize,
    // Median over all blinks of how long both sent eyelids stayed shut together, in ms.
    shut_ms: f64,
    // Median spread of the sent combined gaze within JITTER_WINDOW windows with the eyes open, in degrees.
    jitter: f64,
    // 90th percentile of the sent combined gaze's change per sample around blinks, in degrees.
    blink_jump: f64,
    // Mean change of the sent eyelids per sample while the eyes are clearly open, in VRCFT units.
    flicker: f64,
    // Share of samples where each eye's gaze failed the quality check.
    unreliable: [f64; 2],
}

fn median(values: &mut [f64]) -> f64 {
    percentile(values, 50.0)
}

fn percentile(values: &mut [f64], percent: f64) -> f64 {
    if values.is_empty() {
        return f64::NAN;
    }
    values.sort_by(f64::total_cmp);
    values[((values.len() - 1) as f64 * percent / 100.0).round() as usize]
}

/// For each sample, whether one of the `marked` samples is at most `within` seconds away.
fn near(times: &[f64], marked: &[bool], within: f64) -> Vec<bool> {
    let mut out = vec![false; times.len()];
    let mut last = f64::NEG_INFINITY;
    for i in 0..times.len() {
        if marked[i] {
            last = times[i];
        }
        out[i] = times[i] - last <= within;
    }
    let mut next = f64::INFINITY;
    for i in (0..times.len()).rev() {
        if marked[i] {
            next = times[i];
        }
        out[i] |= next - times[i] <= within;
    }
    out
}

fn metrics(samples: &[EyeData], sent: &[Sample]) -> Metrics {
    let times: Vec<f64> = samples.iter().map(|data| data.sample_time).collect();
    let n = samples.len();
    // Whether sample i follows sample i - 1 without a gap.
    let joined = |i: usize| i > 0 && times[i] - times[i - 1] < MAX_GAP;
    let both_blinking: Vec<bool> = samples
        .iter()
        .map(|data| data.openness.iter().all(|openness| *openness < BLINK_BELOW))
        .collect();
    let either_blinking: Vec<bool> = samples
        .iter()
        .map(|data| data.openness.iter().any(|openness| *openness < BLINK_BELOW))
        .collect();
    let both_shut: Vec<bool> = sent
        .iter()
        .map(|sample| sample.lids.iter().all(|lid| *lid <= LID_SHUT))
        .collect();

    // Blinks as runs of both_blinking samples, allowing short breaks.
    let mut blinks: Vec<(usize, usize)> = Vec::new();
    for i in (0..n).filter(|i| both_blinking[*i]) {
        match blinks.last_mut() {
            Some((_, end)) if i - *end <= BLINK_MERGE + 1 && (*end + 1..=i).all(joined) => *end = i,
            _ => blinks.push((i, i)),
        }
    }
    let mut shut_ms: Vec<f64> = blinks
        .iter()
        .map(|&(start, end)| {
            // The longest stretch of both eyelids sent shut, from the blink's start until its tail is over.
            let mut longest = 0.0_f64;
            let mut run_start = None;
            let mut i = start;
            while i < n && times[i] <= times[end] + BLINK_TAIL && (i == start || joined(i)) {
                match (both_shut[i], run_start) {
                    (true, None) => run_start = Some(times[i]),
                    (false, Some(from)) => {
                        longest = longest.max(times[i] - from);
                        run_start = None;
                    }
                    _ => {}
                }
                i += 1;
            }
            if let Some(from) = run_start {
                longest = longest.max(times[i - 1] - from + f64::from(NOMINAL_DT));
            }
            longest * 1000.0
        })
        .collect();
    let blinks_shut = shut_ms.iter().filter(|ms| **ms > 0.0).count();

    let degrees = |sample: &Sample| [sample.gaze[4], sample.gaze[5]].map(|value| f64::from(value * GAZE_DEGREES));
    let mut spreads = Vec::new();
    let mut window: Vec<[f64; 2]> = Vec::new();
    let mut window_start = 0.0;
    for i in 0..n {
        let open = samples[i].openness.iter().all(|openness| *openness >= OPEN_ABOVE);
        if !open || !joined(i) {
            window.clear();
        }
        if !open {
            continue;
        }
        if window.is_empty() {
            window_start = times[i];
        }
        window.push(degrees(&sent[i]));
        if times[i] - window_start >= JITTER_WINDOW {
            let count = window.len() as f64;
            let variance: f64 = (0..2)
                .map(|axis| {
                    let mean = window.iter().map(|gaze| gaze[axis]).sum::<f64>() / count;
                    window.iter().map(|gaze| (gaze[axis] - mean).powi(2)).sum::<f64>() / count
                })
                .sum();
            spreads.push(variance.sqrt());
            window.clear();
        }
    }

    let around_blinks = near(&times, &either_blinking, BLINK_NEAR);
    let mut jumps: Vec<f64> = (1..n)
        .filter(|i| around_blinks[*i] && joined(*i))
        .map(|i| {
            let [x0, y0] = degrees(&sent[i - 1]);
            let [x1, y1] = degrees(&sent[i]);
            (x1 - x0).hypot(y1 - y0)
        })
        .collect();

    let blink_nearby = near(&times, &either_blinking, OPEN_CLEAR);
    let calm = |i: usize| {
        !blink_nearby[i] && samples[i].openness.iter().all(|openness| *openness >= OPEN_ABOVE)
    };
    let changes: Vec<f64> = (1..n)
        .filter(|i| joined(*i) && calm(*i) && calm(*i - 1))
        .flat_map(|i| (0..2).map(move |eye| f64::from((sent[i].lids[eye] - sent[i - 1].lids[eye]).abs())))
        .collect();

    let unreliable = [0, 1].map(|eye| {
        sent.iter().filter(|sample| !sample.reliable[eye]).count() as f64 / n.max(1) as f64
    });
    Metrics {
        blinks: blinks.len(),
        blinks_shut,
        shut_ms: median(&mut shut_ms),
        jitter: median(&mut spreads),
        blink_jump: percentile(&mut jumps, 90.0),
        flicker: changes.iter().sum::<f64>() / changes.len().max(1) as f64,
        unreliable,
    }
}

/// p50 / p90 / p99 of each eye's larger x/y variance in one of the covariance fields.
fn covariance_line(samples: &[EyeData], label: &str, field: fn(&EyeData) -> [[f32; 3]; 2], limit: f32) -> String {
    let eyes = [0, 1].map(|eye| {
        let mut values: Vec<f64> = samples
            .iter()
            .map(|data| {
                let [x, y, _] = field(data)[eye];
                f64::from(x.max(y))
            })
            .collect();
        let above = values.iter().filter(|value| **value > f64::from(limit)).count() as f64
            / values.len().max(1) as f64;
        format!(
            "{:.4} / {:.4} / {:.4} ({:.1}% above)",
            percentile(&mut values, 50.0),
            percentile(&mut values, 90.0),
            percentile(&mut values, 99.0),
            above * 100.0
        )
    });
    format!("  {label:<22} L {}\n  {:<22} R {}", eyes[0], "", eyes[1])
}

fn report(input: &Path, samples: &[EyeData], skipped: usize, settings: &Settings, before: &Metrics, after: &Metrics) -> String {
    let span = match (samples.first(), samples.last()) {
        (Some(first), Some(last)) => last.sample_time - first.sample_time,
        _ => 0.0,
    };
    let mut text = format!(
        "{}: {} samples over {span:.1} s{}\n\n",
        input.display(),
        samples.len(),
        if skipped > 0 { format!(" ({skipped} unreadable samples skipped)") } else { String::new() }
    );
    let row = |text: &mut String, label: &str, before: String, after: String| {
        text.push_str(&format!("{label:<44}{before:>16}{after:>16}\n"));
    };
    row(&mut text, "", "new stages off".into(), "these settings".into());
    let blinks = format!("both-eye blinks fully closed (of {})", after.blinks);
    row(&mut text, &blinks, before.blinks_shut.to_string(), after.blinks_shut.to_string());
    row(&mut text, "  median time fully closed (ms)", format!("{:.0}", before.shut_ms), format!("{:.0}", after.shut_ms));
    row(&mut text, "gaze jitter while fixating (deg)", format!("{:.3}", before.jitter), format!("{:.3}", after.jitter));
    row(&mut text, "gaze jump around blinks, p90 (deg)", format!("{:.3}", before.blink_jump), format!("{:.3}", after.blink_jump));
    row(&mut text, "eyelid flicker while open (VRCFT/sample)", format!("{:.4}", before.flicker), format!("{:.4}", after.flicker));
    let [left, right] = after.unreliable.map(|share| share * 100.0);
    row(&mut text, "gaze left out as unreliable (L / R)", "-".into(), format!("{left:.1}% / {right:.1}%"));
    text.push_str(&format!(
        "\nCovariance, larger of x/y per eye: p50 / p90 / p99 (share above gaze_quality_limit {})\n",
        settings.gaze_quality_limit
    ));
    let limit = settings.gaze_quality_limit;
    text.push_str(&covariance_line(samples, "after fusion", |data| data.gaze_covariance, limit));
    text.push('\n');
    text.push_str(&covariance_line(samples, "before fusion (used)", |data| data.pre_fusion_covariance, limit));
    text.push_str("\n\nextra_0..7 medians:");
    for i in 0..8 {
        let mut values: Vec<f64> = samples.iter().map(|data| f64::from(data.extra[i])).collect();
        text.push_str(&format!(" {:.4}", median(&mut values)));
    }
    text.push('\n');
    text
}

/// Every processed sample: Frame openness, the gaze and VRCFT eyelids as sent, and the quality check.
fn write_processed(path: &Path, samples: &[EyeData], sent: &[Sample]) -> io::Result<()> {
    let mut out = BufWriter::new(File::create(path)?);
    writeln!(
        out,
        "sample_time,openness_left,openness_right,gaze_left_x,gaze_left_y,gaze_right_x,gaze_right_y,\
         gaze_x,gaze_y,lid_left,lid_right,reliable_left,reliable_right"
    )?;
    for (data, sample) in samples.iter().zip(sent) {
        let mut fields = vec![data.sample_time.to_string()];
        fields.extend(data.openness.iter().chain(&sample.gaze).chain(&sample.lids).map(f32::to_string));
        fields.extend(sample.reliable.map(|reliable| u8::from(reliable).to_string()));
        writeln!(out, "{}", fields.join(","))?;
    }
    out.flush()
}

/// Replay a recording with the new stages off and with `settings`, and print the comparison.
pub fn run(input: &Path, output: Option<&Path>, settings: &Settings, calibration: &LidCalibration) -> Result<(), Box<dyn Error>> {
    let text = fs::read_to_string(input).map_err(|error| format!("{}: {error}", input.display()))?;
    let all = parse(&text).map_err(|error| format!("{}: {error}", input.display()))?;
    let samples: Vec<EyeData> = all.iter().copied().filter(EyeData::is_finite).collect();
    let before = replay(&samples, &without_new_stages(settings), calibration.clone());
    let after = replay(&samples, settings, calibration.clone());
    if let Some(output) = output {
        write_processed(output, &samples, &after).map_err(|error| format!("{}: {error}", output.display()))?;
    }
    let (before, after) = (metrics(&samples, &before), metrics(&samples, &after));
    print!("{}", report(input, &samples, all.len() - samples.len(), settings, &before, &after));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // Six seconds made up by tests/fixtures/make_synthetic.py: steady gaze with a little noise, four
    // blinks (the last seen closed by one eye only), a one-sample dropout, a stretch where the left eye's
    // covariance is high and its gaze wild, and a wink.
    const SYNTHETIC: &str = include_str!("../tests/fixtures/synthetic.csv");

    fn synthetic() -> Vec<EyeData> {
        parse(SYNTHETIC).unwrap()
    }

    fn calibration() -> LidCalibration {
        LidCalibration::load(None, Settings::default().lid_open)
    }

    fn run_with(settings: &Settings) -> Metrics {
        let samples = synthetic();
        metrics(&samples, &replay(&samples, settings, calibration()))
    }

    #[test]
    fn recordings_read_back_exactly() {
        let data = EyeData {
            sample_time: 19892.729813125,
            gaze: [[-0.014556, -0.276537, -0.960893], [-0.097535, -0.275248, -0.956413]],
            gaze_covariance: [[0.038169, 0.036536, 0.033531], [0.064817, 0.036536, 0.063212]],
            fixation_point: [-0.042491, -0.208806, -0.725547],
            pre_fusion_gaze: [[0.1, 0.2, -0.9], [0.3, 0.4, -0.8]],
            pre_fusion_covariance: [[1e-5, 2e-5, 3e-5], [0.5, 0.25, 0.125]],
            openness: [0.821048, 0.781017],
            extra: [0.077017, 0.019944, 0.105099, 0.034945, 0.000022, 0.000021, 0.000044, 0.000040],
        };
        let path = std::env::temp_dir().join(format!("frameeyeosc-record-{}.csv", std::process::id()));
        let mut recorder = Recorder::create(&path).unwrap();
        recorder.write(&data).unwrap();
        recorder.write(&EyeData { sample_time: 19892.74, ..data }).unwrap();
        recorder.flush().unwrap();
        drop(recorder);
        let read = parse(&fs::read_to_string(&path).unwrap()).unwrap();
        fs::remove_file(path).unwrap();
        assert_eq!(read, [data, EyeData { sample_time: 19892.74, ..data }]);
    }

    #[test]
    fn columns_are_found_by_name() {
        let text = "note,".to_owned() + &columns().join(",") + "\n" + "9," + &["1"; 38].join(",");
        let read = parse(&text).unwrap();
        assert_eq!((read[0].sample_time, read[0].extra[7]), (1.0, 1.0));
        assert!(parse("sample_time\n1\n").unwrap_err().contains("gaze_left_x"));
        let broken = columns().join(",") + "\n1,2,x";
        assert!(parse(&broken).unwrap_err().starts_with("line 2"));
    }

    #[test]
    fn synthetic_recording_has_the_expected_events() {
        let before = run_with(&without_new_stages(&Settings::default()));
        assert_eq!(before.blinks, 4, "{before:?}");
        assert!((before.unreliable[0], before.unreliable[1]) == (0.0, 0.0));
        let after = run_with(&Settings::default());
        // Both eyes fail the quality check during the four blinks (5 samples each), and the left eye also
        // during its bad stretch (27 samples), out of 540.
        let failed = after.unreliable.map(|share| (share * 540.0).round());
        assert_eq!(failed, [47.0, 20.0], "{after:?}");
    }

    #[test]
    fn new_stages_close_every_blink_and_steady_the_gaze() {
        let before = run_with(&without_new_stages(&Settings::default()));
        let after = run_with(&Settings::default());
        assert!(before.blinks_shut < before.blinks, "{before:?}");
        assert_eq!(after.blinks_shut, after.blinks, "{after:?}");
        // Held for --blink-hold-ms (80) from the last closed sample.
        assert!(after.shut_ms >= 80.0, "{after:?}");
        assert!(after.blink_jump < before.blink_jump, "{before:?} {after:?}");
        assert!(after.jitter < before.jitter, "{before:?} {after:?}");
        assert!(after.flicker <= before.flicker, "{before:?} {after:?}");
    }

    #[test]
    fn percentiles_pick_the_nearest_rank() {
        let mut values = [5.0, 1.0, 4.0, 2.0, 3.0];
        assert_eq!(median(&mut values), 3.0);
        assert_eq!(percentile(&mut values, 90.0), 5.0);
        assert!(median(&mut []).is_nan());
        let times = [0.0, 0.05, 0.1, 0.3, 0.5];
        assert_eq!(near(&times, &[false, false, true, false, false], 0.06), [false, true, true, false, false]);
    }
}
