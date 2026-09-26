//! Settings: built-in defaults, the JSON file the panel writes, and command-line options on top.

use crate::Args;
use clap::ArgMatches;
use clap::parser::ValueSource;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs;
use std::io;
use std::net::IpAddr;
use std::os::unix::fs::MetadataExt;
use std::path::PathBuf;
use std::time::{Duration, Instant};

// How often the config file's modification time is checked.
const CHECK_INTERVAL: Duration = Duration::from_secs(1);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize, clap::ValueEnum)]
#[serde(rename_all = "lowercase")]
pub enum OutputKind {
    /// VRChat avatar parameters (eyelid 0.75 = relaxed, 1.0 = widened)
    Vrchat,
    /// VRCFaceTracking's ETVR Tracking Module (six values, eyelid 1.0 = relaxed)
    Etvr,
}

impl OutputKind {
    pub fn default_port(self) -> u16 {
        match self {
            Self::Vrchat => 9000,
            Self::Etvr => 8889,
        }
    }
}

/// Everything that can change while running. Field names are the config.json keys, and the
/// defaults match the command-line defaults in `Args`.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub sending: bool,
    pub output: OutputKind,
    /// "auto" for the Steam Link PC, else an IP address or host name without a port.
    pub host: String,
    /// None uses the output's default port.
    pub port: Option<u16>,
    /// Without a trailing slash; empty for no prefix.
    pub prefix: String,
    pub raw: bool,
    pub gaze_min_cutoff: f32,
    pub gaze_beta: f32,
    pub gaze_d_cutoff: f32,
    pub gaze_deadzone: f32,
    pub gaze_hold_below: f32,
    pub independent_eyes: bool,
    pub lid_min_cutoff: f32,
    pub lid_beta: f32,
    pub lid_closed: f32,
    pub lid_open: f32,
    pub lid_widen_start: f32,
    pub lid_wide: f32,
    pub lid_scale_left: Option<f32>,
    pub lid_scale_right: Option<f32>,
    pub lid_calibration: bool,
    pub lid_sync: f32,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            sending: true,
            output: OutputKind::Vrchat,
            host: "auto".into(),
            port: None,
            prefix: "/FT".into(),
            raw: false,
            gaze_min_cutoff: 0.4,
            gaze_beta: 0.8,
            gaze_d_cutoff: 0.5,
            gaze_deadzone: 0.03,
            gaze_hold_below: 0.5,
            independent_eyes: false,
            lid_min_cutoff: 6.0,
            lid_beta: 5.0,
            lid_closed: 0.30,
            lid_open: 0.80,
            lid_widen_start: 0.92,
            lid_wide: 1.00,
            lid_scale_left: None,
            lid_scale_right: None,
            lid_calibration: true,
            lid_sync: 0.4,
        }
    }
}

impl Settings {
    pub fn port(&self) -> u16 {
        self.port.unwrap_or(self.output.default_port())
    }

    pub fn validate(&self) -> Result<(), String> {
        let numbers = [
            self.gaze_min_cutoff,
            self.gaze_beta,
            self.gaze_d_cutoff,
            self.gaze_deadzone,
            self.gaze_hold_below,
            self.lid_min_cutoff,
            self.lid_beta,
            self.lid_closed,
            self.lid_open,
            self.lid_widen_start,
            self.lid_wide,
            self.lid_sync,
        ];
        let scales = [self.lid_scale_left, self.lid_scale_right];
        if !numbers.iter().chain(scales.iter().flatten()).all(|value| value.is_finite()) {
            return Err("settings must be finite numbers".into());
        }
        let host_ok = self.host.parse::<IpAddr>().is_ok()
            || !(self.host.is_empty() || self.host.contains(|c: char| c == ':' || c.is_whitespace()));
        if !host_ok {
            return Err("host must be \"auto\", an IP address or a host name without a port".into());
        }
        if self.port == Some(0) {
            return Err("port must be between 1 and 65535".into());
        }
        if !(self.prefix.is_empty() || self.prefix.starts_with('/')) {
            return Err("prefix must be empty or an OSC path starting with /".into());
        }
        // Every number is finite by now, so plain comparisons are safe.
        if self.lid_closed >= self.lid_open {
            return Err("lid_closed must be below lid_open".into());
        }
        if self.lid_open > self.lid_widen_start {
            return Err("lid_widen_start must not be below lid_open".into());
        }
        let cutoffs_ok = [self.gaze_min_cutoff, self.gaze_d_cutoff, self.lid_min_cutoff]
            .iter()
            .all(|cutoff| *cutoff > 0.0);
        let non_negative_ok = [self.gaze_beta, self.lid_beta, self.gaze_deadzone]
            .iter()
            .all(|value| *value >= 0.0);
        if !cutoffs_ok || !non_negative_ok {
            return Err("filter cutoffs must be positive; betas and the deadzone non-negative".into());
        }
        if !scales.iter().flatten().all(|scale| *scale > 0.0) {
            return Err("lid_scale_left/right must be positive".into());
        }
        if self.lid_sync < 0.0 {
            return Err("lid_sync must be non-negative".into());
        }
        Ok(())
    }
}

