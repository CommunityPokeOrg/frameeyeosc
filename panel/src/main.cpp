// frameeyeosc-panel: a SteamVR dashboard panel for frameeyeosc's settings. It only writes config.json and reads
// status.json; frameeyeosc keeps sending when the panel is closed, crashes or is not installed.
#include "autostart.h"
#include "config.h"
#include "draw.h"
#include "gaze_dots.h"
#include "gaze_fit.h"
#include "i18n.h"
#include "model.h"
#include "panel.h"
#include "sounds.h"
#include "status.h"
#include "target.h"
#include "theme.h"
#include "vr_overlay.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ctime>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t gStopRequested = 0;
volatile std::sig_atomic_t gShowRequested = 0;  ///< SIGUSR1 (from a second launch) opens the panel

/** Exit code after "Quit" or the dashboard's "close" (systemd's RestartPreventExitStatus=). */
constexpr int kExitCodeUserQuit = 3;

constexpr int kThumbnailSize = 256;       ///< dashboard thumbnail edge (px)
constexpr double kPanelPollSec = 0.033;   ///< event polling while the panel is visible
constexpr double kClosedPollSec = 0.25;   ///< event polling while it is not
constexpr double kFitPollSec = 1.0 / 90;  ///< every display frame while the eye fit's target is up (without frame sync)
constexpr uint32_t kFrameSyncTimeoutMs = 50;  ///< the longest wait for the compositor's next frame
constexpr double kStatusReadSec = 0.1;    ///< status.json is read this often while the panel is visible
constexpr double kUpdateSettleSec = 60.0; ///< --update-live: longest wait for a check or an install to finish

/** The command line. */
struct Options {
    enum class Mode { Overlay, Print, DumpPng, Probe, SwitchAway, ContrastReport, PlaySound, Help, Version };
    Mode mode = Mode::Overlay;
    std::string configPath;
    std::string statusPath;
    std::string pngPath;
    std::string thumbnailPngPath;
    int thumbnailSize = 256;
    std::string targetPngPath;    ///< --target-png: the eye fit's target image
    std::string targetStyle = "dot";
    std::string dotPngPath;       ///< --dot-png: a debug gaze dot image
    std::string playSound;        ///< --play-sound: play one eye fit cue and exit
    std::string dotKind = "both";
    int targetSeconds = 3;
    double targetProgress = 0.7;
    int targetBench = 0;          ///< --target-bench N: time drawing the target N times
    bool fitDetails = false;      ///< --fit-details: "Fine-tune" open on the Eye fit tab
    int fitDetailsPage = 0;       ///< --fit-details lids: its eyelid page
    std::string language;         ///< for --dump-png: overrides the config language (ja / en)
    PanelTab tab = PanelTab::Basic;
    bool previewQuit = false;
    bool previewReset = false;
    bool previewUpdatePrompt = false;
    double switchAwaySec = 3.0;
    // Fake states for --dump-png (any of them draws a made-up state instead of reading the files)
    bool fake = false;
    bool fakeNotRunning = false;
    bool fakePaused = false;
    bool fakeNoTracking = false;
    bool fakeEtvr = false;
    bool fakeFixed = false;
    bool fakeTargetNull = false;
    bool fakeLocked = false;
    bool fakeConfigError = false;
    bool fakeBroken = false;
    bool fakeWriteError = false;
    bool fakeCustom = false;
    bool fakeIndependent = false; ///< --fake-independent: independent_eyes on, the gaze pad per eye
    std::string fakePrompt;       ///< vrchat / etvr
    std::string fakeUpdate;       ///< a made-up update state (see printUsage)
    std::string fakeFit;          ///< a made-up eye fit state (see printUsage)
    bool updateLive = false;      ///< --dump-png: run the real update checker (and wait for it after clicks)
    std::vector<std::pair<double, double>> clicks;  ///< --click X,Y: presses carried out before the PNG is drawn
    Autostart fakeAutostart = Autostart::Disabled;
};

/**
 * Mark the process for a clean stop (SIGTERM / SIGINT).
 * @param signal the signal (unused)
 */
void onSignal(int /*signal*/) {
    gStopRequested = 1;
}

/**
 * Mark a request to open the panel (SIGUSR1 from a second launch).
 * @param signal the signal (unused)
 */
void onShowSignal(int /*signal*/) {
    gShowRequested = 1;
}

/** Install the SIGTERM / SIGINT / SIGUSR1 handlers. */
void installSignalHandlers() {
    struct sigaction show {};
    show.sa_handler = onShowSignal;
    sigemptyset(&show.sa_mask);
    sigaction(SIGUSR1, &show, nullptr);

    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
}

/**
 * Monotonic time in seconds.
 * @return seconds
 */
double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

/**
 * Sleep until a stop or show request, or until the time is up.
 * @param seconds how long to wait
 */
void sleepInterruptible(double seconds) {
    const double end = nowSeconds() + seconds;
    while (!gStopRequested && !gShowRequested) {
        const double left = end - nowSeconds();
        if (left <= 0) break;
        std::this_thread::sleep_for(std::chrono::duration<double>(std::fmin(left, 0.5)));
    }
}

/** Print the usage. */
void printUsage() {
    std::printf(
        "Usage: frameeyeosc-panel [options]\n"
        "  (none)                Show the panel on the SteamVR dashboard and stay resident (waits for SteamVR)\n"
        "                        If one is already running, open its panel and exit\n"
        "  --print               Without OpenVR: print what the panel sees (config, status, autostart)\n"
        "  --dump-png PATH       Without OpenVR: draw the panel to a PNG (from the real files) and exit\n"
        "  --thumbnail-png PATH  Draw the dashboard thumbnail (the launcher icon) to a PNG\n"
        "      --thumbnail-size N  Its edge length (default 256)\n"
        "  --play-sound NAME     Play one eye fit cue (pop, pip, buzz, tick, open, done, fail) and exit\n"
        "  --dot-png PATH        Draw a debug gaze dot to a PNG\n"
        "      --dot-kind both|left|right  Which one (default both)\n"
        "  --target-png PATH     Draw the eye fit's target (the head-locked dot) to a PNG\n"
        "      --target-style dot|close|keep|open  The dot, or the eyes-shut step (words from --language)\n"
        "      --target-seconds N  The countdown on it (default 3; 0 = none)\n"
        "      --target-progress F  How much of its ring is left, 0..1 (default 0.7)\n"
        "      --target-bench N  Also draw it N times and print how long one takes, then one gaze point\n"
        "                        paced at 90 frames/s\n"
        "      --language ja|en  Draw in this language instead of the config's\n"
        "      --tab basic|gaze|eyefit|lids|advanced  Draw this tab\n"
        "      --fit-details [gaze|lids]  Open \"Fine-tune\" on the Eye fit tab (default: its gaze page)\n"
        "      --preview-quit    Show \"press again to quit\"\n"
        "      --preview-reset   Show \"press again to reset\"\n"
        "      --preview-update-prompt  Show the \"update to ...?\" question (with --fake-update available)\n"
        "      --fake            Draw a made-up state (running, sending to VRChat) instead of the files.\n"
        "                        Each --fake-* below implies --fake\n"
        "      --fake-not-running / --fake-paused / --fake-no-tracking / --fake-etvr / --fake-fixed\n"
        "      --fake-target-null  Auto target not found yet\n"
        "      --fake-locked     Some keys locked by the command line\n"
        "      --fake-config-error  frameeyeosc reports a config error\n"
        "      --fake-broken     config.json can't be parsed\n"
        "      --fake-write-error  The panel failed to write config.json\n"
        "      --fake-custom     Gaze smoothing values that match no preset\n"
        "      --fake-independent  Move eyes separately (the left column shows each eye's gaze)\n"
        "      --fake-prompt vrchat|etvr  The recommended-settings question\n"
        "      --fake-autostart on|off|missing|unknown\n"
        "      --fake-update checking|uptodate|available|manual|installing|installed|checkfailed|installfailed\n"
        "                        A made-up update state (the version row on the Advanced tab)\n"
        "      --fake-fit waiting|waiting-center|running|running-closed|done|done-center|fitted|fitted-gaze|\n"
        "                 failed-unsteady|failed-notclosed|failed-movement|failed-lidrange|failed-cancelled|\n"
        "                 failed-noresult  A made-up eye fit (Eye fit tab)\n"
        "      --update-live     Run the real update checker: check first, and after each --click wait for the\n"
        "                        check or install it started (installs really happen; for testing with a fake GitHub)\n"
        "      --click X,Y       Press the panel at X,Y first (repeatable; writes --config; not with --fake)\n"
        "  --contrast-report     Print the WCAG contrast ratio of every color pair on screen\n"
        "  --probe               Diagnostics: connect to SteamVR as a background app and describe the resident panel\n"
        "  --probe-switch-away [S]  Diagnostics: switch the dashboard to a temporary overlay for S s (default 3)\n"
        "  --version             Print the version\n"
        "  --config PATH         Config file (default ~/.config/frameeyeosc/config.json)\n"
        "  --status PATH         Status file (default $XDG_RUNTIME_DIR/frameeyeosc/status.json)\n");
}

/**
 * Parse the command line.
 * @param argc argument count
 * @param argv arguments
 * @param options where to write
 * @return true if valid
 */
