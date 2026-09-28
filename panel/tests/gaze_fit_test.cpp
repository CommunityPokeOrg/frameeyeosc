// Tests for the eye fit (gaze_fit.cpp): the fit itself and the session, without OpenVR or files.
// Built with the panel as gaze-fit-test; exits non-zero on failure.
#include "gaze_fit.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gFailures = 0;

/**
 * Record a failed check.
 * @param ok the check
 * @param what what was checked
 * @param line where
 */
void check(bool ok, const char* what, int line) {
    if (ok) return;
    ++gFailures;
    std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
}

#define CHECK(condition) check((condition), #condition, __LINE__)

/**
 * Whether two numbers are within 1e-9 of each other.
 * @param a one
 * @param b the other
 * @return true if equal enough
 */
bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

using namespace gaze_fit;

const double kSide = kSideDeg / kFullScaleDeg;
const double kUpDown = kUpDownDeg / kFullScaleDeg;

/**
 * A steady capture with the eyes open.
 * @param x average x
 * @param y average y
 * @param left left eye openness
 * @param right right eye openness
 * @return the capture
 */
Measured steady(double x, double y, double left = 0.9, double right = 0.8) {
    Measured m;
    m.x = x;
    m.y = y;
    m.spread = 0.01;
    m.samples = 120;
    m.hasOpenness = true;
    m.openness[0] = left;
    m.openness[1] = right;
    return m;
}

/**
 * The eyes-shut capture.
 * @param left left eye openness
 * @param right right eye openness
 * @return the capture
 */
Measured shut(double left = 0.15, double right = 0.26) {
    Measured m;
    m.samples = 130;
    m.hasOpenness = true;
    m.openness[0] = left;
    m.openness[1] = right;
    return m;
}

/**
 * status.json as seen while frameeyeosc runs, with a capture in it.
 * @param id the capture id (0 for none)
 * @param done whether it is finished
 * @param m its result
 * @param gaze whether it has a gaze average (not for the eyes-shut step)
 * @return the status
 */
EyeStatus runningWith(long long id, bool done, const Measured& m, bool gaze = true) {
    EyeStatus status;
    status.present = status.running = status.tracking = true;
    if (id != 0) {
        GazeCaptureStatus& c = status.capture;
        c.present = true;
        c.id = id;
        c.target = "x";
        c.done = done;
        c.samples = done ? m.samples : 0;
        c.hasAverage = done && gaze && m.samples > 0;
        c.x = m.x;
        c.y = m.y;
        c.spread = m.spread;
        c.hasOpenness = done && m.hasOpenness;
        c.openness[0] = m.openness[0];
        c.openness[1] = m.openness[1];
    }
    return status;
}

/** The five gaze captures of a user whose tracker reads 10% short sideways, like the live run. */
void fivePoints(Measured points[kPointCount]) {
    points[static_cast<int>(Point::Center)] = steady(0.0116, -0.0196, 0.92, 0.81);
    points[static_cast<int>(Point::Up)] = steady(0.06, -0.0196 + kUpDown / 0.9, 0.93, 0.86);
    points[static_cast<int>(Point::Down)] = steady(0.0, -0.0196 - kUpDown / 0.88, 0.77, 0.75);
    points[static_cast<int>(Point::Left)] = steady(0.0116 - kSide / 0.93, -0.1);
    points[static_cast<int>(Point::Right)] = steady(0.0116 + kSide / 0.93, -0.08);
    points[static_cast<int>(Point::Closed)] = shut();
}

