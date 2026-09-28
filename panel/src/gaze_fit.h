// The eye fit (the "Eye fit" tab): where the targets are, how captured averages become the gaze zero point and
// gains and each eye's lid fit, and the step-by-step session the panel runs while the dashboard is closed.
// Nothing here talks to OpenVR or writes files, so it can be tested on its own (gaze_fit_test.cpp).
#pragma once

#include "status.h"

#include <cmath>
#include <string>

namespace gaze_fit {

/** The whole fit (five gaze points and the eyes-shut step), or only re-centering the gaze. */
enum class Mode { Full, Center };

/** The steps, in the order the full fit shows them. */
enum class Point { Center, Up, Down, Left, Right, Closed };

/** How many steps there are at most. */
constexpr int kPointCount = 6;

/** A step's target as seen from the head. */
struct Target {
    Point point;
    const char* name;  ///< sent with the capture request and logged by frameeyeosc ("closed" = eyes shut)
    double yawDeg;     ///< degrees to the right
    double pitchDeg;   ///< degrees up
};

/** How far the side and the up / down targets are from straight ahead. */
constexpr double kSideDeg = 20.0;
constexpr double kUpDownDeg = 15.0;
/** The gaze angle that frameeyeosc sends as 1.0. */
constexpr double kFullScaleDeg = 45.0;
/** How far ahead the targets are shown (m). Each eye's own angle to a target depends on it. */
constexpr double kTargetDistanceM = 2.0;
/** The distance between the eyes when SteamVR doesn't say (m). */
constexpr double kDefaultIpdM = 0.063;
/** A capture is used when it has at least this many samples (of about 135 in the 1.5 s frameeyeosc averages)... */
constexpr int kMinSamples = 45;
/** ...and its gaze spreads no more than this (on the -1..1 scale; 0.06 is about 2.7°). */
constexpr double kMaxSpread = 0.06;
/** The eyes-shut capture counts when each eye reads below this share of its straight-ahead open reading. */
constexpr double kClosedShare = 0.7;
/** Each eye's open readings must be at least this far above its closed one (frameeyeosc checks the same). */
constexpr double kMinLidRange = 0.1;
/** Tries per step before giving up. */
constexpr int kMaxAttempts = 3;
/** How long each gaze point takes in all: the dot glides over and the eyes find it, then it is measured. The one
 *  number that makes the fit faster or slower. */
constexpr double kPointSec = 2.5;
/** Of that, seconds a target shows before its capture is asked for, so the eyes can find it... */
constexpr double kSettleSec = 0.5;
/** ...of which the dot spends this long gliding over from the previous target. */
constexpr double kMoveSec = 0.35;
/** The rest is measured by frameeyeosc... */
constexpr double kCaptureSec = kPointSec - kSettleSec;
/** ...skipping its first samples while the eyes settle on the dot. */
constexpr double kCaptureSkipSec = 0.3;
static_assert((kCaptureSec - kCaptureSkipSec) * 90 >= 2 * 45, "at 90 Hz, twice kMinSamples, so a blink still leaves enough");
/** The eyes-shut step: "close your eyes for 3 s" counts down 3, 2, 1 this long, then the capture is asked for... */
constexpr double kCloseSettleSec = 3.0;
/** ...which lasts this long, the time the eyes are shut (what the target says)... */
constexpr double kClosedSec = 3.0;
/** ...skipping its first half second while the eyes close. */
constexpr double kClosedSkipSec = 0.5;
/** After it, "open your eyes" shows this long before the result is written. */
constexpr double kReopenSec = 1.5;
/** Seconds to wait for a capture's result: frameeyeosc checks config.json every 0.1 s and gives up 3 s after the
 *  capture should have ended. */
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
 * A step's target.
 * @param point which one
 * @return where it is
 */
const Target& target(Point point);

/**
 * How many steps a mode has.
 * @param mode the mode
 * @return 6 or 1
 */
int pointCount(Mode mode);

/**
 * The step shown at a position.
 * @param mode the mode
 * @param index 0-based step
 * @return the step
 */
Point pointAt(Mode mode, int index);

/** One capture's result (the raw combined gaze and openness, before any correction or scale). */
struct Measured {
    double x = 0.0;
    double y = 0.0;
    double spread = 0.0;
    bool hasEyeX = false;         ///< xEye is set
    double xEye[2] = {0.0, 0.0};  ///< each eye's own raw sideways gaze, left / right
    double openness[2] = {0.0, 0.0};  ///< left, right
    bool hasOpenness = false;
    int samples = 0;
};

/** The numbers behind a failure, for the panel and the log. */
struct FailureDetail {
    int tries = 0;                      ///< Unsteady / NotClosed: the tries made...
    Measured last;                      ///< ...and the last one
    double closedBelow[2] = {NAN, NAN};  ///< NotClosed: each eye had to read below this (kClosedShare of ahead)
    int eye = -1;                       ///< NoMovement: -1 = the gaze, 0 / 1 = that eye's sideways fit; NoLidRange: the eye
    double movedDeg = NAN;              ///< NoMovement: how far it moved its way...
    double neededDeg = NAN;             ///< ...and how far it had to
    Point lidPoint = Point::Center;     ///< NoLidRange: the open reading nearest the shut one...
    double lidOpen = NAN;               ///< ...that reading (NaN if there was none)...
    double lidClosed = NAN;             ///< ...and the shut one
};

/**
 * One try's numbers for the log, e.g. "center try 2: 128 samples (min 45), spread 3.4° (max 2.7°) -> again".
 * @param point the step
 * @param attempt the try (1-based)
 * @param measured its capture
 * @param center the straight-ahead capture (for the eyes-shut step's limits)
 * @param outcome "ok", "again" or "failed"
 * @return the line
 */
std::string tryText(Point point, int attempt, const Measured& measured, const Measured& center, const char* outcome);

/**
 * Whether a gaze capture is steady and long enough to use.
 * @param measured the capture
 * @return true if usable
 */
bool usable(const Measured& measured);

/**
 * Whether the eyes-shut capture is long enough and the eyes were shut.
 * @param closed the eyes-shut capture
 * @param center the straight-ahead capture (its openness is "open")
 * @return true if usable
 */
bool usableClosed(const Measured& closed, const Measured& center);

/** The settings a fit writes. */
struct Values {
    double offsetX = 0.0;
    double offsetY = 0.0;
    double gainX = 1.0;
    double gainUp = 1.0;
    double gainDown = 1.0;
    bool hasEyeX = false;  ///< each eye's own sideways zero point and gain below are set
    double eyeOffsetX[2] = {0.0, 0.0};  ///< left, right
    double eyeGainX[2] = {1.0, 1.0};
    bool hasLids = false;  ///< the eyelid readings below are set
    double lidClosed[2] = {0.0, 0.0};  ///< left, right
    double lidUp[2] = {0.0, 0.0};
    double lidOpen[2] = {0.0, 0.0};
    double lidDown[2] = {0.0, 0.0};
};

/**
 * Where an eye really looks, on the -1..1 scale, to see a target that is `yawDeg` right of straight ahead at
 * kTargetDistanceM: its angle from that eye, not from between the eyes. +x is right. The left eye sits ipd/2 to
 * the left, so it turns right a little to see a target straight ahead, and the right eye turns left.
 * @param yawDeg the target's angle from between the eyes
 * @param eye 0 = left, 1 = right
 * @param ipd the distance between the eyes (m)
 * @return the angle (1.0 = 45°)
 */
double eyeAngle(double yawDeg, int eye, double ipd);

/**
 * Each eye's own sideways zero point and gain, so that after them the eye points at each target the way it
 * really had to (see eyeAngle). Needs every gaze point's per-eye x; otherwise nothing is set.
 * @param points the captures, indexed by Point
 * @param ipd the distance between the eyes (m)
 * @param out where they go (hasEyeX set when fitted)
 * @param detail on failure, which eye and how far it moved (may be null)
 * @return false if an eye did not move far enough the right way between the side targets
 */
bool fitEyes(const Measured points[kPointCount], double ipd, Values& out, FailureDetail* detail = nullptr);

/**
 * The zero point from the center capture; everything else stays as it is. Each eye's own zero point moves too
 * when there is one, keeping its gain.
 * @param center the center capture
 * @param current the settings now
 * @param ipd the distance between the eyes (m)
 * @return the new settings (rounded, within range)
 */
Values fitCenter(const Measured& center, const Values& current, double ipd = kDefaultIpdM);

/**
 * The zero point and the three gains from the five gaze captures. Each gain makes the target angle come out as
 * that angle: gain = target / (point - center), with left and right averaged into one gain.
 * @param points the captures, indexed by Point
 * @param out the new gaze settings (rounded, within range); the lid readings are left alone
 * @param failed the first point that did not move far enough the right way
 * @return false if a point did not move far enough the right way
 */
bool fitGaze(const Measured points[kPointCount], Values& out, Point& failed, FailureDetail* detail = nullptr);

/**
 * Each eye's lid fit: the eyes-shut reading, and the open readings looking up, straight ahead and down.
 * @param points the captures, indexed by Point (Closed included)
 * @param out where the lid readings go (hasLids set)
 * @param detail on failure, which eye and reading (may be null)
 * @return false if an eye's open readings are not at least kMinLidRange above its closed one
 */
bool fitLids(const Measured points[kPointCount], Values& out, FailureDetail* detail = nullptr);

/** Where a session is. */
enum class Phase {
    Idle,       ///< nothing going on (maybe showing the last result)
    Waiting,    ///< waiting for the dashboard to close
    Settling,   ///< a target is shown; its capture is asked for after the settle time
    Capturing,  ///< waiting for frameeyeosc's result
    Reopen,     ///< "open your eyes" after the eyes-shut step
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
    NotClosed,     ///< the eyes-shut step never read as shut
    NoMovement,    ///< a point did not move far enough the right way
    NoLidRange,    ///< the eyelids barely changed between open and shut
    WriteFailed,   ///< config.json could not be written
};

/** How the target looks at the moment. */
enum class TargetStyle {
    Dot,          ///< a dot to look at
    CloseEyes,    ///< "close your eyes" with a countdown
    KeepClosed,   ///< "keep them closed"
    OpenEyes,     ///< "open your eyes"
};

/** What the panel shows about a session. */
struct View {
    Phase phase = Phase::Idle;
    Mode mode = Mode::Full;
    int index = 0;             ///< the step shown (0-based)
    int count = 1;             ///< steps in this mode
    Point point = Point::Center;  ///< the step shown, or the one that failed
    int attempt = 1;           ///< try at this step (1-based)
    Failure failure = Failure::None;
    FailureDetail detail;      ///< the numbers behind the failure
    Values values;             ///< the settings written (Done)
};

/** What the caller does after a tick. */
struct Actions {
    bool writeCapture = false;  ///< write a gaze_capture request for `target`, then call captureSent / writeFailed
    const char* target = "";
    double captureSec = 0.0;    ///< how long that capture lasts...
    double skipSec = 0.0;       ///< ...and how much of its start frameeyeosc skips
    bool writeValues = false;   ///< write `values` (once, when done): in Center mode only the zero point
    Values values;
    bool showTarget = false;    ///< show the head-locked target (hide it otherwise)
    TargetStyle style = TargetStyle::Dot;
    double yawDeg = 0.0;        ///< where, gliding between targets
    double pitchDeg = 0.0;
    int seconds = 0;            ///< the countdown on the target (0 = none): the seconds measured, or the eyes-shut 3, 2, 1
    double progress = 0.0;      ///< the ring on the target, 1 -> 0 over one step
    bool arrived = false;       ///< the target has finished gliding to this step (or didn't have to move)
    std::string log;            ///< a try's numbers to log (tryText), or empty
};

/**
 * One fit. The caller ticks it every frame while it is active, with whether the dashboard is open and the latest
 * status.json, and carries out the returned actions.
 */
class Session {
public:
    /**
     * Start: wait for the dashboard to close.
     * @param mode the whole fit, or re-centering only
     * @param current the settings now (kept where the mode does not change them)
     * @param now monotonic seconds
     * @param ipd the distance between the eyes (m), for each eye's own angle to the targets
     */
    void start(Mode mode, const Values& current, double now, double ipd = kDefaultIpdM);

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
    Mode mode_ = Mode::Full;
    Failure failure_ = Failure::None;
    Values current_;
    Values result_;
    double ipd_ = kDefaultIpdM;
    Measured measured_[kPointCount];
    int index_ = 0;
    int attempt_ = 1;
    Point failedPoint_ = Point::Center;
    FailureDetail detail_;
    Point previousPoint_ = Point::Center;  ///< where the dot glides from
    double startedAt_ = 0.0;     ///< when Waiting began
    double phaseAt_ = 0.0;       ///< when Settling, Capturing or Reopen began
    long long captureId_ = 0;    ///< 0 until captureSent
    bool requested_ = false;     ///< the request was asked for and not yet confirmed

    /**
     * Stop with a failure.
     * @param failure why
     */
    void fail(Failure failure);

    /**
     * Show the next step, or finish.
     * @param now monotonic seconds
     * @param actions where to ask for the final write
     */
    void next(double now, Actions& actions);

    /**
     * Compute the result and ask for it to be written.
     * @param actions where to ask for the write
     */
    void finish(Actions& actions);

    /** @return the step shown now */
    Point point() const { return pointAt(mode_, index_); }

    /** @return how long the current step settles */
    double settleSec() const { return point() == Point::Closed ? kCloseSettleSec : kSettleSec; }

    /** @return how long the current step's capture lasts */
    double captureSec() const { return point() == Point::Closed ? kClosedSec : kCaptureSec; }
};

}  // namespace gaze_fit
