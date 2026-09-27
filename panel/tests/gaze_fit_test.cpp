// Tests for the gaze fit (gaze_fit.cpp): the fit itself and the session, without OpenVR or files.
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

/**
 * A steady capture.
 * @param x average x
 * @param y average y
 * @return the capture
 */
Measured steady(double x, double y) {
    Measured m;
    m.x = x;
    m.y = y;
    m.spread = 0.01;
    m.samples = 120;
    return m;
}

/**
 * status.json as seen while frameeyeosc runs, with a capture in it.
 * @param id the capture id (0 for none)
 * @param done whether it is finished
 * @param m its result
 * @return the status
 */
EyeStatus runningWith(long long id, bool done, const Measured& m) {
    EyeStatus status;
    status.present = status.running = status.tracking = true;
    if (id != 0) {
        status.capture.present = true;
        status.capture.id = id;
        status.capture.target = "x";
        status.capture.done = done;
        status.capture.samples = done ? m.samples : 0;
        status.capture.hasAverage = done && m.samples > 0;
        status.capture.x = m.x;
        status.capture.y = m.y;
        status.capture.spread = m.spread;
    }
    return status;
}

void testFitFive() {
    const double side = kSideDeg / kFullScaleDeg;
    const double upDown = kUpDownDeg / kFullScaleDeg;
    Measured points[5];
    points[static_cast<int>(Point::Center)] = steady(0.02, -0.10);
    points[static_cast<int>(Point::Up)] = steady(0.02, -0.10 + upDown / 1.25);
    points[static_cast<int>(Point::Down)] = steady(0.02, -0.10 - upDown / 0.8);
    points[static_cast<int>(Point::Left)] = steady(0.02 - side / 1.1, -0.10);
    points[static_cast<int>(Point::Right)] = steady(0.02 + side / 1.1, -0.10);
    Values v;
    Point failed = Point::Center;
    CHECK(fitFive(points, v, failed));
    CHECK(near(v.offsetX, 0.02) && near(v.offsetY, -0.1));
    CHECK(near(v.gainX, 1.1) && near(v.gainUp, 1.25) && near(v.gainDown, 0.8));

    // A point that went the wrong way (or hardly moved) is reported, and nothing is fitted
    points[static_cast<int>(Point::Right)] = steady(0.02 - 0.05, -0.10);
    CHECK(!fitFive(points, v, failed) && failed == Point::Right);
    points[static_cast<int>(Point::Right)] = steady(0.02 + side / 1.1, -0.10);
    points[static_cast<int>(Point::Up)] = steady(0.02, -0.10 + 0.2 * upDown);
    CHECK(!fitFive(points, v, failed) && failed == Point::Up);

    // Gains stay within 0.5..2 and zero points within ±0.5
    points[static_cast<int>(Point::Up)] = steady(0.02, 0.9);
    points[static_cast<int>(Point::Center)] = steady(0.7, -0.10);
    points[static_cast<int>(Point::Left)] = steady(0.7 - side / 1.1, -0.10);
    points[static_cast<int>(Point::Right)] = steady(0.7 + side / 1.1, -0.10);
    CHECK(fitFive(points, v, failed));
    CHECK(near(v.gainUp, kGainMin) && near(v.offsetX, kOffsetLimit));
}

void testFitCenterAndUsable() {
    Values current;
    current.gainX = 1.3;
    current.gainDown = 0.7;
    const Values v = fitCenter(steady(-0.0314, 0.1234), current);
    CHECK(near(v.offsetX, -0.031) && near(v.offsetY, 0.123));
    CHECK(near(v.gainX, 1.3) && near(v.gainDown, 0.7));

    CHECK(usable(steady(0, 0)));
    Measured few = steady(0, 0);
    few.samples = kMinSamples - 1;
    CHECK(!usable(few));
    Measured shaky = steady(0, 0);
    shaky.spread = kMaxSpread + 0.001;
    CHECK(!usable(shaky));
    CHECK(!usable(Measured()));
}

void testCenterSession() {
    Session s;
    Values current;
    current.gainUp = 1.4;
    s.start(Mode::Center, current, 100.0);
    CHECK(s.active() && s.view().phase == Phase::Waiting);
    // Nothing is shown while the dashboard is open
    Actions a = s.tick(100.1, true, runningWith(0, false, {}));
    CHECK(!a.showTarget && s.view().phase == Phase::Waiting);
    a = s.tick(101.0, false, runningWith(0, false, {}));
    CHECK(a.showTarget && a.point == Point::Center && a.seconds == 3 && near(a.progress, 1.0));
    CHECK(!a.writeCapture);
    a = s.tick(102.0, false, runningWith(0, false, {}));
    CHECK(a.writeCapture && std::string(a.target) == "center" && a.showTarget);
    s.captureSent(41, 102.0);
    // An old capture's result is not ours
    a = s.tick(102.5, false, runningWith(40, true, steady(0.3, 0.3)));
    CHECK(s.view().phase == Phase::Capturing && a.seconds == 2);
    a = s.tick(103.2, false, runningWith(41, false, {}));
    CHECK(a.seconds == 2);
    a = s.tick(104.0, false, runningWith(41, false, {}));
    CHECK(a.seconds == 2 && a.progress < 0.5);
    a = s.tick(105.3, false, runningWith(41, true, steady(0.05, -0.12)));
    CHECK(a.writeValues && !a.showTarget && s.view().phase == Phase::Done && !s.active());
    CHECK(near(a.values.offsetX, 0.05) && near(a.values.offsetY, -0.12) && near(a.values.gainUp, 1.4));
    // Done stays done
    a = s.tick(106.0, false, runningWith(41, true, steady(0.05, -0.12)));
    CHECK(!a.writeValues && !a.writeCapture);
}

