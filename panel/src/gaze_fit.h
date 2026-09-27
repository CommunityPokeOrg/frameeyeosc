// Gaze calibration (the "Gaze fit" tab): where the targets are, how captured gaze averages become the gaze zero
// point and gains, and the step-by-step session the panel runs while the dashboard is closed. Nothing here talks to
// OpenVR or writes files, so it can be tested on its own (gaze_fit_test.cpp).
#pragma once

#include "status.h"

namespace gaze_fit {

/** Only the center point, or all five. */
enum class Mode { Center, FivePoint };

/** The targets, in the order the five-point fit shows them. */
enum class Point { Center, Up, Down, Left, Right };

/** A target as seen from the head. */
struct Target {
    Point point;
    const char* name;  ///< sent with the capture request and logged by frameeyeosc
    double yawDeg;     ///< degrees to the right
    double pitchDeg;   ///< degrees up
};

/** How far the side and the up / down targets are from straight ahead. */
constexpr double kSideDeg = 20.0;
constexpr double kUpDownDeg = 15.0;
/** The gaze angle that frameeyeosc sends as 1.0. */
constexpr double kFullScaleDeg = 45.0;
/** A capture is used when it has at least this many samples (of about 135 in the 1.5 s frameeyeosc averages)... */
constexpr int kMinSamples = 45;
/** ...and spreads no more than this (on the -1..1 scale; 0.06 is about 2.7°). */
constexpr double kMaxSpread = 0.06;
/** Tries per point before giving up. */
constexpr int kMaxAttempts = 3;
/** Seconds the target shows before the capture is asked for, so the eyes can find it. */
constexpr double kSettleSec = 1.0;
/** How long frameeyeosc captures (its first 0.5 s are skipped). */
constexpr double kCaptureSec = 2.0;
/** Seconds to wait for a capture's result: frameeyeosc reads config.json once a second, then gives up after 5 s. */
constexpr double kResultTimeoutSec = 8.0;
/** How long "close the dashboard to start" waits. */
constexpr double kDashboardWaitSec = 60.0;
/** A side / up / down point must move at least this share of its target angle, the right way. */
constexpr double kMinMoveFraction = 0.25;
/** The allowed zero points and gains (the same as frameeyeosc's). */
constexpr double kOffsetLimit = 0.5;
constexpr double kGainMin = 0.5;
constexpr double kGainMax = 2.0;

/**
 * A target.
 * @param point which one
 * @return where it is
 */
const Target& target(Point point);

/**
 * How many points a mode shows.
 * @param mode the mode
 * @return 1 or 5
 */
int pointCount(Mode mode);

/**
 * The point shown at a step.
 * @param mode the mode
 * @param index 0-based step
 * @return the point
 */
Point pointAt(Mode mode, int index);

/** One capture's result (the raw combined gaze, before the zero point and gains). */
struct Measured {
    double x = 0.0;
    double y = 0.0;
    double spread = 0.0;
    int samples = 0;
};

/**
 * Whether a capture is steady and long enough to use.
 * @param measured the capture
 * @return true if usable
 */
bool usable(const Measured& measured);

/** The five settings a fit writes. */
struct Values {
    double offsetX = 0.0;
    double offsetY = 0.0;
    double gainX = 1.0;
    double gainUp = 1.0;
    double gainDown = 1.0;
};

/**
 * The zero point from the center capture; the gains stay as they are.
 * @param center the center capture
 * @param current the settings now
 * @return the new settings (rounded, within range)
 */
Values fitCenter(const Measured& center, const Values& current);

/**
 * The zero point and the three gains from all five captures. Each gain makes the target angle come out as that
 * angle: gain = target / (point - center), with left and right averaged into one gain.
 * @param points the captures, indexed by Point
 * @param out the new settings (rounded, within range)
 * @param failed the first point that did not move far enough the right way
 * @return false if a point did not move far enough the right way
 */
bool fitFive(const Measured points[5], Values& out, Point& failed);

/** Where a session is. */
enum class Phase {
    Idle,       ///< nothing going on (maybe showing the last result)
    Waiting,    ///< waiting for the dashboard to close
    Settling,   ///< a target is shown; the capture is asked for after kSettleSec
    Capturing,  ///< waiting for frameeyeosc's result
    Done,       ///< the new settings were written
    Failed,     ///< stopped; see Failure
};

/** Why a session stopped. */
enum class Failure {
    None,
    Cancelled,     ///< the dashboard was opened again (or "Stop" pressed)
    WaitTimedOut,  ///< the dashboard was not closed within kDashboardWaitSec
    NotRunning,    ///< frameeyeosc is not running
    NoResult,      ///< frameeyeosc did not answer a capture
    Unsteady,      ///< a point stayed unsteady or eyes closed for kMaxAttempts tries
    NoMovement,    ///< a point did not move far enough the right way
    WriteFailed,   ///< config.json could not be written
};

/** What the panel shows about a session. */
struct View {
    Phase phase = Phase::Idle;
    Mode mode = Mode::Center;
    int index = 0;             ///< the step shown (0-based)
    int count = 1;             ///< steps in this mode
    Point point = Point::Center;  ///< the point shown, or the one that failed
    int attempt = 1;           ///< try at this point (1-based)
    Failure failure = Failure::None;
    Values values;             ///< the settings written (Done)
};

/** What the caller does after a tick. */
struct Actions {
    bool writeCapture = false;  ///< write a gaze_capture request for `target`, then call captureSent / writeFailed
    const char* target = "";
    bool writeValues = false;   ///< write `values` (once, when done): only the zero point in Center mode
    Values values;
    bool showTarget = false;    ///< show the head-locked target at `point` (hide it otherwise)
    Point point = Point::Center;
    int seconds = 0;            ///< the countdown on the target
    double progress = 0.0;      ///< the ring on the target, 1 -> 0 over one point
};

/**
 * One calibration run. The caller ticks it about 30 times a second while it is active, with whether the
 * dashboard is open and the latest status.json, and carries out the returned actions.
 */
class Session {
public:
    /**
     * Start: wait for the dashboard to close.
     * @param mode center only, or five points
     * @param current the settings now (Center mode keeps the gains)
     * @param now monotonic seconds
     */
    void start(Mode mode, const Values& current, double now);