/// Keys the panel uses to send one-off requests rather than settings.
#[derive(Default, Deserialize)]
#[serde(default)]
struct Requests {
    /// Bumped to make the learned eyelid calibration start over.
    calibration_reset: i64,
}

/// Parse config.json: missing keys keep their defaults and unknown keys are ignored.
fn parse(text: &str) -> Result<(Settings, i64), String> {
    let settings: Settings = serde_json::from_str(text).map_err(|error| error.to_string())?;
    let requests: Requests = serde_json::from_str(text).map_err(|error| error.to_string())?;
    Ok((settings, requests.calibration_reset))
}

/// Ids of the options that were typed on the command line rather than left at their defaults.
pub fn given_options(matches: &ArgMatches) -> HashSet<String> {
    matches
        .ids()
        .filter(|id| matches.value_source(id.as_str()) == Some(ValueSource::CommandLine))
        .map(|id| id.to_string())
        .collect()
}

/// Split `--target HOST:PORT` (HOST may be a bracketed IPv6 address).
pub fn split_target(target: &str) -> Option<(String, u16)> {
    let (host, port) = target.rsplit_once(':')?;
    let host = host
        .strip_prefix('[')
        .and_then(|host| host.strip_suffix(']'))
        .unwrap_or(host);
    let port = port.parse().ok()?;
    (!host.is_empty()).then(|| (host.to_owned(), port))
}

/// Overwrite `settings` with the options given on the command line; returns the keys they pin.
pub fn apply_args(settings: &mut Settings, args: &Args, given: &HashSet<String>) -> Vec<&'static str> {
    let mut locked = Vec::new();
    macro_rules! pin {
        ($($field:ident),*) => {$(
            if given.contains(stringify!($field)) {
                settings.$field = args.$field;
                locked.push(stringify!($field));
            }
        )*};
    }
    pin!(output);
    if given.contains("target") {
        locked.push("host");
        // main() has already rejected anything that is neither "auto" nor HOST:PORT.
        match split_target(&args.target) {
            Some((host, port)) => {
                settings.host = host;
                settings.port = Some(port);
                locked.push("port");
            }
            _ => settings.host = "auto".into(),
        }
    }
    if given.contains("port") && !locked.contains(&"port") {
        settings.port = args.port;
        locked.push("port");
    }
    if given.contains("prefix") {
        settings.prefix.clone_from(&args.prefix);
        locked.push("prefix");
    }
    pin!(
        raw,
        gaze_min_cutoff,
        gaze_beta,
        gaze_d_cutoff,
        gaze_deadzone,
        gaze_hold_below,
        independent_eyes,
        lid_min_cutoff,
        lid_beta,
        lid_closed,
        lid_open,
        lid_widen_start,
        lid_wide,
        lid_scale_left,
        lid_scale_right
    );
    if given.contains("no_lid_calibration") {
        settings.lid_calibration = !args.no_lid_calibration;
        locked.push("lid_calibration");
    }
    pin!(lid_sync);
    // "/" alone means no prefix, like "".
    let prefix = settings.prefix.trim_end_matches('/');
    settings.prefix = prefix.to_owned();
    locked
}

pub struct Reload {
    pub settings: Settings,
    pub reset_calibration: bool,
}