void testFit() {
    Measured points[kPointCount];
    fivePoints(points);
    Values v;
    Point failed = Point::Center;
    CHECK(fitGaze(points, v, failed));
    CHECK(near(v.offsetX, 0.012) && near(v.offsetY, -0.02));  // rounded to 3 decimals
    CHECK(near(v.gainX, 0.93) && near(v.gainUp, 0.9) && near(v.gainDown, 0.88));
    CHECK(fitLids(points, v) && v.hasLids);
    CHECK(near(v.lidClosed[0], 0.15) && near(v.lidClosed[1], 0.26));
    CHECK(near(v.lidOpen[0], 0.92) && near(v.lidUp[1], 0.86) && near(v.lidDown[0], 0.77));

    // A point that went the wrong way (or hardly moved) is reported, and nothing is fitted
    Measured wrong[kPointCount];
    fivePoints(wrong);
    wrong[static_cast<int>(Point::Right)] = steady(0.0116 - 0.05, -0.08);
    CHECK(!fitGaze(wrong, v, failed) && failed == Point::Right);
    // Eyelids that barely closed can't be fitted
    Measured blinkless[kPointCount];
    fivePoints(blinkless);
    blinkless[static_cast<int>(Point::Closed)] = shut(0.85, 0.26);
    Values lids;
    CHECK(!fitLids(blinkless, lids) && !lids.hasLids);

    // Gains stay within 0.5..2 and zero points within ±0.5
    Measured far[kPointCount];
    fivePoints(far);
    far[static_cast<int>(Point::Up)] = steady(0.0, 0.9);
    far[static_cast<int>(Point::Center)] = steady(0.7, -0.0195);
    far[static_cast<int>(Point::Left)] = steady(0.7 - kSide, 0.0);
    far[static_cast<int>(Point::Right)] = steady(0.7 + kSide, 0.0);
    CHECK(fitGaze(far, v, failed));
    CHECK(near(v.gainUp, kGainMin) && near(v.offsetX, kOffsetLimit));
}

/**
 * Each eye's raw x that frameeyeosc would report for a point, for an eye whose tracker reads (angle / gain + offset).
 * @param m the capture to add it to
 * @param yawDeg the target
 * @param gains each eye's gain
 * @param offsets each eye's offset
 */
void withEyes(Measured& m, double yawDeg, const double gains[2], const double offsets[2]) {
    m.hasEyeX = true;
    for (int eye = 0; eye < 2; ++eye) m.xEye[eye] = eyeAngle(yawDeg, eye, kDefaultIpdM) / gains[eye] + offsets[eye];
}

void testEyes() {
    // Seen from between the eyes the target is straight ahead; the left eye turns right to see it, the right left
    const double left = eyeAngle(0.0, 0, 0.063);
    const double right = eyeAngle(0.0, 1, 0.063);
    CHECK(left > 0 && near(left, -right));
    CHECK(std::fabs(left * kFullScaleDeg - std::atan2(0.0315, 2.0) * 180 / M_PI) < 1e-9);
    CHECK(eyeAngle(kSideDeg, 0, 0.063) > eyeAngle(kSideDeg, 1, 0.063));
    CHECK(near(eyeAngle(kSideDeg, 0, 0.0), kSideDeg / kFullScaleDeg));

    const double gains[2] = {0.95, 0.9};
    const double offsets[2] = {0.03, -0.01};
    Measured points[kPointCount];
    fivePoints(points);
    withEyes(points[static_cast<int>(Point::Center)], 0.0, gains, offsets);
    withEyes(points[static_cast<int>(Point::Left)], -kSideDeg, gains, offsets);
    withEyes(points[static_cast<int>(Point::Right)], kSideDeg, gains, offsets);
    Values v;
    CHECK(fitEyes(points, kDefaultIpdM, v) && v.hasEyeX);
    CHECK(near(v.eyeGainX[0], 0.95) && near(v.eyeGainX[1], 0.9));
    CHECK(std::fabs(v.eyeOffsetX[0] - 0.03) < 0.0011 && std::fabs(v.eyeOffsetX[1] + 0.01) < 0.0011);

    // Re-centering moves each eye's zero point and keeps its gain
    Measured moved = points[static_cast<int>(Point::Center)];
    for (double& x : moved.xEye) x += 0.05;
    const Values centered = fitCenter(moved, v, kDefaultIpdM);
    CHECK(std::fabs(centered.eyeOffsetX[0] - 0.08) < 0.0011 && near(centered.eyeGainX[1], 0.9));

    // Without per-eye x (an older frameeyeosc) nothing is fitted and nothing fails
    Measured plain[kPointCount];
    fivePoints(plain);
    Values none;
    CHECK(fitEyes(plain, kDefaultIpdM, none) && !none.hasEyeX);
    // An eye that went the wrong way between the side targets fails
    points[static_cast<int>(Point::Right)].xEye[1] = points[static_cast<int>(Point::Left)].xEye[1] - 0.1;
    CHECK(!fitEyes(points, kDefaultIpdM, v));
}