bool parseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasNext = i + 1 < argc;
        if (arg == "--print") {
            options.mode = Options::Mode::Print;
        } else if (arg == "--dump-png" && hasNext) {
            options.mode = Options::Mode::DumpPng;
            options.pngPath = argv[++i];
        } else if (arg == "--thumbnail-png" && hasNext) {
            options.mode = Options::Mode::DumpPng;
            options.thumbnailPngPath = argv[++i];
        } else if (arg == "--target-png" && hasNext) {
            options.mode = Options::Mode::DumpPng;
            options.targetPngPath = argv[++i];
        } else if (arg == "--play-sound" && hasNext) {
            options.mode = Options::Mode::PlaySound;
            options.playSound = argv[++i];
            sounds::Cue cue = sounds::Cue::Pop;
            if (!sounds::parse(options.playSound, cue)) {
                std::fprintf(stderr, "--play-sound: unknown cue %s\n", options.playSound.c_str());
                return false;
            }
        } else if (arg == "--dot-png" && hasNext) {
            options.mode = Options::Mode::DumpPng;
            options.dotPngPath = argv[++i];
        } else if (arg == "--dot-kind" && hasNext) {
            options.dotKind = argv[++i];
            if (options.dotKind != "both" && options.dotKind != "left" && options.dotKind != "right") {
                std::fprintf(stderr, "--dot-kind must be both, left or right: %s\n", options.dotKind.c_str());
                return false;
            }
        } else if (arg == "--target-style" && hasNext) {
            options.targetStyle = argv[++i];
            if (options.targetStyle != "dot" && options.targetStyle != "close" && options.targetStyle != "keep" &&
                options.targetStyle != "open") {
                std::fprintf(stderr, "--target-style must be dot, close, keep or open: %s\n", options.targetStyle.c_str());
                return false;
            }
        } else if (arg == "--target-bench" && hasNext) {
            options.targetBench = std::max(0, std::min(100000, std::atoi(argv[++i])));
        } else if (arg == "--fit-details") {
            options.fitDetails = true;
            if (hasNext && (std::string(argv[i + 1]) == "gaze" || std::string(argv[i + 1]) == "lids")) {
                options.fitDetailsPage = std::string(argv[++i]) == "lids" ? 1 : 0;
            }
        } else if (arg == "--target-seconds" && hasNext) {
            options.targetSeconds = std::max(0, std::min(99, std::atoi(argv[++i])));
        } else if (arg == "--target-progress" && hasNext) {
            options.targetProgress = std::max(0.0, std::min(1.0, std::atof(argv[++i])));
        } else if (arg == "--thumbnail-size" && hasNext) {
            options.thumbnailSize = std::max(16, std::min(1024, std::atoi(argv[++i])));
        } else if (arg == "--language" && hasNext) {
            options.language = argv[++i];
            Language check;
            if (!parseLanguage(options.language, check)) {
                std::fprintf(stderr, "--language must be ja or en: %s\n", options.language.c_str());
                return false;
            }
        } else if (arg == "--tab" && hasNext) {
            const std::string tab = argv[++i];
            if (tab == "basic") {
                options.tab = PanelTab::Basic;
            } else if (tab == "gaze") {
                options.tab = PanelTab::Gaze;
            } else if (tab == "eyefit") {
                options.tab = PanelTab::EyeFit;
            } else if (tab == "lids") {
                options.tab = PanelTab::Lids;
            } else if (tab == "advanced") {
                options.tab = PanelTab::Advanced;
            } else {
                std::fprintf(stderr, "--tab must be basic, gaze, eyefit, lids or advanced: %s\n", tab.c_str());
                return false;
            }
        } else if (arg == "--preview-quit") {
            options.previewQuit = true;
        } else if (arg == "--preview-reset") {
            options.previewReset = true;
        } else if (arg == "--preview-update-prompt") {
            options.previewUpdatePrompt = true;
        } else if (arg == "--fake") {
            options.fake = true;
        } else if (arg == "--fake-not-running") {
            options.fake = options.fakeNotRunning = true;
        } else if (arg == "--fake-paused") {
            options.fake = options.fakePaused = true;
        } else if (arg == "--fake-no-tracking") {
            options.fake = options.fakeNoTracking = true;
        } else if (arg == "--fake-etvr") {
            options.fake = options.fakeEtvr = true;
        } else if (arg == "--fake-fixed") {
            options.fake = options.fakeFixed = true;
        } else if (arg == "--fake-target-null") {
            options.fake = options.fakeTargetNull = true;
        } else if (arg == "--fake-locked") {
            options.fake = options.fakeLocked = true;
        } else if (arg == "--fake-config-error") {
            options.fake = options.fakeConfigError = true;
        } else if (arg == "--fake-broken") {
            options.fake = options.fakeBroken = true;
        } else if (arg == "--fake-write-error") {
            options.fake = options.fakeWriteError = true;
        } else if (arg == "--fake-custom") {
            options.fake = options.fakeCustom = true;
        } else if (arg == "--fake-independent") {
            options.fake = options.fakeIndependent = true;
        } else if (arg == "--fake-prompt" && hasNext) {
            options.fakePrompt = argv[++i];
            if (options.fakePrompt != kOutputVrchat && options.fakePrompt != kOutputEtvr) {
                std::fprintf(stderr, "--fake-prompt must be vrchat or etvr: %s\n", options.fakePrompt.c_str());
                return false;
            }
        } else if (arg == "--fake-autostart" && hasNext) {
            const std::string state = argv[++i];
            if (state == "on") {
                options.fakeAutostart = Autostart::Enabled;
            } else if (state == "off") {
                options.fakeAutostart = Autostart::Disabled;
            } else if (state == "missing") {
                options.fakeAutostart = Autostart::Missing;
            } else if (state == "unknown") {
                options.fakeAutostart = Autostart::Unknown;
            } else {
                std::fprintf(stderr, "--fake-autostart must be on, off, missing or unknown: %s\n", state.c_str());
                return false;
            }
            options.fake = true;
        } else if (arg == "--fake-update" && hasNext) {
            options.fakeUpdate = argv[++i];
            static const char* const kStates[] = {"checking",  "uptodate",  "available",   "manual",
                                                  "installing", "installed", "checkfailed", "installfailed"};
            if (std::find(std::begin(kStates), std::end(kStates), options.fakeUpdate) == std::end(kStates)) {
                std::fprintf(stderr, "--fake-update: unknown state %s\n", options.fakeUpdate.c_str());
                return false;
            }
            options.fake = true;
        } else if (arg == "--fake-fit" && hasNext) {
            options.fakeFit = argv[++i];
            static const char* const kFitStates[] = {
                "waiting",         "waiting-center",  "running",         "running-closed",   "done",
                "done-center",     "fitted",          "fitted-gaze",     "failed-unsteady",  "failed-notclosed",
                "failed-movement", "failed-lidrange", "failed-cancelled", "failed-noresult"};
            if (std::find(std::begin(kFitStates), std::end(kFitStates), options.fakeFit) == std::end(kFitStates)) {
                std::fprintf(stderr, "--fake-fit: unknown state %s\n", options.fakeFit.c_str());
                return false;
            }
            options.fake = true;
        } else if (arg == "--update-live") {
            options.updateLive = true;
        } else if (arg == "--version") {
            options.mode = Options::Mode::Version;
        } else if (arg == "--click" && hasNext) {
            double x = 0;
            double y = 0;
            if (std::sscanf(argv[++i], "%lf,%lf", &x, &y) != 2) {
                std::fprintf(stderr, "--click needs X,Y: %s\n", argv[i]);
                return false;
            }
            options.clicks.emplace_back(x, y);
        } else if (arg == "--contrast-report") {
            options.mode = Options::Mode::ContrastReport;
        } else if (arg == "--probe") {
            options.mode = Options::Mode::Probe;
        } else if (arg == "--probe-switch-away") {
            options.mode = Options::Mode::SwitchAway;
            if (hasNext && argv[i + 1][0] != '-') options.switchAwaySec = std::max(0.5, std::atof(argv[++i]));
        } else if (arg == "--config" && hasNext) {
            options.configPath = argv[++i];
        } else if (arg == "--status" && hasNext) {
            options.statusPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            options.mode = Options::Mode::Help;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            return false;
        }
    }
    if (options.fake && options.updateLive) {
        std::fprintf(stderr, "--update-live works on the real files, not with --fake\n");
        return false;
    }
    if (options.fake && !options.clicks.empty()) {
        std::fprintf(stderr, "--click works on the real files, not with --fake\n");
        return false;
    }
    if (options.configPath.empty()) options.configPath = defaultConfigPath();
    if (options.statusPath.empty()) options.statusPath = defaultStatusPath();
    return true;
}

/**
 * The panel language from config.json, or the Frame's system language if it has none (or an unknown one).
 * @param config the config
 * @return the language
 */
Language configLanguage(const ConfigFile& config) {
    Language language = systemLanguage();
    const JsonValue* written = config.root.get(key::kLanguage);
    if (written != nullptr && written->isString()) parseLanguage(written->text, language);
    return language;
}

/**
 * The update checker's settings: frame-update.sh as install.sh puts it, this release's tarball name and the
 * install.sh option used when ~/.config/frameeyeosc/install-args is missing (0.3.x did not write it).
 * @return the settings
 */
frame_updater::UpdaterConfig updaterConfig() {
    frame_updater::UpdaterConfig c;
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const std::string dataHome =
        data != nullptr && data[0] == '/' ? std::string(data) : std::string(home != nullptr ? home : "") + "/.local/share";
    c.script = dataHome + "/frameeyeosc/frame-update.sh";
    c.app = "frameeyeosc";
    c.repo = "sasaken1102r/frameeyeosc";
    c.currentVersion = FRAMEEYEOSC_VERSION;
    c.assetPattern = "frameeyeosc-{version}-steamframe-aarch64.tar.gz";
    // The update button is only in the panel, so whoever uses it has the panel installed
    c.defaultInstallArgs = {"--with-panel"};
    return c;
}

/**
 * A made-up update state for --fake-update.
 * @param state the state name
 * @return the status
 */