/// Watches the config file and merges it with the command line whenever it changes.
pub struct Config {
    pub path: Option<PathBuf>,
    args: Args,
    given: HashSet<String>,
    pub locked: Vec<&'static str>,
    pub error: Option<String>,
    // Modification time, size and inode; None while the file does not exist.
    stamp: Option<(i64, i64, u64, u64)>,
    last_check: Instant,
    // calibration_reset from the last good read (0 without a file); None until there has been one.
    reset: Option<i64>,
}

impl Config {
    pub fn new(path: Option<PathBuf>, args: Args, given: HashSet<String>) -> Self {
        let locked = apply_args(&mut Settings::default(), &args, &given);
        Self {
            path,
            args,
            given,
            locked,
            error: None,
            stamp: None,
            last_check: Instant::now(),
            reset: None,
        }
    }

    /// The settings to start with. A broken file falls back to the defaults (plus options) and is
    /// reported in `error`; only invalid command-line options are fatal.
    pub fn load(&mut self) -> Result<Settings, String> {
        let mut fallback = Settings::default();
        apply_args(&mut fallback, &self.args, &self.given);
        fallback.validate()?;
        self.stamp = self.stamp();
        match self.read() {
            Ok((settings, reset)) => {
                if reset.is_some() {
                    eprintln!("Loaded {}", self.display_path());
                }
                self.reset = Some(reset.unwrap_or(0));
                Ok(settings)
            }
            Err(error) => {
                eprintln!("{}: {error}; using the defaults", self.display_path());
                self.error = Some(error);
                Ok(fallback)
            }
        }
    }

    /// Check the file once a second; returns new settings when it changed and is valid.
    pub fn poll(&mut self) -> Option<Reload> {
        if self.last_check.elapsed() < CHECK_INTERVAL {
            return None;
        }
        self.last_check = Instant::now();
        self.check()
    }

    fn check(&mut self) -> Option<Reload> {
        let stamp = self.stamp();
        if stamp == self.stamp {
            return None;
        }
        self.stamp = stamp;
        match self.read() {
            Ok((settings, reset)) => {
                match reset {
                    Some(_) => eprintln!("Loaded {}", self.display_path()),
                    None => eprintln!("{} is gone; using the defaults", self.display_path()),
                }
                self.error = None;
                // Removing the file resets the counter to 0 but is not itself a request.
                let reset_calibration = reset.is_some_and(|new| self.reset.is_some_and(|old| old != new));
                self.reset = Some(reset.unwrap_or(0));
                Some(Reload {
                    settings,
                    reset_calibration,
                })
            }
            Err(error) => {
                eprintln!("{}: {error}; keeping the previous settings", self.display_path());
                self.error = Some(error);
                None
            }
        }
    }

    /// The merged settings, and calibration_reset if the file exists.
    fn read(&self) -> Result<(Settings, Option<i64>), String> {
        let text = match self.path.as_deref().map(fs::read_to_string) {
            Some(Ok(text)) => Some(text),
            Some(Err(error)) if error.kind() != io::ErrorKind::NotFound => {
                return Err(error.to_string());
            }
            _ => None,
        };
        let (mut settings, reset) = match text {
            Some(text) => parse(&text).map(|(settings, reset)| (settings, Some(reset)))?,
            None => (Settings::default(), None),
        };
        apply_args(&mut settings, &self.args, &self.given);
        settings.validate()?;
        Ok((settings, reset))
    }

    fn stamp(&self) -> Option<(i64, i64, u64, u64)> {
        let metadata = fs::metadata(self.path.as_deref()?).ok()?;
        Some((metadata.mtime(), metadata.mtime_nsec(), metadata.size(), metadata.ino()))
    }

    fn display_path(&self) -> String {
        self.path
            .as_deref()
            .map_or_else(|| "config".into(), |path| path.display().to_string())
    }
}

