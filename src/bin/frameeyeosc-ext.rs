//! `frameeyeosc-ext`: a small tool and usage example for the extensions bus
//! (see docs/extensions.md). Build it with `cargo build --release --bin frameeyeosc-ext`;
//! it is a development helper, not part of the installed service.
//!
//!   frameeyeosc-ext list                      the apps currently on the bus
//!   frameeyeosc-ext send <app> <kind> [json]  send a message, no reply expected
//!   frameeyeosc-ext ask <app> <kind> [json]   send a message and print the first reply
//!   frameeyeosc-ext ping <app>                shorthand for `ask <app> ping`
//!   frameeyeosc-ext echo [name]               register as `name` and echo back every message

use clap::{Parser, Subcommand};
use frameeyeosc::extensions::{self, Envelope, Extension, Registration};
use serde_json::json;
use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

#[derive(Parser)]
#[command(about = "Inspect and talk to Steam Frame apps on the extensions bus")]
struct Cli {
    /// The bus folder [default: $XDG_RUNTIME_DIR/frame-apps]
    #[arg(long, global = true)]
    bus: Option<std::path::PathBuf>,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// List the apps currently registered on the bus
    List,
    /// Send a message to an app; no reply is waited for
    Send {
        /// The app's registered name (frameeyeosc, ext-echo, ...)
        app: String,
        /// The message kind (what the app accepts is listed by `list`)
        kind: String,
        /// The payload as JSON [default: null]
        data: Option<String>,
    },
    /// Send a message and print the first reply (within 2 s)
    Ask {
        app: String,
        kind: String,
        data: Option<String>,
    },
    /// Ask an app "ping" and print the answer
    Ping { app: String },
    /// Register as an app and echo every received message back to its sender
    Echo {
        /// The name to register as
        #[arg(default_value = "ext-echo")]
        name: String,
    },
}

static STOP: AtomicBool = AtomicBool::new(false);

extern "C" fn stop(_: libc::c_int) {
    STOP.store(true, Ordering::Relaxed);
}

fn parse_data(raw: Option<&str>) -> Result<serde_json::Value, String> {
    match raw {
        None => Ok(serde_json::Value::Null),
        Some(text) => {
            serde_json::from_str(text).map_err(|error| format!("--data is not valid JSON: {error}"))
        }
    }
}

fn describe(peer: &extensions::Peer) -> String {
    let app = &peer.descriptor;
    let accepts = if app.accepts.is_empty() {
        " (announces no kinds)".to_owned()
    } else {
        format!(" accepts: {}", app.accepts.join(", "))
    };
    let version = app.version.as_deref().unwrap_or("?");
    let description = app.description.as_deref().unwrap_or("");
    format!(
        "{} v{version} (pid {}){accepts}\n    {description}",
        app.name, app.pid
    )
}

fn print_reply(reply: Option<Envelope>) {
    match reply {
        Some(envelope) => println!("{} -> {}: {}", envelope.from, envelope.kind, envelope.data),
        None => println!("no reply within 2 s (the app may not answer this kind)"),
    }
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let bus = cli.bus.unwrap_or_else(extensions::bus_dir);
    match cli.command {
        Command::List => {
            let peers = extensions::discover(&bus);
            if peers.is_empty() {
                println!("no apps on the bus ({})", bus.display());
            }
            for peer in &peers {
                println!("{}", describe(peer));
            }
            ExitCode::SUCCESS
        }
        Command::Send { app, kind, data } => match parse_data(data.as_deref()) {
            Ok(data) => match extensions::send_once(&bus, "frameeyeosc-ext", &app, &kind, data) {
                Ok(_) => {
                    println!("sent {kind} to {app}");
                    ExitCode::SUCCESS
                }
                Err(error) => {
                    eprintln!("{error}");
                    ExitCode::FAILURE
                }
            },
            Err(error) => {
                eprintln!("{error}");
                ExitCode::FAILURE
            }
        },
        Command::Ask { app, kind, data } => match parse_data(data.as_deref()) {
            Ok(data) => {
                match extensions::request(&bus, &app, &kind, data, Duration::from_secs(2)) {
                    Ok(reply) => {
                        print_reply(reply);
                        ExitCode::SUCCESS
                    }
                    Err(error) => {
                        eprintln!("{error}");
                        ExitCode::FAILURE
                    }
                }
            }
            Err(error) => {
                eprintln!("{error}");
                ExitCode::FAILURE
            }
        },
        Command::Ping { app } => {
            let sent = json!({"t": std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_or(0.0, |since| since.as_secs_f64())});
            match extensions::request(&bus, &app, "ping", sent, Duration::from_secs(2)) {
                Ok(reply) => {
                    print_reply(reply);
                    ExitCode::SUCCESS
                }
                Err(error) => {
                    eprintln!("{error}");
                    ExitCode::FAILURE
                }
            }
        }
        Command::Echo { name } => {
            // The same signal handling as the daemon's recorder: exit cleanly on Ctrl+C so
            // the name is unregistered instead of going stale.
            unsafe {
                let mut action: libc::sigaction = std::mem::zeroed();
                action.sa_sigaction = stop as extern "C" fn(libc::c_int) as libc::sighandler_t;
                libc::sigemptyset(&mut action.sa_mask);
                for signal in [libc::SIGINT, libc::SIGTERM] {
                    libc::sigaction(signal, &action, std::ptr::null_mut());
                }
            }
            let info = Registration {
                version: Some(env!("CARGO_PKG_VERSION").into()),
                description: Some("frameeyeosc-ext echo: the extensions bus usage example".into()),
                accepts: vec!["echo".into()],
            };
            let mut app = match Extension::register_at(&bus, &name, info) {
                Ok(app) => app,
                Err(error) => {
                    eprintln!("could not register as {name:?}: {error}");
                    return ExitCode::FAILURE;
                }
            };
            println!("listening as {name} on {} (Ctrl+C to stop)", bus.display());
            while !STOP.load(Ordering::Relaxed) {
                for message in app.poll() {
                    println!("{} -> {}: {}", message.from, message.kind, message.data);
                    let kind = if message.kind == "ping" {
                        "pong"
                    } else {
                        "echo"
                    };
                    if let Err(error) = app.reply(&message, kind, message.data.clone()) {
                        eprintln!("could not reply to {}: {error}", message.from);
                    }
                }
                std::thread::sleep(Duration::from_millis(20));
            }
            println!("unregistered {name}");
            ExitCode::SUCCESS
        }
    }
}