frame_updater::UpdateStatus fakeUpdate(const std::string& state) {
    using frame_updater::UpdateState;
    frame_updater::UpdateStatus u;
    u.current = FRAMEEYEOSC_VERSION;
    u.latest = u.current;
    u.url = "https://github.com/sasaken1102r/frameeyeosc/releases/latest";
    u.checkedAt = static_cast<long long>(std::time(nullptr)) - 600;
    if (state == "checking") {
        u.checking = true;
        u.checkedAt = 0;
    } else if (state == "uptodate") {
        u.state = UpdateState::UpToDate;
    } else if (state == "available" || state == "manual") {
        u.state = UpdateState::Available;
        u.latest = "9.9.9";
        u.installable = state == "available";
        if (!u.installable) u.reason = "no-checksums";
    } else if (state == "installing") {
        u.state = UpdateState::Installing;
        u.step = "download";
        u.version = "9.9.9";
    } else if (state == "installed") {
        u.state = UpdateState::Installed;
        u.version = "9.9.9";
    } else if (state == "checkfailed") {
        u.state = UpdateState::CheckFailed;
        u.error = "network";
    } else if (state == "installfailed") {
        u.state = UpdateState::InstallFailed;
        u.version = "9.9.9";
        u.error = "checksum-mismatch";
    }
    return u;
}

/**
 * A made-up model for --dump-png (--fake*), so every state can be checked without frameeyeosc.
 * @param options the command line
 * @return the model
 */
PanelModel fakeModel(const Options& options) {
    PanelModel m;
    m.configPath = options.configPath;
    m.statusPath = options.statusPath;
    m.config.exists = true;
    m.config.root.type = JsonValue::Type::Object;
    JsonValue& root = m.config.root;
    if (options.fakeEtvr) root.set(key::kOutput, JsonValue::makeString(kOutputEtvr));
    if (options.fakeFixed) root.set(key::kHost, JsonValue::makeString("192.168.0.60"));
    if (options.fakePaused) root.set(key::kSending, JsonValue::makeBool(false));
    if (options.fakeCustom) {
        root.set(key::kGazeMinCutoff, JsonValue::makeNumber(0.3));
        root.set(key::kGazeBeta, JsonValue::makeNumber(0.9));
        root.set(key::kGazeDeadzone, JsonValue::makeNumber(0.045));
        root.set(key::kLidScaleLeft, JsonValue::makeNumber(1.03));
        root.set(key::kLidScaleRight, JsonValue::makeNumber(0.97));
        root.set(key::kGazeHoldBelow, JsonValue::makeNumber(0.0));
        root.set(key::kPort, JsonValue::makeNumber(9001, true));
        root.set(key::kPrefix, JsonValue::makeString(""));
    }
    if (options.fakeBroken) m.config.error = "expected , or } between members (near character 212)";
    if (options.fakeIndependent) root.set(key::kIndependentEyes, JsonValue::makeBool(true));
    if (!options.fakeFit.empty()) {
        using gaze_fit::Failure;
        using gaze_fit::Phase;
        using gaze_fit::Point;
        gaze_fit::View& fit = m.fit;
        const std::string& state = options.fakeFit;
        fit.mode = state.find("center") != std::string::npos ? gaze_fit::Mode::Center : gaze_fit::Mode::Full;
        fit.count = gaze_fit::pointCount(fit.mode);
        // The fit in config.json: the gaze of the live run on 2026-09-28, and eyelids like the worn recording
        const bool saved = state.rfind("done", 0) == 0 || state.rfind("fitted", 0) == 0;
        if (saved) {
            root.set(key::kGazeOffsetX, JsonValue::makeNumber(0.012));
            root.set(key::kGazeOffsetY, JsonValue::makeNumber(-0.02));
            root.set(key::kGazeGainX, JsonValue::makeNumber(0.93));
            root.set(key::kGazeGainUp, JsonValue::makeNumber(0.9));
            root.set(key::kGazeGainDown, JsonValue::makeNumber(0.88));
            // Each eye's own sideways fit (made-up numbers)
            root.set(key::kGazeOffsetXLeft, JsonValue::makeNumber(0.031));
            root.set(key::kGazeOffsetXRight, JsonValue::makeNumber(-0.006));
            root.set(key::kGazeGainXLeft, JsonValue::makeNumber(0.95));
            root.set(key::kGazeGainXRight, JsonValue::makeNumber(0.9));
        }
        if (saved && state != "fitted-gaze") {
            const double readings[2][4] = {{0.15, 0.93, 0.92, 0.77}, {0.26, 0.86, 0.81, 0.75}};
            for (int eye = 0; eye < 2; ++eye) {
                for (int i = 0; i < 4; ++i) root.set(kLidFitKeys[eye][i], JsonValue::makeNumber(readings[eye][i]));
            }
        }
        if (state.rfind("waiting", 0) == 0) {
            fit.phase = Phase::Waiting;
        } else if (state == "running") {
            fit.phase = Phase::Capturing;
            fit.index = 2;
            fit.point = Point::Down;
            fit.attempt = 2;
        } else if (state == "running-closed") {
            fit.phase = Phase::Settling;
            fit.index = 5;
            fit.point = Point::Closed;
        } else if (state.rfind("done", 0) == 0) {
            fit.phase = Phase::Done;
        } else if (saved) {
            fit.phase = Phase::Idle;
        } else {
            fit.phase = Phase::Failed;
            fit.failure = state == "failed-unsteady"    ? Failure::Unsteady
                          : state == "failed-notclosed" ? Failure::NotClosed
                          : state == "failed-movement"  ? Failure::NoMovement
                          : state == "failed-lidrange"  ? Failure::NoLidRange
                          : state == "failed-cancelled" ? Failure::Cancelled
                                                        : Failure::NoResult;
            fit.point = state == "failed-movement" ? Point::Down : Point::Left;
        }
    }

    EyeStatus& s = m.status;
    if (!options.fakeNotRunning) {
        const double now = unixNow();
        const bool etvr = options.fakeEtvr;
        s.present = true;
        s.running = true;
        s.pid = 12345;
        s.time = now;
        s.started = now - 4980;
        s.sending = !options.fakePaused;
        s.output = etvr ? kOutputEtvr : kOutputVrchat;
        s.targetMode = options.fakeFixed ? "fixed" : "auto";
        if (!options.fakeTargetNull) {
            s.target = std::string("192.168.0.60:") + (etvr ? "8889" : (options.fakeCustom ? "9001" : "9000"));
        }
        s.rate = options.fakePaused ? 0.0 : 89.6;
        s.tracking = !options.fakeNoTracking;
        if (s.tracking) {
            s.hasRaw = true;
            s.openness = {{0.81, 0.79}};
            s.opennessScaled = {{0.84, 0.77}};
            s.gaze = {{0.22, -0.14}};
            s.hasSent = true;
            s.lidsVrcft = {{0.75, 0.72}};
            s.lids = etvr ? Pair {{1.0, 0.96}} : s.lidsVrcft;
            s.sentGaze = {{0.17, -0.10}};
            // Each eye turned in a little (left eye right of the combined gaze, right eye left of it)
            s.rawGazeEye[0] = {{0.27, -0.14}};
            s.rawGazeEye[1] = {{0.17, -0.14}};
            s.sentGazeEye[0] = options.fakeIndependent ? Pair {{0.21, -0.10}} : s.sentGaze;
            s.sentGazeEye[1] = options.fakeIndependent ? Pair {{0.13, -0.10}} : s.sentGaze;
        }
        s.calibrationEnabled = true;
        s.relaxed = {{0.78, 0.82}};
        s.scales = {{1.026, 0.976}};
        s.learning = true;
        s.configPath = options.configPath;
        s.calibrationPath = "/home/steamos/.config/frameeyeosc/calibration";
        if (options.fakeConfigError) s.configError = "lid_closed must be below lid_open";
        s.effective = root;
        if (options.fakeLocked) {
            s.locked = {key::kPort, key::kRaw, key::kLidOpen, key::kIndependentEyes, key::kGazeOffsetY};
            s.effective.set(key::kPort, JsonValue::makeNumber(9123, true));
            s.effective.set(key::kRaw, JsonValue::makeBool(true));
            s.effective.set(key::kLidOpen, JsonValue::makeNumber(0.78));
            s.effective.set(key::kIndependentEyes, JsonValue::makeBool(true));
            s.effective.set(key::kGazeOffsetY, JsonValue::makeNumber(-0.05));
        }
    } else {
        s.readError = "no status file";
    }
    m.autostart.autostart = options.fakeAutostart;
    m.language = configLanguage(m.config);
    if (options.fakeWriteError) m.panelError = "rename failed: Read-only file system";
    m.update = fakeUpdate(options.fakeUpdate);
    if (options.fakeUpdate.empty()) m.update.state = frame_updater::UpdateState::UpToDate;
    return m;
}

void applyHit(const PanelHit& hit, PanelModel& model, EyePanel& panel, AutostartWorker& autostart,
              frame_updater::UpdateChecker* updater, gaze_fit::Session* fit, const VrOverlay* vr);
std::string targetLabel(const UiText& t, gaze_fit::TargetStyle style);

/**
 * --update-live: tick the update checker until its check or install is over (or time runs out).
 * @param updater the checker
 * @param enabled the update_check setting
 */