    /** Stop ("Stop" pressed). */
    void cancel();

    /**
     * Move on.
     * @param now monotonic seconds
     * @param dashboardOpen whether the SteamVR dashboard is open
     * @param status the latest status.json
     * @return what to do
     */
    Actions tick(double now, bool dashboardOpen, const EyeStatus& status);

    /**
     * The capture request asked for by the last tick was written.
     * @param id its id (frameeyeosc answers with the same id)
     * @param now monotonic seconds
     */
    void captureSent(long long id, double now);

    /** A write asked for by the last tick failed. */
    void writeFailed();

    /** @return true while waiting for the dashboard or showing targets */
    bool active() const;

    /** @return what the panel shows */
    View view() const;

private:
    Phase phase_ = Phase::Idle;
    Mode mode_ = Mode::Center;
    Failure failure_ = Failure::None;
    Values current_;
    Values result_;
    Measured measured_[5];
    int index_ = 0;
    int attempt_ = 1;
    Point failedPoint_ = Point::Center;
    double startedAt_ = 0.0;     ///< when Waiting began
    double phaseAt_ = 0.0;       ///< when Settling or Capturing began
    long long captureId_ = 0;    ///< 0 until captureSent
    double runningSeenAt_ = -1;  ///< when frameeyeosc was first seen capturing
    bool requested_ = false;     ///< the request was asked for and not yet confirmed

    /**
     * Stop with a failure.
     * @param failure why
     */
    void fail(Failure failure);

    /**
     * Show the next target, or finish.
     * @param now monotonic seconds
     * @param actions where to ask for the final write
     */
    void next(double now, Actions& actions);
};

}  // namespace gaze_fit