void testCenterAndUsable() {
    Values current;
    current.gainX = 1.3;
    current.hasLids = true;
    current.lidOpen[0] = 0.9;
    const Values v = fitCenter(steady(-0.0314, 0.1234), current);
    CHECK(near(v.offsetX, -0.031) && near(v.offsetY, 0.123));
    CHECK(near(v.gainX, 1.3) && v.hasLids && near(v.lidOpen[0], 0.9));

    CHECK(usable(steady(0, 0)));
    Measured few = steady(0, 0);
    few.samples = kMinSamples - 1;
    CHECK(!usable(few));
    Measured shaky = steady(0, 0);
    shaky.spread = kMaxSpread + 0.001;
    CHECK(!usable(shaky));
    CHECK(!usable(Measured()));

    const Measured center = steady(0, 0, 0.92, 0.81);
    CHECK(usableClosed(shut(), center));
    CHECK(!usableClosed(shut(0.15, 0.7), center));  // the right eye stayed open
    Measured briefly = shut();
    briefly.samples = 10;
    CHECK(!usableClosed(briefly, center));
}

/**
 * Take a session through one step: settle, ask, answer.
 * @param s the session
 * @param now the time (advanced)
 * @param id the next capture id (advanced)
 * @param answer the capture's result
 * @param settle how long the step settles
 * @param gaze whether the answer has a gaze average
 * @return the actions after the answer
 */
Actions runStep(Session& s, double& now, long long& id, const Measured& answer, double settle = kSettleSec,
                bool gaze = true) {
    s.tick(now, false, runningWith(0, false, {}));
    now += settle;
    const Actions a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.writeCapture);
    CHECK(near(a.captureSec, gaze ? kCaptureSec : kClosedSec) && near(a.skipSec, gaze ? kCaptureSkipSec : kClosedSkipSec));
    s.captureSent(++id, now);
    now += (gaze ? kCaptureSec : kClosedSec) + 0.2;
    return s.tick(now, false, runningWith(id, true, answer, gaze));
}

void testFullSession() {
    Measured points[kPointCount];
    fivePoints(points);
    Session s;
    double now = 100.0;
    long long id = 0;
    s.start(Mode::Full, Values(), now);
    CHECK(s.active() && s.view().phase == Phase::Waiting && s.view().count == 6);
    // Nothing is shown while the dashboard is open
    Actions a = s.tick(now + 0.1, true, runningWith(0, false, {}));
    CHECK(!a.showTarget && s.view().phase == Phase::Waiting);

    // First step: the dot straight ahead, the ring full and no number until it is measured
    now += 1.0;
    a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.showTarget && a.style == TargetStyle::Dot && a.seconds == 0 && near(a.progress, 1.0));
    CHECK(near(a.yawDeg, 0.0) && near(a.pitchDeg, 0.0));
    now += kSettleSec;
    a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.writeCapture && std::string(a.target) == "center" && near(a.captureSec, 2.0) && near(a.skipSec, 0.3));
    CHECK(near(kSettleSec + kCaptureSec, 2.5));
    s.captureSent(++id, now);
    // An old capture's result is not ours; the measured seconds count down 2, 1 and the ring runs down evenly
    a = s.tick(now + 0.5, false, runningWith(id - 1 + 100, true, steady(0.3, 0.3)));
    CHECK(s.view().phase == Phase::Capturing && a.seconds == 2 && near(a.progress, 1.5 / 2.5));
    a = s.tick(now + 1.0, false, runningWith(id, false, {}));
    a = s.tick(now + 1.5, false, runningWith(id, false, {}));
    CHECK(a.seconds == 1 && near(a.progress, 0.5 / 2.5));
    now += 2.2;
    a = s.tick(now, false, runningWith(id, true, points[0]));
    CHECK(s.view().point == Point::Up && s.view().phase == Phase::Settling);

    // The dot glides up from the center over kMoveSec
    a = s.tick(now + kMoveSec / 2, false, runningWith(0, false, {}));
    CHECK(a.pitchDeg > 0.0 && a.pitchDeg < kUpDownDeg);
    a = s.tick(now + kMoveSec, false, runningWith(0, false, {}));
    CHECK(near(a.pitchDeg, kUpDownDeg));

    // An unsteady capture is tried again at the same point, without gliding
    Measured shaky = points[1];
    shaky.spread = 0.2;
    runStep(s, now, id, shaky);
    CHECK(s.view().point == Point::Up && s.view().attempt == 2 && s.view().phase == Phase::Settling);
    a = s.tick(now + 0.01, false, runningWith(0, false, {}));
    CHECK(near(a.pitchDeg, kUpDownDeg));
    for (int i = 1; i < 5; ++i) runStep(s, now, id, points[i]);
    CHECK(s.view().point == Point::Closed);

    // The eyes-shut step: "close your eyes" 3, 2, 1, then "keep them closed", then "open your eyes"
    // The ring runs the full circle over the countdown: full at 3.0 s left, half at 1.5 s, nearly empty at 0.1 s
    a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.style == TargetStyle::CloseEyes && a.seconds == 3 && near(a.progress, 1.0));
    a = s.tick(now + 1.5, false, runningWith(0, false, {}));
    CHECK(a.seconds == 2 && near(a.progress, 0.5));
    a = s.tick(now + 2.9, false, runningWith(0, false, {}));
    CHECK(a.seconds == 1 && near(a.progress, 0.1 / 3));
    a = s.tick(now + 2.5, false, runningWith(0, false, {}));
    CHECK(a.style == TargetStyle::CloseEyes && a.seconds == 1 && !a.writeCapture);
    now += kCloseSettleSec;
    a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.writeCapture && std::string(a.target) == "closed" && a.style == TargetStyle::KeepClosed);
    CHECK(near(a.captureSec, 3.0) && near(a.skipSec, 0.5) && near(a.progress, 1.0));
    s.captureSent(++id, now);
    // ...and again the whole way round while the eyes are shut
    a = s.tick(now + 1.5, false, runningWith(id, false, {}));
    CHECK(a.style == TargetStyle::KeepClosed && near(a.progress, 0.5));
    now += kClosedSec + 0.2;
    a = s.tick(now, false, runningWith(id, true, points[5], false));
    CHECK(s.view().phase == Phase::Reopen && a.style == TargetStyle::OpenEyes && !a.writeValues);
    now += kReopenSec;
    a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.writeValues && !a.showTarget && s.view().phase == Phase::Done && !s.active());
    CHECK(near(a.values.gainX, 0.93) && a.values.hasLids && near(a.values.lidClosed[1], 0.26));
    CHECK(id == 7);
    // Done stays done
    a = s.tick(now + 1, false, runningWith(id, true, points[5], false));
    CHECK(!a.writeValues && !a.writeCapture);
}