void settleUpdater(frame_updater::UpdateChecker& updater, bool enabled) {
    const double end = nowSeconds() + kUpdateSettleSec;
    // Give a just-started install a moment to write its state file
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    while (nowSeconds() < end) {
        updater.tick(enabled);
        const auto& u = updater.status();
        if (!u.checking && u.state != frame_updater::UpdateState::Installing) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

/**
 * --dump-png / --thumbnail-png: draw without OpenVR and save PNGs.
 * @param options the command line
 * @return exit code
 */
int runDumpPng(const Options& options) {
    FontSet fonts;
    fonts.load(kFontPath, kBoldFontPath);
    if (!options.pngPath.empty()) {
        PanelModel model;
        if (options.fake) {
            model = fakeModel(options);
        } else {
            model.configPath = options.configPath;
            model.statusPath = options.statusPath;
            model.config = readConfigFile(options.configPath);
            model.status = readStatus(options.statusPath, unixNow());
            model.autostart.autostart = readAutostart();
            model.language = configLanguage(model.config);
        }
        if (!options.language.empty()) parseLanguage(options.language, model.language);
        EyePanel panel(fonts);
        panel.setTab(options.tab);
        panel.setFitDetails(options.fitDetails);
        panel.setFitDetailsPage(options.fitDetailsPage);
        if (options.previewQuit) panel.armQuitForPreview();
        if (options.previewReset) panel.armResetForPreview();
        if (!options.fakePrompt.empty()) panel.showPrompt(options.fakePrompt);
        if (options.previewUpdatePrompt) panel.showUpdatePrompt(model.update.latest);
        std::unique_ptr<frame_updater::UpdateChecker> updater;
        const bool updateCheck = model.config.flag(key::kUpdateCheck);
        if (options.updateLive) {
            updater = std::make_unique<frame_updater::UpdateChecker>(updaterConfig());
            updater->tick(updateCheck);
            settleUpdater(*updater, updateCheck);
            model.update = updater->status();
        }
        // Presses as the laser pointer would make them (hit areas and config writes, without a headset)
        AutostartWorker idleAutostart;  // never started: autostart presses are only logged
        for (const auto& click : options.clicks) {
            panel.render(model);
            const PanelHit hit = panel.pointerDown(click.first, click.second, nowSeconds());
            std::printf("click %.0f,%.0f -> action %d key %s arg %d\n", click.first, click.second,
                        static_cast<int>(hit.action), hit.key != nullptr ? hit.key : "-", hit.arg);
            if (hit.action != PanelAction::Quit) {
                applyHit(hit, model, panel, idleAutostart, updater.get(), nullptr, nullptr);
            }
            panel.pointerUp();
            if (updater) {
                settleUpdater(*updater, model.config.flag(key::kUpdateCheck));
                model.update = updater->status();
            }
        }
        if (updater) {
            const auto& u = model.update;
            std::printf("update: state %d, checking %d, current %s, latest %s, installable %d, step %s, version %s, "
                        "error %s, checked_at %lld\n",
                        static_cast<int>(u.state), u.checking, u.current.c_str(), u.latest.c_str(), u.installable,
                        u.step.c_str(), u.version.c_str(), u.error.c_str(), u.checkedAt);
        }
        panel.pointerLeave();
        panel.render(model);
        if (!panel.writePng(options.pngPath)) {
            std::fprintf(stderr, "Could not write the PNG: %s\n", options.pngPath.c_str());
            return 1;
        }
        std::printf("Wrote %s (%dx%d)\n", options.pngPath.c_str(), panel.width(), panel.height());
    }
    if (!options.dotPngPath.empty()) {
        const DotKind kind = options.dotKind == "left"    ? DotKind::Left
                             : options.dotKind == "right" ? DotKind::Right
                                                          : DotKind::Both;
        std::vector<uint8_t> rgba;
        renderGazeDot(kind, rgba, options.dotPngPath);
        std::printf("Wrote %s (%dx%d)\n", options.dotPngPath.c_str(), kDotImageSize, kDotImageSize);
    }
    if (!options.targetPngPath.empty()) {
        Language language = Language::Ja;
        if (!options.language.empty()) parseLanguage(options.language, language);
        const UiText& t = uiText(language);
        using gaze_fit::TargetStyle;
        const std::string& name = options.targetStyle;
        const TargetStyle style = name == "close"  ? TargetStyle::CloseEyes
                                  : name == "keep" ? TargetStyle::KeepClosed
                                  : name == "open" ? TargetStyle::OpenEyes
                                                   : TargetStyle::Dot;
        const std::string label = targetLabel(t, style);
        std::vector<uint8_t> rgba;
        renderTarget(fonts, style, label, options.targetSeconds, options.targetProgress, rgba, options.targetPngPath);
        std::printf("Wrote %s (%dx%d)\n", options.targetPngPath.c_str(), kTargetImageSize, kTargetImageSize);
        if (options.targetBench > 0) {
            // What one frame of the target costs to draw (the VR loop draws one per display frame while it is up)
            double start = nowSeconds();
            for (int i = 0; i < options.targetBench; ++i) {
                renderTarget(fonts, style, label, 3, 1.0 - static_cast<double>(i) / options.targetBench, rgba);
            }
            const double each = (nowSeconds() - start) / options.targetBench;
            std::printf("target: %.3f ms per frame over %d frames (%.0f frames/s possible)\n", each * 1000,
                        options.targetBench, 1.0 / each);
            // As the VR loop draws it: one gaze point's ring running down over kPointSec at 90 frames/s, paced to
            // frame deadlines (as WaitFrameSync would), drawing only when the picture changes
            TargetPainter painter;
            const double frame = 1.0 / 90;
            const int frames = static_cast<int>(std::lround(gaze_fit::kPointSec / frame));
            int drawn = 0;
            double busy = 0.0;
            start = nowSeconds();
            for (int i = 0; i < frames; ++i) {
                const double workStart = nowSeconds();
                const double left = gaze_fit::kPointSec - i * frame;
                const int seconds = left <= gaze_fit::kCaptureSec ? static_cast<int>(std::ceil(left - 1e-9)) : 0;
                if (painter.paint(fonts, style, label, seconds, left / gaze_fit::kPointSec)) ++drawn;
                busy += nowSeconds() - workStart;
                std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::duration_cast<
                    std::chrono::steady_clock::duration>(std::chrono::duration<double>(start + (i + 1) * frame))));
            }
            const double took = nowSeconds() - start;
            std::printf("paced: %d frames in %.3f s (%.1f frames/s), %d pictures drawn, %.3f ms busy per frame\n",
                        frames, took, frames / took, drawn, busy / frames * 1000);
        }
    }
    if (!options.thumbnailPngPath.empty()) {
        std::vector<uint8_t> rgba;
        renderThumbnail(fonts, options.thumbnailSize, rgba, options.thumbnailPngPath);
        std::printf("Wrote %s (%dx%d)\n", options.thumbnailPngPath.c_str(), options.thumbnailSize,
                    options.thumbnailSize);
    }
    return 0;
}

/**
 * --print: what the panel sees, for checking over SSH.
 * @param options the command line
 * @return exit code (1 if the config is broken)
 */
int runPrint(const Options& options) {
    const ConfigFile config = readConfigFile(options.configPath);
    std::printf("config: %s (%s)\n", options.configPath.c_str(),
                !config.exists ? "missing, all defaults" : (config.error.empty() ? "ok" : config.error.c_str()));
    for (const SettingSpec& spec : settingSpecs()) {
        const JsonValue value = config.value(spec.key);
        JsonValue wrapper;
        wrapper.type = JsonValue::Type::Object;
        wrapper.set(spec.key, value);
        std::string line = writeJson(wrapper);
        // Keep only the "key": value line
        const size_t start = line.find('"');
        const size_t end = line.find('\n', start);
        std::printf("  %s\n", line.substr(start, end - start).c_str());
    }
    const EyeStatus status = readStatus(options.statusPath, unixNow());
    std::printf("status: %s (%s)\n", options.statusPath.c_str(),
                !status.present ? status.readError.c_str() : (status.running ? "running" : "stale or pid gone"));
    if (status.present) {
        std::printf("  pid %d, age %.2f s, sending %s, output %s, target %s (%s), rate %.1f, tracking %s\n", status.pid,
                    unixNow() - status.time, status.sending ? "yes" : "no", status.output.c_str(),
                    status.target.empty() ? "null" : status.target.c_str(), status.targetMode.c_str(), status.rate,
                    status.tracking ? "yes" : "no");
        std::printf("  raw openness_scaled %.3f %.3f, gaze %.3f %.3f; sent lids %.3f %.3f, gaze %.3f %.3f\n",
                    status.opennessScaled.v[0], status.opennessScaled.v[1], status.gaze.v[0], status.gaze.v[1],
                    status.lids.v[0], status.lids.v[1], status.sentGaze.v[0], status.sentGaze.v[1]);
        std::printf("  calibration enabled %s, relaxed %.3f %.3f, scales %.3f %.3f, learning %s\n",
                    status.calibrationEnabled ? "yes" : "no", status.relaxed.v[0], status.relaxed.v[1],
                    status.scales.v[0], status.scales.v[1], status.learning ? "yes" : "no");
        std::string locked;
        for (const auto& name : status.locked) locked += " " + name;
        std::printf("  locked:%s\n  config_error: %s\n  config_path: %s\n", locked.empty() ? " (none)" : locked.c_str(),
                    status.configError.empty() ? "null" : status.configError.c_str(), status.configPath.c_str());
    }
    const Autostart autostart = readAutostart();
    std::printf("autostart (%s): %s\n", kServiceName,
                autostart == Autostart::Enabled    ? "enabled"
                : autostart == Autostart::Disabled ? "disabled"
                : autostart == Autostart::Missing  ? "not installed"
                                                   : "unknown");
    return config.error.empty() ? 0 : 1;
}

/**
 * A short text of what the status column shows, rounded so that the panel is only redrawn when something
 * visible changes.
 * @param s the status
 * @return the text
 */
std::string statusSignature(const EyeStatus& s) {
    char text[1024];
    std::snprintf(text, sizeof(text),
                  "%d|%d|%d|%s|%s|%s|%.0f|%d|%d|%.2f %.2f %.2f %.2f|%.2f %.2f %.2f %.2f|%.2f %.2f|%.2f %.2f|%.2f %.2f|%d|%d|%d",
                  s.running, s.pid, s.sending, s.output.c_str(), s.targetMode.c_str(), s.target.c_str(), s.rate,
                  s.tracking, s.hasRaw, s.opennessScaled.v[0], s.opennessScaled.v[1], s.gaze.v[0], s.gaze.v[1],
                  s.lids.v[0], s.lids.v[1], s.sentGaze.v[0], s.sentGaze.v[1], s.relaxed.v[0], s.relaxed.v[1],
                  s.scales.v[0], s.scales.v[1], s.lidsVrcft.v[0], s.lidsVrcft.v[1], s.learning, s.calibrationEnabled,
                  static_cast<int>((s.time - s.started) / 60));
    std::string signature = text;
    char eyes[128];
    std::snprintf(eyes, sizeof(eyes), "|%.2f %.2f %.2f %.2f|%.2f %.2f %.2f %.2f", s.rawGazeEye[0].v[0],
                  s.rawGazeEye[0].v[1], s.rawGazeEye[1].v[0], s.rawGazeEye[1].v[1], s.sentGazeEye[0].v[0],
                  s.sentGazeEye[0].v[1], s.sentGazeEye[1].v[0], s.sentGazeEye[1].v[1]);
    signature += eyes;
    signature += "|" + s.configError + "|" + s.configPath + "|" + s.calibrationPath + "|";
    for (const auto& name : s.locked) signature += name + ",";
    if (s.effective.isObject()) signature += writeJson(s.effective);
    return signature;
}