/**
 * Take a session through one point: settle, ask, answer.
 * @param s the session
 * @param now the time (advanced)
 * @param id the next capture id (advanced)
 * @param answer the capture's result
 * @return the actions after the answer
 */
Actions runPoint(Session& s, double& now, long long& id, const Measured& answer) {
    Actions a = s.tick(now, false, runningWith(0, false, {}));
    now += kSettleSec;
    a = s.tick(now, false, runningWith(0, false, {}));
    CHECK(a.writeCapture);
    s.captureSent(++id, now);
    now += 2.5;
    return s.tick(now, false, runningWith(id, true, answer));
}

void testFiveSessionWithRetry() {
    Session s;
    double now = 10.0;
    long long id = 0;
    s.start(Mode::FivePoint, Values(), now);
    const double side = kSideDeg / kFullScaleDeg;
    const double upDown = kUpDownDeg / kFullScaleDeg;
    runPoint(s, now, id, steady(0.0, 0.0));
    CHECK(s.view().index == 1 && s.view().point == Point::Up);
    // An unsteady capture is tried again at the same point
    Measured shaky = steady(0.0, upDown);
    shaky.spread = 0.2;
    runPoint(s, now, id, shaky);
    CHECK(s.view().point == Point::Up && s.view().attempt == 2 && s.view().phase == Phase::Settling);
    runPoint(s, now, id, steady(0.0, upDown));
    runPoint(s, now, id, steady(0.0, -upDown / 2.0));
    runPoint(s, now, id, steady(-side, 0.0));
    const Actions a = runPoint(s, now, id, steady(side, 0.0));
    CHECK(a.writeValues && s.view().phase == Phase::Done);
    CHECK(near(a.values.gainX, 1.0) && near(a.values.gainUp, 1.0) && near(a.values.gainDown, 2.0));
    CHECK(id == 6);
}

void testFailures() {
    const double upDown = kUpDownDeg / kFullScaleDeg;
    Measured shaky = steady(0.0, 0.0);
    shaky.samples = 3;
    {
        // Three unsteady tries in a row
        Session s;
        double now = 0.0;
        long long id = 0;
        s.start(Mode::Center, Values(), now);
        for (int i = 0; i < kMaxAttempts; ++i) runPoint(s, now, id, shaky);
        CHECK(s.view().phase == Phase::Failed && s.view().failure == Failure::Unsteady);
    }
    {
        // Opening the dashboard during a run stops it and hides the target
        Session s;
        s.start(Mode::FivePoint, Values(), 0.0);
        s.tick(0.5, false, runningWith(0, false, {}));
        const Actions a = s.tick(0.8, true, runningWith(0, false, {}));
        CHECK(!a.showTarget && s.view().failure == Failure::Cancelled);
    }
    {
        // frameeyeosc not running, before or during a run
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
        s.start(Mode::Center, Values(), 0.0);
        s.tick(kDashboardWaitSec + 1, true, runningWith(0, false, {}));
        CHECK(s.view().failure == Failure::WaitTimedOut);
    }
    {
        // "Stop" while waiting, and a failed write
        Session s;
        s.start(Mode::Center, Values(), 0.0);
        s.cancel();
        CHECK(s.view().failure == Failure::Cancelled && !s.active());
        s.start(Mode::Center, Values(), 0.0);
        s.tick(0.1, false, runningWith(0, false, {}));
        const Actions a = s.tick(1.2, false, runningWith(0, false, {}));
        CHECK(a.writeCapture);
        s.writeFailed();
        CHECK(s.view().failure == Failure::WriteFailed);
    }
    {
        // A five-point run where "down" did not move: the fit fails at down
        Session s;
        double now = 0.0;
        long long id = 0;
        s.start(Mode::FivePoint, Values(), now);
        runPoint(s, now, id, steady(0.0, 0.0));
        runPoint(s, now, id, steady(0.0, upDown));
        runPoint(s, now, id, steady(0.0, 0.0));
        runPoint(s, now, id, steady(-0.4, 0.0));
        const Actions a = runPoint(s, now, id, steady(0.4, 0.0));
        CHECK(!a.writeValues && s.view().failure == Failure::NoMovement && s.view().point == Point::Down);
    }
}

}  // namespace

/**
 * Run the tests.
 * @return 0 if all passed
 */
int main() {
    testFitFive();
    testFitCenterAndUsable();
    testCenterSession();
    testFiveSessionWithRetry();
    testFailures();
    if (gFailures == 0) std::printf("gaze-fit-test: all passed\n");
    return gFailures == 0 ? 0 : 1;
}