void testCenterSession() {
    Session s;
    double now = 0.0;
    long long id = 0;
    Values current;
    current.gainUp = 1.4;
    current.hasLids = true;
    s.start(Mode::Center, current, now);
    CHECK(s.view().count == 1);
    // Re-centering needs no openness
    Measured gazeOnly = steady(0.05, -0.12);
    gazeOnly.hasOpenness = false;
    const Actions a = runStep(s, now, id, gazeOnly);
    CHECK(a.writeValues && s.view().phase == Phase::Done);
    CHECK(near(a.values.offsetX, 0.05) && near(a.values.offsetY, -0.12) && near(a.values.gainUp, 1.4));
    CHECK(a.values.hasLids);
}

void testFailures() {
    Measured points[kPointCount];
    fivePoints(points);
    {
        // Three unsteady tries in a row
        Session s;
        double now = 0.0;
        long long id = 0;
        s.start(Mode::Center, Values(), now);
        Measured shaky = points[0];
        shaky.samples = 3;
        Actions a;
        for (int i = 0; i < kMaxAttempts; ++i) {
            a = runStep(s, now, id, shaky);
            // Every try is logged with its numbers
            const std::string expected = "center try " + std::to_string(i + 1) + ": 3 samples (min 45), spread ";
            CHECK(a.log.rfind(expected, 0) == 0);
            CHECK(a.log.find(i + 1 < kMaxAttempts ? "-> again" : "-> failed") != std::string::npos);
        }
        CHECK(s.view().phase == Phase::Failed && s.view().failure == Failure::Unsteady);
        // The numbers behind it: the tries and the last one
        CHECK(s.view().detail.tries == kMaxAttempts && s.view().detail.last.samples == 3);
        Measured wide = steady(0, 0);
        wide.spread = 3.4 / 45;
        CHECK(tryText(Point::Center, 2, wide, Measured(), "again") ==
              "center try 2: 120 samples (min 45), spread 3.4° (max 2.7°) -> again");
    }
    {
        // The eyes never shut: three tries, then NotClosed
        Session s;
        double now = 0.0;
        long long id = 0;
        s.start(Mode::Full, Values(), now);
        for (int i = 0; i < 5; ++i) runStep(s, now, id, points[i]);
        Actions a;
        for (int i = 0; i < kMaxAttempts; ++i) a = runStep(s, now, id, steady(0, 0), kCloseSettleSec, false);
        CHECK(s.view().failure == Failure::NotClosed && s.view().point == Point::Closed);
        // Each eye had to read below 70% of its straight-ahead reading
        CHECK(near(s.view().detail.closedBelow[0], 0.7 * points[0].openness[0]) && s.view().detail.tries == 3);
        CHECK(a.log.rfind("closed try 3: 120 samples (min 45), openness L ", 0) == 0);
    }
    {
        // "Down" did not move: stops before asking to close the eyes
        Session s;
        double now = 0.0;
        long long id = 0;
        s.start(Mode::Full, Values(), now);
        Measured flat[kPointCount];
        fivePoints(flat);
        flat[static_cast<int>(Point::Down)] = flat[static_cast<int>(Point::Center)];
        for (int i = 0; i < 5; ++i) runStep(s, now, id, flat[i]);
        CHECK(s.view().failure == Failure::NoMovement && s.view().point == Point::Down && id == 5);
        // It did not move at all, and had to move a quarter of 15°
        const FailureDetail& d = s.view().detail;
        CHECK(d.eye == -1 && near(d.movedDeg, 0.0) && near(d.neededDeg, 3.75));
    }
    {
        // Eyelids that barely closed: the gaze is fine, but nothing is written
        Session s;
        double now = 0.0;
        long long id = 0;
        s.start(Mode::Full, Values(), now);
        // The right eye reads low looking down, and its shut reading is less than 0.1 below that
        Measured lowDown[kPointCount];
        fivePoints(lowDown);
        lowDown[static_cast<int>(Point::Down)].openness[1] = 0.6;
        for (int i = 0; i < 5; ++i) runStep(s, now, id, lowDown[i]);
        runStep(s, now, id, shut(0.15, 0.55), kCloseSettleSec, false);
        now += kReopenSec;
        const Actions a = s.tick(now, false, runningWith(0, false, {}));
        CHECK(!a.writeValues && s.view().failure == Failure::NoLidRange);
        // Which eye and reading: the right eye looking down, 0.6 open against 0.55 shut
        const FailureDetail& d = s.view().detail;
        CHECK(d.eye == 1 && d.lidPoint == Point::Down && near(d.lidOpen, 0.6) && near(d.lidClosed, 0.55));
    }
    {
        // Opening the dashboard during a run stops it and hides the target
        Session s;
        s.start(Mode::Full, Values(), 0.0);
        s.tick(0.5, false, runningWith(0, false, {}));
        const Actions a = s.tick(0.8, true, runningWith(0, false, {}));
        CHECK(!a.showTarget && s.view().failure == Failure::Cancelled);
    }
    {
        // frameeyeosc not running
        Session s;
        s.start(Mode::Center, Values(), 0.0);
        s.tick(0.1, true, EyeStatus());
        CHECK(s.view().failure == Failure::NotRunning);
    }
    {
        // No answer to a capture
        Session s;
        s.start(Mode::Center, Values(), 0.0);
        s.tick(0.1, false, runningWith(0, false, {}));
        s.tick(1.2, false, runningWith(0, false, {}));
        s.captureSent(1, 1.2);
        s.tick(1.2 + kResultTimeoutSec + 0.1, false, runningWith(0, false, {}));
        CHECK(s.view().failure == Failure::NoResult);
    }
    {
        // The dashboard never closed
        Session s;
        s.start(Mode::Full, Values(), 0.0);
        s.tick(kDashboardWaitSec + 1, true, runningWith(0, false, {}));
        CHECK(s.view().failure == Failure::WaitTimedOut);
    }
    {
        // "Stop" while waiting, and a failed write
        Session s;
        s.start(Mode::Full, Values(), 0.0);
        s.cancel();
        CHECK(s.view().failure == Failure::Cancelled && !s.active());
        s.start(Mode::Center, Values(), 0.0);
        s.tick(0.1, false, runningWith(0, false, {}));
        const Actions a = s.tick(1.2, false, runningWith(0, false, {}));
        CHECK(a.writeCapture);
        s.writeFailed();
        CHECK(s.view().failure == Failure::WriteFailed);
    }
}

}  // namespace

/**
 * Run the tests.
 * @return 0 if all passed
 */
int main() {
    testFit();
    testEyes();
    testCenterAndUsable();
    testFullSession();
    testCenterSession();
    testFailures();
    if (gFailures == 0) std::printf("gaze-fit-test: all passed\n");
    return gFailures == 0 ? 0 : 1;
}