/**
 * Write a change to config.json and keep the model in step; a failure is shown in the panel.
 * @param model the model (config and error are updated)
 * @param change edits the object
 * @return true if written
 */
bool writeConfig(PanelModel& model, const std::function<void(JsonValue&)>& change) {
    std::string error;
    const bool ok = updateConfigFile(model.configPath, change, error);
    if (ok) {
        model.panelError.clear();
        model.panelErrorBroken = false;
    } else {
        std::fprintf(stderr, "[config] %s\n", error.c_str());
        model.panelError = error;
        model.panelErrorBroken = !model.config.error.empty();
    }
    model.config = readConfigFile(model.configPath);
    model.language = configLanguage(model.config);
    return ok;
}

/**
 * Ask frameeyeosc for a gaze capture: gaze_capture = {"id": the next id, "target": target, "seconds": how long,
 * "skip": how much of the start to skip}.
 * @param model the model
 * @param target the target name
 * @param seconds how long it lasts
 * @param skip how much of its start is skipped
 * @param lastId the last id asked for (updated)
 * @return the new id, or 0 if the write failed
 */
long long writeCaptureRequest(PanelModel& model, const char* target, double seconds, double skip, long long& lastId) {
    long long id = 0;
    const std::string name = target;
    const long long last = lastId;
    const bool ok = writeConfig(model, [&id, name, last, seconds, skip](JsonValue& root) {
        // Always a new id, also after a restart of the panel or a hand-edited file
        const JsonValue* old = root.get(key::kGazeCapture);
        const JsonValue* oldId = old != nullptr && old->isObject() ? old->get("id") : nullptr;
        const long long written = oldId != nullptr && oldId->isNumber() ? static_cast<long long>(oldId->number) : 0;
        id = std::max(written, last) + 1;
        JsonValue request;
        request.type = JsonValue::Type::Object;
        request.set("id", JsonValue::makeNumber(static_cast<double>(id), true));
        request.set("target", JsonValue::makeString(name));
        request.set("seconds", JsonValue::makeNumber(seconds));
        request.set("skip", JsonValue::makeNumber(skip));
        root.set(key::kGazeCapture, request);
    });
    if (!ok) return 0;
    lastId = id;
    std::fprintf(stderr, "[fit] asked for gaze capture %lld (%s, %.1f s)\n", id, target, seconds);
    return id;
}

/**
 * Write an eye fit's result: the gaze zero point; for the whole fit also the gains and each eye's lid readings.
 * @param model the model
 * @param values the result
 * @param mode the mode
 * @return true if written
 */
bool writeFitValues(PanelModel& model, const gaze_fit::Values& values, gaze_fit::Mode mode) {
    const bool full = mode == gaze_fit::Mode::Full;
    std::fprintf(stderr, "[fit] done: offset %+.3f %+.3f, gains %.2f %.2f %.2f%s\n", values.offsetX, values.offsetY,
                 values.gainX, values.gainUp, values.gainDown, full ? "" : " (the rest unchanged)");
    if (full && values.hasLids) {
        for (int eye = 0; eye < 2; ++eye) {
            std::fprintf(stderr, "[fit] %s eyelid: closed %.3f, up %.3f, ahead %.3f, down %.3f\n",
                         eye == 0 ? "left" : "right", values.lidClosed[eye], values.lidUp[eye], values.lidOpen[eye],
                         values.lidDown[eye]);
        }
    }
    if (values.hasEyeX) {
        std::fprintf(stderr, "[fit] each eye sideways: left %+.3f x%.2f, right %+.3f x%.2f\n", values.eyeOffsetX[0],
                     values.eyeGainX[0], values.eyeOffsetX[1], values.eyeGainX[1]);
    }
    return writeConfig(model, [values, full](JsonValue& root) {
        root.set(key::kGazeOffsetX, JsonValue::makeNumber(values.offsetX));
        root.set(key::kGazeOffsetY, JsonValue::makeNumber(values.offsetY));
        // Each eye's own sideways values: re-centering moves their zero points; a full fit sets them, or clears
        // them when this frameeyeosc could not measure each eye
        const char* eyeOffsets[2] = {key::kGazeOffsetXLeft, key::kGazeOffsetXRight};
        const char* eyeGains[2] = {key::kGazeGainXLeft, key::kGazeGainXRight};
        for (int eye = 0; eye < 2; ++eye) {
            if (values.hasEyeX) {
                root.set(eyeOffsets[eye], JsonValue::makeNumber(values.eyeOffsetX[eye]));
                root.set(eyeGains[eye], JsonValue::makeNumber(values.eyeGainX[eye]));
            } else if (full) {
                root.set(eyeOffsets[eye], JsonValue::makeNull());
                root.set(eyeGains[eye], JsonValue::makeNull());
            }
        }
        if (!full) return;
        root.set(key::kGazeGainX, JsonValue::makeNumber(values.gainX));
        root.set(key::kGazeGainUp, JsonValue::makeNumber(values.gainUp));
        root.set(key::kGazeGainDown, JsonValue::makeNumber(values.gainDown));
        if (!values.hasLids) return;
        for (int eye = 0; eye < 2; ++eye) {
            const double readings[4] = {values.lidClosed[eye], values.lidUp[eye], values.lidOpen[eye],
                                        values.lidDown[eye]};
            for (int i = 0; i < 4; ++i) root.set(kLidFitKeys[eye][i], JsonValue::makeNumber(readings[i]));
        }
    });
}

/**
 * The words on the target for the eyes-shut step.
 * @param t texts
 * @param style the target's look
 * @return the words (empty for the dot)
 */
std::string targetLabel(const UiText& t, gaze_fit::TargetStyle style) {
    switch (style) {
        case gaze_fit::TargetStyle::Dot: return "";
        case gaze_fit::TargetStyle::CloseEyes: return t.targetClose;
        case gaze_fit::TargetStyle::KeepClosed: return t.targetKeepClosed;
        case gaze_fit::TargetStyle::OpenEyes: return t.targetOpen;
    }
    return "";
}

/**
 * Carry out a button press: re-read config.json, change keys, write it back; or ask the autostart worker.
 * @param hit the button
 * @param model the model (config and error are updated)
 * @param panel the panel (to open the recommendation prompt)
 * @param autostart the autostart worker
 * @param updater the update checker (null in --dump-png without --update-live)
 * @param fit the eye fit session (null in --dump-png)
 * @param vr the connection to SteamVR, for the IPD (null in --dump-png)
 */