/// `$XDG_CONFIG_HOME/frameeyeosc` or `~/.config/frameeyeosc`.
pub fn config_dir() -> Option<PathBuf> {
    let config = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")))?;
    Some(config.join("frameeyeosc"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use clap::{CommandFactory, FromArgMatches};
    use std::path::Path;

    fn cli(argv: &[&str]) -> (Args, HashSet<String>) {
        let matches = Args::command()
            .try_get_matches_from(std::iter::once("frameeyeosc").chain(argv.iter().copied()))
            .unwrap();
        (Args::from_arg_matches(&matches).unwrap(), given_options(&matches))
    }

    fn merged(file: &str, argv: &[&str]) -> Result<(Settings, Vec<&'static str>), String> {
        let (args, given) = cli(argv);
        let (mut settings, _) = parse(file)?;
        let locked = apply_args(&mut settings, &args, &given);
        settings.validate()?;
        Ok((settings, locked))
    }

    #[test]
    fn command_line_defaults_match_the_config_defaults() {
        let (settings, locked) = merged("{}", &[]).unwrap();
        assert_eq!(settings, Settings::default());
        assert!(locked.is_empty());
    }

    #[test]
    fn missing_keys_keep_defaults_and_unknown_keys_are_ignored() {
        let (settings, reset) =
            parse(r#"{"version": 1, "language": "en", "lid_open": 0.85, "port": 9001, "extra": [1]}"#)
                .unwrap();
        assert_eq!(settings.lid_open, 0.85);
        assert_eq!(settings.port, Some(9001));
        assert_eq!(settings.lid_closed, 0.30);
        assert_eq!(settings.host, "auto");
        assert_eq!(reset, 0);
        assert_eq!(parse(r#"{"calibration_reset": 3}"#).unwrap().1, 3);
        // Integers are fine where a decimal is expected.
        assert_eq!(parse(r#"{"lid_min_cutoff": 10}"#).unwrap().0.lid_min_cutoff, 10.0);
    }

    #[test]
    fn bad_files_and_values_are_rejected() {
        assert!(merged("{\"lid_open\": 0.8,", &[]).is_err());
        assert!(merged(r#"{"output": "osc"}"#, &[]).is_err());
        assert!(merged(r#"{"port": 70000}"#, &[]).is_err());
        assert!(merged(r#"{"port": 0}"#, &[]).is_err());
        assert!(merged(r#"{"gaze_beta": "fast"}"#, &[]).is_err());
        assert!(merged(r#"{"lid_closed": 0.9}"#, &[]).is_err());
        assert!(merged(r#"{"lid_widen_start": 0.5}"#, &[]).is_err());
        assert!(merged(r#"{"gaze_min_cutoff": 0}"#, &[]).is_err());
        assert!(merged(r#"{"lid_scale_left": -1}"#, &[]).is_err());
        assert!(merged(r#"{"lid_sync": -0.1}"#, &[]).is_err());
        assert!(merged(r#"{"lid_wide": 1e300}"#, &[]).is_err());
        assert!(merged(r#"{"host": "192.168.0.60:9000"}"#, &[]).is_err());
        assert!(merged(r#"{"prefix": "FT"}"#, &[]).is_err());
        assert!(merged(r#"{"host": "fe80::1"}"#, &[]).is_ok());
    }

    #[test]
    fn command_line_options_win_and_are_locked() {
        let file = r#"{"output": "etvr", "host": "10.0.0.2", "port": 9100, "raw": false,
            "lid_open": 0.85, "lid_calibration": true, "gaze_beta": 2.0}"#;
        let (settings, locked) = merged(
            file,
            &["--target", "192.168.0.60:9000", "--raw", "--no-lid-calibration", "--gaze-beta", "1.5"],
        )
        .unwrap();
        assert_eq!((settings.host.as_str(), settings.port), ("192.168.0.60", Some(9000)));
        assert!(settings.raw && !settings.lid_calibration);
        assert_eq!(settings.gaze_beta, 1.5);
        // Not given on the command line, so the file's values stay.
        assert_eq!(settings.output, OutputKind::Etvr);
        assert_eq!(settings.lid_open, 0.85);
        assert_eq!(locked, ["host", "port", "raw", "gaze_beta", "lid_calibration"]);

        let (settings, locked) =
            merged(file, &["--target", "auto", "--port", "9001", "--output", "vrchat"]).unwrap();
        assert_eq!((settings.host.as_str(), settings.port), ("auto", Some(9001)));
        assert_eq!(settings.output, OutputKind::Vrchat);
        assert_eq!(locked, ["output", "host", "port"]);

        let (settings, locked) = merged(file, &["--port", "9002"]).unwrap();
        assert_eq!((settings.host.as_str(), settings.port), ("10.0.0.2", Some(9002)));
        assert_eq!(locked, ["port"]);
    }

    #[test]
    fn prefix_can_be_empty() {
        for prefix in ["", "/"] {
            let (settings, locked) = merged("{}", &["--prefix", prefix]).unwrap();
            assert_eq!(settings.prefix, "");
            assert_eq!(locked, ["prefix"]);
        }
        assert_eq!(merged(r#"{"prefix": "/"}"#, &[]).unwrap().0.prefix, "");
        assert_eq!(merged(r#"{"prefix": "/FT/"}"#, &[]).unwrap().0.prefix, "/FT");
    }

    #[test]
    fn targets_split_into_host_and_port() {
        assert_eq!(split_target("192.168.0.60:9000"), Some(("192.168.0.60".into(), 9000)));
        assert_eq!(split_target("[::1]:8889"), Some(("::1".into(), 8889)));
        assert_eq!(split_target("pc.local:9000"), Some(("pc.local".into(), 9000)));
        assert_eq!(split_target("192.168.0.60"), None);
        assert_eq!(split_target(":9000"), None);
    }

    fn write_atomically(path: &Path, text: &str) {
        let temporary = path.with_extension("tmp");
        fs::write(&temporary, text).unwrap();
        fs::rename(temporary, path).unwrap();
    }

    #[test]
    fn broken_file_keeps_the_previous_settings_until_fixed() {
        let dir = std::env::temp_dir().join(format!("frameeyeosc-config-test-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("config.json");
        let (args, given) = cli(&["--lid-sync", "0.2"]);
        let mut config = Config::new(Some(path.clone()), args, given);

        // No file yet: defaults plus options, and no error.
        let settings = config.load().unwrap();
        assert_eq!(settings.lid_sync, 0.2);
        assert!(config.error.is_none() && config.check().is_none());

        write_atomically(&path, r#"{"lid_open": 0.85, "lid_sync": 0.9, "calibration_reset": 0}"#);
        let reload = config.check().unwrap();
        assert_eq!((reload.settings.lid_open, reload.settings.lid_sync), (0.85, 0.2));
        assert!(!reload.reset_calibration);

        write_atomically(&path, r#"{"lid_open": 0.9"#);
        assert!(config.check().is_none());
        assert!(config.error.is_some());
        write_atomically(&path, r#"{"lid_open": 0.1}"#);
        assert!(config.check().is_none());
        assert_eq!(config.error.as_deref(), Some("lid_closed must be below lid_open"));

        write_atomically(&path, r#"{"lid_open": 0.9, "calibration_reset": 1}"#);
        let reload = config.check().unwrap();
        assert_eq!(reload.settings.lid_open, 0.9);
        assert!(reload.reset_calibration && config.error.is_none());
        // Unchanged file: nothing to do.
        assert!(config.check().is_none());

        // Removing the file brings back the defaults without resetting the calibration, and the
        // panel's next write starts counting from 0 again.
        fs::remove_file(&path).unwrap();
        let reload = config.check().unwrap();
        assert_eq!(reload.settings.lid_open, 0.80);
        assert!(!reload.reset_calibration);
        write_atomically(&path, r#"{"calibration_reset": 0}"#);
        assert!(!config.check().unwrap().reset_calibration);
        write_atomically(&path, r#"{"calibration_reset": 1}"#);
        assert!(config.check().unwrap().reset_calibration);
        fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn broken_file_at_startup_falls_back_to_defaults() {
        let dir = std::env::temp_dir().join(format!("frameeyeosc-config-start-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let path = dir.join("config.json");
        fs::write(&path, "not json").unwrap();
        let (args, given) = cli(&[]);
        let mut config = Config::new(Some(path), args, given);
        assert_eq!(config.load().unwrap(), Settings::default());
        assert!(config.error.is_some());
        // Invalid options on the command line are still fatal.
        let (args, given) = cli(&["--lid-closed", "0.9"]);
        assert!(Config::new(None, args, given).load().is_err());
        fs::remove_dir_all(dir).unwrap();
    }
}