void applyHit(const PanelHit& hit, PanelModel& model, EyePanel& panel, AutostartWorker& autostart,
              frame_updater::UpdateChecker* updater, gaze_fit::Session* fit, const VrOverlay* vr) {
    const SettingsView view(model);
    std::function<void(JsonValue&)> change;
    std::string openPrompt;
    bool reset = false;
    switch (hit.action) {
        case PanelAction::None:
        case PanelAction::Tab:
        case PanelAction::Quit: return;
        case PanelAction::PromptNo: std::fprintf(stderr, "[action] recommended settings: no\n"); return;
        case PanelAction::AutostartOn:
        case PanelAction::AutostartOff:
            std::fprintf(stderr, "[action] start with SteamVR: %s\n", hit.action == PanelAction::AutostartOn ? "on" : "off");
            autostart.request(hit.action == PanelAction::AutostartOn);
            return;
        case PanelAction::SetBool: {
            const std::string name = hit.key;
            const bool value = hit.arg != 0;
            change = [name, value](JsonValue& root) { root.set(name, JsonValue::makeBool(value)); };
            break;
        }
        case PanelAction::Step: {
            const SettingSpec* spec = findSetting(hit.key);
            if (spec == nullptr) return;
            const std::string name = hit.key;
            double current = view.number(name);
            if (name == key::kPort && !std::isfinite(current)) current = view.port();
            if ((name == key::kLidScaleLeft || name == key::kLidScaleRight) && !std::isfinite(current)) {
                const double scale = model.status.scales.v[name == key::kLidScaleLeft ? 0 : 1];
                current = std::isfinite(scale) ? scale : 1.0;
            }
            double low = -1e9;
            double high = 1e9;
            lidMarkBounds(name, view, low, high);
            const double next = stepValue(*spec, current, hit.arg, low, high);
            if (std::fabs(next - current) < 1e-12) return;
            const bool integer = spec->type == SettingType::Integer || spec->type == SettingType::NullableInteger;
            change = [name, next, integer](JsonValue& root) { root.set(name, JsonValue::makeNumber(next, integer)); };
            break;
        }
        case PanelAction::HostAuto:
            change = [](JsonValue& root) { root.set(key::kHost, JsonValue::makeString("auto")); };
            break;
        case PanelAction::FixHost: {
            const std::string host = hostOfTarget(model.status.target);
            if (host.empty()) return;
            change = [host](JsonValue& root) { root.set(key::kHost, JsonValue::makeString(host)); };
            break;
        }
        case PanelAction::PortDefault:
            change = [](JsonValue& root) { root.set(key::kPort, JsonValue::makeNull()); };
            break;
        case PanelAction::SetOutput: {
            const std::string output = hit.arg == 1 ? kOutputEtvr : kOutputVrchat;
            if (view.text(key::kOutput) == output) return;
            // A new output type starts from its own default port
            change = [output](JsonValue& root) {
                root.set(key::kOutput, JsonValue::makeString(output));
                root.set(key::kPort, JsonValue::makeNull());
            };
            openPrompt = output;
            break;
        }
        case PanelAction::Preset: {
            if (hit.arg < 0 || hit.arg > 2) return;
            const GazePreset preset = gazePresets()[hit.arg];
            change = [preset](JsonValue& root) {
                root.set(key::kGazeMinCutoff, JsonValue::makeNumber(preset.minCutoff));
                root.set(key::kGazeBeta, JsonValue::makeNumber(preset.beta));
                root.set(key::kGazeDCutoff, JsonValue::makeNumber(preset.dCutoff));
            };
            break;
        }
        case PanelAction::NumberOn: {
            const SettingSpec* spec = findSetting(hit.key);
            if (spec == nullptr) return;
            const std::string name = hit.key;
            const double value = spec->defaultNumber > 0 ? spec->defaultNumber : spec->onNumber;
            change = [name, value](JsonValue& root) { root.set(name, JsonValue::makeNumber(value)); };
            break;
        }
        case PanelAction::NumberOff: {
            const std::string name = hit.key;
            change = [name](JsonValue& root) { root.set(name, JsonValue::makeNumber(0.0)); };
            break;
        }
        case PanelAction::CalibrationReset:
            change = [](JsonValue& root) {
                const JsonValue* current = root.get(key::kCalibrationReset);
                const double count = current != nullptr && current->isNumber() ? current->number : 0.0;
                root.set(key::kCalibrationReset, JsonValue::makeNumber(std::floor(count) + 1, true));
            };
            break;
        case PanelAction::ScaleAuto: {
            const bool left = !view.locked(key::kLidScaleLeft);
            const bool right = !view.locked(key::kLidScaleRight);
            change = [left, right](JsonValue& root) {
                if (left) root.set(key::kLidScaleLeft, JsonValue::makeNull());
                if (right) root.set(key::kLidScaleRight, JsonValue::makeNull());
            };
            break;
        }
        case PanelAction::ScaleFixed: {
            // Start from the scales in use, so the eyelids don't jump
            const SettingSpec* spec = findSetting(key::kLidScaleLeft);
            double values[2];
            for (int eye = 0; eye < 2; ++eye) {
                const double scale = model.status.running ? model.status.scales.v[eye] : NAN;
                values[eye] = roundToDecimals(*spec, std::isfinite(scale) ? scale : 1.0);
            }
            const bool left = !view.locked(key::kLidScaleLeft);
            const bool right = !view.locked(key::kLidScaleRight);
            change = [left, right, values](JsonValue& root) {
                if (left) root.set(key::kLidScaleLeft, JsonValue::makeNumber(values[0]));
                if (right) root.set(key::kLidScaleRight, JsonValue::makeNumber(values[1]));
            };
            break;
        }
        case PanelAction::PrefixFt:
            change = [](JsonValue& root) { root.set(key::kPrefix, JsonValue::makeString("/FT")); };
            break;
        case PanelAction::PrefixNone:
            change = [](JsonValue& root) { root.set(key::kPrefix, JsonValue::makeString("")); };
            break;
        case PanelAction::Language: {
            const std::string code = hit.arg == 1 ? "en" : "ja";
            change = [code](JsonValue& root) { root.set(key::kLanguage, JsonValue::makeString(code)); };
            break;
        }
        case PanelAction::PromptYes: {
            const std::vector<SettingChange> changes =
                recommendedSettings(hit.arg == 1 ? kOutputEtvr : kOutputVrchat, view);
            change = [changes](JsonValue& root) {
                for (const SettingChange& item : changes) root.set(item.key, item.value);
            };
            break;
        }
        case PanelAction::ResetAll: reset = true; break;
        case PanelAction::UpdateCheck:
            std::fprintf(stderr, "[update] check now\n");
            if (updater != nullptr) updater->checkNow();
            return;
        case PanelAction::UpdateInstall: {
            // "Update", or "Try again" after a failed install
            const frame_updater::UpdateStatus& u = model.update;
            if (u.state == frame_updater::UpdateState::Available && u.installable) {
                panel.showUpdatePrompt(u.latest);
            } else if (u.state == frame_updater::UpdateState::InstallFailed) {
                panel.showUpdatePrompt(u.version.empty() ? u.latest : u.version);
            }
            return;
        }
        case PanelAction::UpdateConfirm:
            std::fprintf(stderr, "[update] installing %s (log: %s)\n", model.update.latest.c_str(),
                         updater != nullptr ? updater->logPath().c_str() : "-");
            if (updater != nullptr && !updater->install()) std::fprintf(stderr, "[update] could not start it\n");
            return;
        case PanelAction::UpdateCancel: std::fprintf(stderr, "[update] install: no\n"); return;
        case PanelAction::UpdateDismiss:
            if (updater != nullptr) updater->dismiss();
            return;
        case PanelAction::FitStart:
        case PanelAction::FitCenter: {
            const bool full = hit.action == PanelAction::FitStart;
            std::fprintf(stderr, "[fit] %s: waiting for the dashboard to close\n", full ? "eye fit" : "re-center");
            if (fit != nullptr) {
                const double ipd = vr != nullptr ? vr->userIpdMeters() : gaze_fit::kDefaultIpdM;
                std::fprintf(stderr, "[fit] IPD %.1f mm\n", ipd * 1000);
                fit->start(full ? gaze_fit::Mode::Full : gaze_fit::Mode::Center, fitInConfig(view).values, nowSeconds(),
                           ipd);
            }
            return;
        }
        case PanelAction::FitDetails:
        case PanelAction::FitDetailsPage: return;
        case PanelAction::FitStop:
            std::fprintf(stderr, "[fit] stopped\n");
            if (fit != nullptr) fit->cancel();
            return;
        case PanelAction::FitReset: {
            std::vector<std::string> names = {key::kGazeOffsetX,     key::kGazeOffsetY,      key::kGazeGainX,
                                              key::kGazeGainUp,      key::kGazeGainDown,     key::kGazeOffsetXLeft,
                                              key::kGazeOffsetXRight, key::kGazeGainXLeft,  key::kGazeGainXRight};
            for (const auto& eye : kLidFitKeys) names.insert(names.end(), std::begin(eye), std::end(eye));
            std::vector<std::string> unlocked;
            for (const std::string& name : names) {
                if (!view.locked(name)) unlocked.push_back(name);
            }
            change = [unlocked](JsonValue& root) {
                for (const std::string& name : unlocked) root.set(name, defaultValue(*findSetting(name)));
            };
            break;
        }
    }

    std::string error;
    bool ok = false;
    if (reset) {
        std::fprintf(stderr, "[action] reset all settings\n");
        ok = resetConfigFile(model.configPath, error);
    } else {
        std::fprintf(stderr, "[action] %d %s %d\n", static_cast<int>(hit.action), hit.key != nullptr ? hit.key : "-",
                     hit.arg);
        ok = updateConfigFile(model.configPath, change, error);
    }
    if (ok) {
        model.panelError.clear();
        model.panelErrorBroken = false;
        if (!openPrompt.empty()) panel.showPrompt(openPrompt);
    } else {
        std::fprintf(stderr, "[config] %s\n", error.c_str());
        model.panelError = error;
        model.panelErrorBroken = !model.config.error.empty();
    }
    model.config = readConfigFile(model.configPath);
    model.language = configLanguage(model.config);
}

/**
 * Where the eye fit's sound files go: a "sounds" folder next to the status file.
 * @param statusPath the status file
 * @return the folder
 */
std::string soundsDir(const std::string& statusPath) {
    const size_t slash = statusPath.find_last_of('/');
    return (slash == std::string::npos ? std::string(".") : statusPath.substr(0, slash)) + "/sounds";
}

/**
 * --play-sound: play one cue and wait for it (for listening while tuning).
 * @param options the command line
 * @return exit code
 */
int runPlaySound(const Options& options) {
    sounds::Cue cue = sounds::Cue::Pop;
    sounds::parse(options.playSound, cue);
    sounds::Player player;
    if (!player.init(soundsDir(options.statusPath)) || player.program().empty()) return 1;
    player.play(cue);
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        player.reap();
    }
    return 0;
}

/**
 * Where the single-instance lock lives ($XDG_RUNTIME_DIR, or /run/user/<uid>).
 * @return the path
 */
std::string lockFilePath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && runtime[0] != '\0') return std::string(runtime) + "/frameeyeosc-panel.lock";
    const std::string userRuntime = "/run/user/" + std::to_string(::getuid());
    if (::access(userRuntime.c_str(), W_OK) == 0) return userRuntime + "/frameeyeosc-panel.lock";
    return "/tmp/frameeyeosc-panel-" + std::to_string(::getuid()) + ".lock";
}

/**
 * Take the single-instance lock and write our PID into it (the file stays open; the OS drops the lock on exit).
 * @param lockFd the open lock file if taken
 * @param holderPid the resident instance's PID if not taken (0 if unreadable)
 * @return true if taken (or locking is unavailable and we should just start)
 */
bool acquireInstanceLock(int& lockFd, pid_t& holderPid) {
    lockFd = -1;
    holderPid = 0;
    const std::string path = lockFilePath();
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        std::fprintf(stderr, "[start] can't open the lock file %s; starting without the single-instance check\n",
                     path.c_str());
        return true;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
        const std::string pid = std::to_string(::getpid()) + "\n";
        if (::ftruncate(fd, 0) != 0 || ::pwrite(fd, pid.data(), pid.size(), 0) < 0) {
            std::fprintf(stderr, "[start] could not write the PID into the lock file\n");
        }
        lockFd = fd;
        return true;
    }
    // The resident instance may have just taken the lock and not written its PID yet
    for (int attempt = 0; attempt < 10 && holderPid <= 0; ++attempt) {
        char buffer[32] = {};
        const ssize_t n = ::pread(fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) holderPid = static_cast<pid_t>(std::atol(buffer));
        if (holderPid <= 0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ::close(fd);
    return false;
}

/**
 * Whether this process was started by systemd as frameeyeosc-panel.service. INVOCATION_ID alone is not enough:
 * the Frame's Steam client runs as steam.service and passes INVOCATION_ID on to what it launches, so the
 * cgroup is checked too.
 * @return true if started by our own unit
 */
bool startedByOwnService() {
    const char* invocation = std::getenv("INVOCATION_ID");
    if (invocation == nullptr || invocation[0] == '\0') return false;
    const std::string suffix = "/frameeyeosc-panel.service";
    std::ifstream cgroup("/proc/self/cgroup");
    std::string line;
    while (std::getline(cgroup, line)) {
        if (line.size() >= suffix.size() && line.compare(line.size() - suffix.size(), std::string::npos, suffix) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Log what the update checker found, once per change (not the "checking" flag alone).
 * @param u the status
 */
void logUpdate(const frame_updater::UpdateStatus& u) {
    static std::string last;
    std::string line;
    switch (u.state) {
        case frame_updater::UpdateState::Unknown: return;
        case frame_updater::UpdateState::UpToDate: line = "up to date (" + u.current + ")"; break;
        case frame_updater::UpdateState::Available:
            line = "new release " + u.latest + (u.installable ? "" : " (not installable: " + u.reason + ")");
            break;
        case frame_updater::UpdateState::Installing: line = "installing " + u.version + ": " + u.step; break;
        case frame_updater::UpdateState::Installed: line = "installed " + u.version + "; reopen to use it"; break;
        case frame_updater::UpdateState::CheckFailed: line = "check failed (" + u.error + "): " + u.message; break;
        case frame_updater::UpdateState::InstallFailed: line = "install failed (" + u.error + "): " + u.message; break;
    }
    if (line == last) return;
    last = line;
    std::fprintf(stderr, "[update] %s\n", line.c_str());
}

/**
 * Stay resident as a dashboard overlay. Waits for SteamVR, exits quietly when it quits.
 * If another instance runs, asks it to open its panel and exits (no VR_Init).
 * @param options the command line
 * @return exit code
 */
int runOverlay(const Options& options) {
    int lockFd = -1;
    pid_t holderPid = 0;
    if (!acquireInstanceLock(lockFd, holderPid)) {
        // Started by systemd (Restart=always) while another instance runs: exit without signalling, with the
        // code systemd does not restart on, so the panel does not pop open every 5 s
        if (startedByOwnService()) {
            std::fprintf(stderr, "[start] already running (PID %d); started by the service, so exiting\n",
                         static_cast<int>(holderPid));
            return kExitCodeUserQuit;
        }
        if (holderPid > 0 && ::kill(holderPid, SIGUSR1) == 0) {
            std::fprintf(stderr, "[start] already running (PID %d); opening its panel\n", static_cast<int>(holderPid));
            return 0;
        }
        std::fprintf(stderr, "[start] seems to be running already, but could not signal it (PID %d)\n",
                     static_cast<int>(holderPid));
        return 1;
    }

    PanelModel model;
    model.configPath = options.configPath;
    model.statusPath = options.statusPath;
    model.config = readConfigFile(model.configPath);
    model.language = configLanguage(model.config);
    std::fprintf(stderr, "[start] config %s, status %s\n", model.configPath.c_str(), model.statusPath.c_str());
    if (!model.config.error.empty()) std::fprintf(stderr, "[config] broken: %s\n", model.config.error.c_str());
    FontSet fonts;
    fonts.load(kFontPath, kBoldFontPath);
    EyePanel panel(fonts);
    AutostartWorker autostart;
    autostart.start();
    frame_updater::UpdateChecker updater(updaterConfig());
    std::fprintf(stderr, "[update] frameeyeosc-panel %s, checking %s\n", FRAMEEYEOSC_VERSION,
                 model.config.flag(key::kUpdateCheck) ? "on" : "off");
    VrOverlay vr;

    // Wait for SteamVR
    std::string lastMessage;
    while (!gStopRequested) {
        std::string message;
        const VrOverlay::ConnectResult result = vr.connect(panel.width(), panel.height(), message);
        if (result == VrOverlay::ConnectResult::Ok) break;
        if (message != lastMessage) {
            if (result == VrOverlay::ConnectResult::NotRunning) {
                std::fprintf(stderr, "[VR] SteamVR is not running; retrying every 3 s\n");
            } else {
                std::fprintf(stderr, "[VR] connect failed: %s (retrying in 3 s)\n", message.c_str());
            }
            lastMessage = message;
        }
        sleepInterruptible(3.0);
        if (gShowRequested) {
            gShowRequested = 0;
            std::fprintf(stderr, "[start] asked to open the panel, but not connected to SteamVR\n");
        }
    }
    if (gStopRequested) {
        autostart.stop();
        if (lockFd >= 0) ::close(lockFd);
        return 0;
    }
    std::fprintf(stderr, "[VR] connected to SteamVR\n");
    {
        std::vector<uint8_t> thumbnail;
        renderThumbnail(fonts, kThumbnailSize, thumbnail);
        vr.submitThumbnail(thumbnail.data(), kThumbnailSize);
        // Give the panel a first image right away (no empty frame the first time it is chosen)
        model.status = readStatus(model.statusPath, unixNow());
        panel.render(model);
        vr.submitPanel(panel.toRgba().data());
        vr.logOverlayState("after connecting");
    }

    gaze_fit::Session fit;
    long long lastCaptureId = 0;
    // The eye fit's sound cues: files written once now, played while the fit runs (if fit_sounds is on)
    sounds::Player player;
    player.init(soundsDir(model.statusPath));
    sounds::FitCues cues;
    TargetPainter targetPainter;
    bool targetUp = false;          // shown this loop: then the loop waits for the compositor's next frame
    int targetFrames = 0;           // frames since the target came up, to log the rate...
    int targetDraws = 0;            // ...and how many of them drew a new picture
    double targetShownAt = 0.0;
    double loggedYaw = NAN;         // where the target was last logged as settled
    double loggedPitch = NAN;
    std::string lastFitState;
    // The debug gaze dots: their socket, the three dot images (drawn once), which image each overlay has
    gaze_dots::Receiver dots;
    std::vector<uint8_t> dotImages[3];
    for (int kind = 0; kind < 3; ++kind) renderGazeDot(static_cast<DotKind>(kind), dotImages[kind]);
    int dotImageShown[2] = {-1, -1};
    bool dotsVisible[2] = {false, false};
    bool dotsOpenFailed = false;
    double lastDotAt = 0.0;
    double dotIpd = gaze_fit::kDefaultIpdM;
    double dotDistance = 0.0;       // how far along the rays the dots are (gaze_debug_dots_distance_m), 0 until set
    uint64_t drawnAutostart = autostart.snapshot(model.autostart);
    uint64_t drawnUpdate = 0;
    std::string lastSignature;
    std::string lastStamp = configStamp(model.configPath);
    bool lastRunning = false;
    double nextStatusRead = 0.0;
    bool dirty = true;
    bool wasVisible = false;
    bool firstSubmit = true;
    bool userQuit = false;
    while (!gStopRequested && !userQuit) {
        if (gShowRequested) {
            gShowRequested = 0;
            std::fprintf(stderr, "[start] a second launch asked to open the panel\n");
            vr.showPanel();
        }
        const VrEvents events = vr.pollEvents();
        // SteamVR quitting exits with 0; the dashboard's "close" is the user quitting, like "Quit" (code 3)
        if (events.quit) break;
        if (events.closeRequested) {
            std::fprintf(stderr, "[VR] quitting from the dashboard's \"close\"\n");
            userQuit = true;
            break;
        }
        if (!vr.steamVrAlive()) {
            std::fprintf(stderr, "[VR] vrserver is gone; quitting\n");
            break;
        }

        // Files are only read while the panel is visible, or while a eye fit runs with the dashboard closed
        const bool visible = vr.panelVisible();
        autostart.setActive(visible);
        const bool fitting = fit.active();
        // (also while the debug dots are on, so their switch and distance apply without opening the dashboard)
        if ((visible || fitting || dots.isOpen()) && ((visible && !wasVisible) || nowSeconds() >= nextStatusRead)) {
            nextStatusRead = nowSeconds() + kStatusReadSec;
            model.status = readStatus(model.statusPath, unixNow());
            if (model.status.running != lastRunning) {
                if (model.status.running) {
                    std::fprintf(stderr, "[status] frameeyeosc is running (PID %d)\n", model.status.pid);
                } else {
                    std::fprintf(stderr, "[status] frameeyeosc is not running (%s)\n",
                                 model.status.present ? "stale status" : model.status.readError.c_str());
                }
                lastRunning = model.status.running;
            }
            const std::string signature = statusSignature(model.status);
            if (signature != lastSignature) {
                lastSignature = signature;
                dirty = true;
            }
            const std::string stamp = configStamp(model.configPath);
            if (stamp != lastStamp) {
                lastStamp = stamp;
                model.config = readConfigFile(model.configPath);
                model.language = configLanguage(model.config);
                dirty = true;
            }
        }

        for (const PointerInput& input : events.pointer) {
            switch (input.type) {
                case PointerInput::Type::Move: dirty |= panel.pointerMove(input.x, input.y); break;
                case PointerInput::Type::Down: {
                    const PanelHit hit = panel.pointerDown(input.x, input.y, nowSeconds());
                    if (hit.action == PanelAction::Quit) {
                        std::fprintf(stderr, "[VR] quitting from the panel's \"Quit\"\n");
                        userQuit = true;
                    } else {
                        applyHit(hit, model, panel, autostart, &updater, &fit, &vr);
                        lastStamp = configStamp(model.configPath);
                    }
                    dirty = true;
                    break;
                }
                case PointerInput::Type::Up: dirty |= panel.pointerUp(); break;
                case PointerInput::Type::Leave: dirty |= panel.pointerLeave(); break;
            }
        }
        if (userQuit) break;
        dirty |= panel.tick(nowSeconds());
        const uint64_t autostartVersion = autostart.snapshot(model.autostart);
        if (autostartVersion != drawnAutostart) {
            drawnAutostart = autostartVersion;
            dirty = true;
        }
        // Checks at start and then daily (frame-update.sh asks GitHub at most once a day), even while closed
        updater.tick(model.config.flag(key::kUpdateCheck));
        if (updater.revision() != drawnUpdate) {
            drawnUpdate = updater.revision();
            model.update = updater.status();
            logUpdate(model.update);
            dirty = true;
        }

        // The eye fit: requests and results go through config.json and status.json; the target is only shown
        // while the dashboard is closed
        {
            const gaze_fit::Actions actions = fit.tick(nowSeconds(), vr.dashboardVisible(), model.status);
            // Cues follow what the target shows (a dot arriving, the countdown, the eyes-shut ending)
            const bool soundsOn = model.config.flag(key::kFitSounds);
            for (sounds::Cue cue : cues.update(fit.view(), actions)) {
                if (soundsOn) player.play(cue);
            }
            player.reap();
            if (actions.writeCapture) {
                const long long id =
                    writeCaptureRequest(model, actions.target, actions.captureSec, actions.skipSec, lastCaptureId);
                if (id != 0) {
                    fit.captureSent(id, nowSeconds());
                } else {
                    fit.writeFailed();
                }
                lastStamp = configStamp(model.configPath);
            }
            if (actions.writeValues) {
                if (!writeFitValues(model, actions.values, fit.view().mode)) fit.writeFailed();
                lastStamp = configStamp(model.configPath);
            }
            targetUp = false;
            if (actions.showTarget) {
                // Every display frame, so the ring runs down and the dot glides smoothly; the picture is only drawn
                // and sent when it changed
                const std::string label = targetLabel(uiText(model.language), actions.style);
                const bool drawn =
                    targetPainter.paint(fonts, actions.style, label, actions.seconds, actions.progress);
                targetUp = vr.showTarget(actions.yawDeg, actions.pitchDeg,
                                         drawn ? targetPainter.rgba().data() : nullptr, kTargetImageSize);
                if (drawn && !targetUp) targetPainter.reset();
                if (targetFrames++ == 0) targetShownAt = nowSeconds();
                if (drawn) ++targetDraws;
                if (actions.arrived && (actions.yawDeg != loggedYaw || actions.pitchDeg != loggedPitch)) {
                    std::fprintf(stderr, "[VR] target at %.0f deg right, %.0f deg up\n", actions.yawDeg,
                                 actions.pitchDeg);
                    loggedYaw = actions.yawDeg;
                    loggedPitch = actions.pitchDeg;
                }
            } else {
                vr.hideTarget();
                if (targetFrames > 0) {
                    const double seconds = nowSeconds() - targetShownAt;
                    std::fprintf(stderr, "[fit] target up %.1f s: %d frames (%.0f per second), %d pictures drawn\n",
                                 seconds, targetFrames, targetFrames / std::max(seconds, 0.001), targetDraws);
                    targetFrames = 0;
                    targetDraws = 0;
                    loggedYaw = loggedPitch = NAN;
                    targetPainter.reset();
                }
            }
            const gaze_fit::View view = fit.view();
            char state[96];
            std::snprintf(state, sizeof(state), "phase %d, point %d/%d (%s), try %d, failure %d",
                          static_cast<int>(view.phase), view.index + 1, view.count,
                          gaze_fit::target(view.point).name, view.attempt, static_cast<int>(view.failure));
            if (state != lastFitState) {
                if (!lastFitState.empty() || view.phase != gaze_fit::Phase::Idle) std::fprintf(stderr, "[fit] %s\n", state);
                lastFitState = state;
                model.fit = view;
                dirty = true;
            }
        }

        // The debug gaze dots: while the switch is on (and no eye fit runs), move a dot to where the gaze frameeyeosc
        // sends points, as each sample arrives. Only the transform changes; each dot's image is sent once
        {
            const bool wanted = model.config.flag(key::kGazeDebugDots) && !fit.active();
            const auto hideDots = [&]() {
                for (int i = 0; i < 2; ++i) {
                    vr.hideDot(i);
                    dotsVisible[i] = false;
                }
            };
            // One distance, dashboard open or closed (near enough to show over it; nothing jumps)
            const double wantedDistance = gaze_dots::dotDistance(model.config.number(key::kGazeDebugDotsDistanceM));
            if (dots.isOpen() && wantedDistance != dotDistance) {
                std::fprintf(stderr, "[dots] at %.2f m, %.1f mm wide (plain overlays, no sort order or flags)\n",
                             wantedDistance, gaze_dots::dotWidth(wantedDistance) * 1000);
                dotDistance = wantedDistance;
            }
            if (wanted && !dots.isOpen() && !dotsOpenFailed) {
                const size_t slash = model.statusPath.find_last_of('/');
                const std::string dir = slash == std::string::npos ? "." : model.statusPath.substr(0, slash);
                std::string error;
                if (dots.open(dir + "/" + gaze_dots::kSocketName, error)) {
                    dotIpd = vr.userIpdMeters();
                    // Set the distance now, so a packet in this same pass isn't placed at 0 m with the default width
                    dotDistance = wantedDistance;
                    std::fprintf(stderr, "[dots] at %.2f m, %.1f mm wide (plain overlays, no sort order or flags)\n",
                                 wantedDistance, gaze_dots::dotWidth(wantedDistance) * 1000);
                    std::fprintf(stderr, "[dots] listening in %s (IPD %.1f mm)\n", dir.c_str(), dotIpd * 1000);
                } else {
                    std::fprintf(stderr, "[dots] %s\n", error.c_str());
                    dotsOpenFailed = true;
                }
            } else if (!wanted && (dots.isOpen() || dotsOpenFailed)) {
                if (dots.isOpen()) std::fprintf(stderr, "[dots] off\n");
                dotDistance = 0.0;
                dots.close();
                hideDots();
                dotsOpenFailed = false;
            }
            gaze_dots::Packet packet;
            if (dots.isOpen() && dots.poll(packet)) {
                lastDotAt = nowSeconds();
                const auto place = [&](int index, int eye, DotKind kind, float x, float y) {
                    const gaze_dots::Pose pose = gaze_dots::dotPose(x, y, eye, dotIpd, dotDistance);
                    const int image = static_cast<int>(kind);
                    dotsVisible[index] = vr.showDot(index, pose.position.x, pose.position.y, pose.position.z,
                                                    pose.yawDeg, pose.pitchDeg, dotImages[image].data(),
                                                    kDotImageSize, dotImageShown[index] != image,
                                                    gaze_dots::dotWidth(dotDistance));
                    if (dotsVisible[index]) dotImageShown[index] = image;
                };
                if (packet.independent) {
                    place(0, -1, DotKind::Left, packet.gaze[0], packet.gaze[1]);
                    place(1, 1, DotKind::Right, packet.gaze[2], packet.gaze[3]);
                } else {
                    place(0, 0, DotKind::Both, packet.gaze[4], packet.gaze[5]);
                    vr.hideDot(1);
                    dotsVisible[1] = false;
                }
            } else if ((dotsVisible[0] || dotsVisible[1]) && nowSeconds() - lastDotAt > gaze_dots::kStaleSec) {
                // No samples (frameeyeosc stopped, or the eyes aren't tracked): no stale dots
                hideDots();
            }
        }

        // Draw only while visible, and only when something changed
        if (visible && (dirty || !wasVisible)) {
            panel.render(model);
            vr.submitPanel(panel.toRgba().data());
            dirty = false;
            if (firstSubmit) {
                firstSubmit = false;
                vr.logOverlayState("after the first draw");
            }
        }
        wasVisible = visible;
        // Every display frame while the fit's target or the debug dots are up: with the target, paced by the
        // compositor itself (a fixed sleep plus the loop's work fell behind the display, and the ring stuttered)
        // (the dots only while packets arrive: with none for kStaleSec, the usual slow poll)
        const bool dotsLive = dots.isOpen() && nowSeconds() - lastDotAt <= gaze_dots::kStaleSec;
        const bool everyFrame = fit.active() || dotsLive;
        if (!(targetUp && vr.waitFrameSync(kFrameSyncTimeoutMs))) {
            sleepInterruptible(everyFrame ? kFitPollSec : (visible ? kPanelPollSec : kClosedPollSec));
        }
    }

    // The same shutdown for SIGTERM / SIGINT, SteamVR quitting, vrserver gone, "close" and "Quit"
    vr.shutdown();
    autostart.stop();
    std::fprintf(stderr, "[VR] done\n");
    if (lockFd >= 0) ::close(lockFd);
    return userQuit ? kExitCodeUserQuit : 0;
}

}  // namespace

/**
 * Entry point.
 * @param argc argument count
 * @param argv arguments
 * @return exit code
 */
int main(int argc, char** argv) {
    // Line-buffered stderr so journald gets each line at once
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    installSignalHandlers();

    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage();
        return 2;
    }
    switch (options.mode) {
        case Options::Mode::Help: printUsage(); return 0;
        case Options::Mode::Version: std::printf("frameeyeosc-panel %s\n", FRAMEEYEOSC_VERSION); return 0;
        case Options::Mode::Print: return runPrint(options);
        case Options::Mode::DumpPng: return runDumpPng(options);
        case Options::Mode::Probe: return VrOverlay::probe();
        case Options::Mode::SwitchAway: return VrOverlay::switchAway(options.switchAwaySec);
        case Options::Mode::ContrastReport: return printContrastReport();
        case Options::Mode::PlaySound: return runPlaySound(options);
        case Options::Mode::Overlay: break;
    }
    return runOverlay(options);
}
