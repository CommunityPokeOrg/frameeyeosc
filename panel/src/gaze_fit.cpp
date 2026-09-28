// The eye fit: targets, the fit and the session.
#include "gaze_fit.h"

#include <algorithm>
#include <cmath>

namespace gaze_fit {

namespace {

constexpr Target kTargets[kPointCount] = {
    {Point::Center, "center", 0.0, 0.0},
    {Point::Up, "up", 0.0, kUpDownDeg},
    {Point::Down, "down", 0.0, -kUpDownDeg},
    {Point::Left, "left", -kSideDeg, 0.0},
    {Point::Right, "right", kSideDeg, 0.0},
    // The eyes-shut step shows its words straight ahead
    {Point::Closed, "closed", 0.0, 0.0},
};

/**
 * Round to a number of decimals.
 * @param value the value
 * @param decimals digits after the point
 * @return the rounded value
 */
double roundTo(double value, int decimals) {
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

/**
 * A zero point as written: within range, 3 decimals (the panel's stepper shows 3).
 * @param value the measured center
 * @return the setting
 */
double offsetSetting(double value) {
    return roundTo(std::clamp(value, -kOffsetLimit, kOffsetLimit), 3);
}

/**
 * A gain as written: within range, 2 decimals.
 * @param value the computed gain
 * @return the setting
 */
double gainSetting(double value) {
    return roundTo(std::clamp(value, kGainMin, kGainMax), 2);
}

/**
 * A capture by step.
 * @param points the captures
 * @param point which
 * @return the capture
 */
const Measured& at(const Measured points[kPointCount], Point point) {
    return points[static_cast<int>(point)];
}

}  // namespace

const Target& target(Point point) {
    return kTargets[static_cast<int>(point)];
}

int pointCount(Mode mode) {
    return mode == Mode::Center ? 1 : kPointCount;
}

Point pointAt(Mode mode, int index) {
    if (mode == Mode::Center) return Point::Center;
    return kTargets[std::clamp(index, 0, kPointCount - 1)].point;
}

bool usable(const Measured& measured) {
    return measured.samples >= kMinSamples && std::isfinite(measured.x) && std::isfinite(measured.y) &&
           std::isfinite(measured.spread) && measured.spread <= kMaxSpread;
}

bool usableClosed(const Measured& closed, const Measured& center) {
    if (closed.samples < kMinSamples || !closed.hasOpenness || !center.hasOpenness) return false;
    for (int eye = 0; eye < 2; ++eye) {
        if (!(closed.openness[eye] < kClosedShare * center.openness[eye])) return false;
    }
    return true;
}

double eyeAngle(double yawDeg, int eye, double ipd) {
    const double yaw = yawDeg * M_PI / 180.0;
    // The target seen from the eye: the left eye is at -ipd/2, so the target is ipd/2 further right of it
    const double side = kTargetDistanceM * std::sin(yaw) + (eye == 0 ? ipd / 2 : -ipd / 2);
    return std::atan2(side, kTargetDistanceM * std::cos(yaw)) * 180.0 / M_PI / kFullScaleDeg;
}

bool fitEyes(const Measured points[kPointCount], double ipd, Values& out) {
    const Point used[3] = {Point::Center, Point::Left, Point::Right};
    for (Point point : used) {
        if (!at(points, point).hasEyeX) return true;  // an older frameeyeosc: nothing to fit, not a failure
    }
    Values fitted = out;
    for (int eye = 0; eye < 2; ++eye) {
        const double center = at(points, Point::Center).xEye[eye];
        const double left = at(points, Point::Left).xEye[eye];
        const double right = at(points, Point::Right).xEye[eye];
        const double expectedCenter = eyeAngle(0.0, eye, ipd);
        const double expectedSpan = eyeAngle(kSideDeg, eye, ipd) - eyeAngle(-kSideDeg, eye, ipd);
        if (!(right - left >= kMinMoveFraction * expectedSpan)) return false;
        // (x - offset) * gain: the span sets the gain, and the center then lands on the eye's own angle
        const double gain = gainSetting(expectedSpan / (right - left));
        fitted.eyeGainX[eye] = gain;
        fitted.eyeOffsetX[eye] = offsetSetting(center - expectedCenter / gain);
    }
    fitted.hasEyeX = true;
    out = fitted;
    return true;
}

Values fitCenter(const Measured& center, const Values& current, double ipd) {
    Values values = current;
    values.offsetX = offsetSetting(center.x);
    values.offsetY = offsetSetting(center.y);
    if (current.hasEyeX && center.hasEyeX) {
        for (int eye = 0; eye < 2; ++eye) {
            values.eyeOffsetX[eye] = offsetSetting(center.xEye[eye] - eyeAngle(0.0, eye, ipd) / current.eyeGainX[eye]);
        }
    }
    return values;
}

bool fitGaze(const Measured points[kPointCount], Values& out, Point& failed) {
    const Measured& center = at(points, Point::Center);
    const double side = kSideDeg / kFullScaleDeg;
    const double upDown = kUpDownDeg / kFullScaleDeg;
    // How far each point moved from the center, counted in its own direction
    const double right = at(points, Point::Right).x - center.x;
    const double left = center.x - at(points, Point::Left).x;
    const double up = at(points, Point::Up).y - center.y;
    const double down = center.y - at(points, Point::Down).y;
    const struct {
        Point point;
        double moved;
        double target;
    } checks[4] = {{Point::Up, up, upDown}, {Point::Down, down, upDown}, {Point::Left, left, side},
                   {Point::Right, right, side}};
    for (const auto& check : checks) {
        if (!(check.moved >= kMinMoveFraction * check.target)) {
            failed = check.point;
            return false;
        }
    }
    out.offsetX = offsetSetting(center.x);
    out.offsetY = offsetSetting(center.y);
    out.gainX = gainSetting(2.0 * side / (left + right));
    out.gainUp = gainSetting(upDown / up);
    out.gainDown = gainSetting(upDown / down);
    return true;
}

bool fitLids(const Measured points[kPointCount], Values& out) {
    const Measured& closed = at(points, Point::Closed);
    const Measured* open[3] = {&at(points, Point::Up), &at(points, Point::Center), &at(points, Point::Down)};
    for (int eye = 0; eye < 2; ++eye) {
        for (const Measured* m : open) {
            if (!m->hasOpenness || !(m->openness[eye] >= closed.openness[eye] + kMinLidRange)) return false;
        }
    }
    for (int eye = 0; eye < 2; ++eye) {
        out.lidClosed[eye] = roundTo(closed.openness[eye], 3);
        out.lidUp[eye] = roundTo(open[0]->openness[eye], 3);
        out.lidOpen[eye] = roundTo(open[1]->openness[eye], 3);
        out.lidDown[eye] = roundTo(open[2]->openness[eye], 3);
    }
    out.hasLids = true;
    return true;
}

void Session::start(Mode mode, const Values& current, double now, double ipd) {
    *this = Session();
    phase_ = Phase::Waiting;
    mode_ = mode;
    current_ = current;
    ipd_ = ipd;
    startedAt_ = now;
}

void Session::cancel() {
    if (active()) fail(Failure::Cancelled);
}

bool Session::active() const {
    return phase_ == Phase::Waiting || phase_ == Phase::Settling || phase_ == Phase::Capturing ||
           phase_ == Phase::Reopen;
}

void Session::fail(Failure failure) {
    phase_ = Phase::Failed;
    failure_ = failure;
    failedPoint_ = point();
}

void Session::writeFailed() {
    // After the final write too: the result is then not in effect
    if (active() || phase_ == Phase::Done) fail(Failure::WriteFailed);
}

void Session::captureSent(long long id, double now) {
    if (phase_ != Phase::Capturing || !requested_) return;
    requested_ = false;
    captureId_ = id;
    phaseAt_ = now;
}

void Session::next(double now, Actions& actions) {
    if (point() == Point::Closed) {
        // Nothing more to measure; "open your eyes" first, then the result
        phase_ = Phase::Reopen;
        phaseAt_ = now;
        return;
    }
    previousPoint_ = point();
    ++index_;
    attempt_ = 1;
    if (index_ >= pointCount(mode_)) {
        finish(actions);
        return;
    }
    // Before asking to close the eyes, make sure the gaze part worked
    if (point() == Point::Closed) {
        Values gaze = current_;
        Point failed = Point::Center;
        if (!fitGaze(measured_, gaze, failed)) {
            index_ = static_cast<int>(failed);
            fail(Failure::NoMovement);
            return;
        }
        if (!fitEyes(measured_, ipd_, gaze)) {
            index_ = static_cast<int>(Point::Right);
            fail(Failure::NoMovement);
            return;
        }
    }
    phase_ = Phase::Settling;
    phaseAt_ = now;
}

void Session::finish(Actions& actions) {
    if (mode_ == Mode::Center) {
        result_ = fitCenter(measured_[static_cast<int>(Point::Center)], current_, ipd_);
    } else {
        result_ = current_;
        Point failed = Point::Center;
        if (!fitGaze(measured_, result_, failed)) {
            index_ = static_cast<int>(failed);
            fail(Failure::NoMovement);
            return;
        }
        // Each eye's own sideways fit replaces the one before; without per-eye data there is none
        result_.hasEyeX = false;
        if (!fitEyes(measured_, ipd_, result_)) {
            index_ = static_cast<int>(Point::Right);
            fail(Failure::NoMovement);
            return;
        }
        if (!fitLids(measured_, result_)) {
            fail(Failure::NoLidRange);
            return;
        }
    }
    phase_ = Phase::Done;
    actions.writeValues = true;
    actions.values = result_;
}

Actions Session::tick(double now, bool dashboardOpen, const EyeStatus& status) {
    Actions actions;
    switch (phase_) {
        case Phase::Idle:
        case Phase::Done:
        case Phase::Failed: return actions;
        case Phase::Waiting:
            if (!status.running) {
                fail(Failure::NotRunning);
            } else if (!dashboardOpen) {
                phase_ = Phase::Settling;
                phaseAt_ = now;
            } else if (now - startedAt_ >= kDashboardWaitSec) {
                fail(Failure::WaitTimedOut);
            }
            break;
        case Phase::Settling:
        case Phase::Capturing:
        case Phase::Reopen:
            // Targets are only shown with the dashboard closed; opening it stops the run
            if (dashboardOpen) {
                fail(Failure::Cancelled);
            } else if (!status.running) {
                fail(Failure::NotRunning);
            }
            break;
    }
    if (phase_ == Phase::Settling && now - phaseAt_ >= settleSec()) {
        phase_ = Phase::Capturing;
        phaseAt_ = now;
        requested_ = true;
        captureId_ = 0;
        actions.writeCapture = true;
        actions.target = target(point()).name;
        actions.captureSec = captureSec();
        actions.skipSec = point() == Point::Closed ? kClosedSkipSec : kCaptureSkipSec;
    } else if (phase_ == Phase::Capturing && !requested_) {
        const GazeCaptureStatus& capture = status.capture;
        const bool ours = captureId_ != 0 && capture.present && capture.id == captureId_;
        if (ours && capture.done) {
            Measured measured;
            measured.samples = capture.samples;
            if (capture.hasAverage) {
                measured.x = capture.x;
                measured.y = capture.y;
                measured.spread = capture.spread;
            }
            measured.hasEyeX = capture.hasEyeX;
            measured.xEye[0] = capture.xEye[0];
            measured.xEye[1] = capture.xEye[1];
            measured.hasOpenness = capture.hasOpenness;
            measured.openness[0] = capture.openness[0];
            measured.openness[1] = capture.openness[1];
            const bool closedStep = point() == Point::Closed;
            // The full fit needs each gaze point's openness too, for the lid fit
            const bool ok = closedStep ? usableClosed(measured, measured_[static_cast<int>(Point::Center)])
                                       : usable(measured) && (mode_ == Mode::Center || measured.hasOpenness);
            if (ok) {
                measured_[static_cast<int>(point())] = measured;
                next(now, actions);
            } else if (++attempt_ > kMaxAttempts) {
                attempt_ = kMaxAttempts;
                fail(closedStep ? Failure::NotClosed : Failure::Unsteady);
            } else {
                previousPoint_ = point();
                phase_ = Phase::Settling;
                phaseAt_ = now;
            }
        } else if (now - phaseAt_ >= kResultTimeoutSec) {
            fail(Failure::NoResult);
        }
    } else if (phase_ == Phase::Reopen && now - phaseAt_ >= kReopenSec) {
        finish(actions);
    }

    if (phase_ == Phase::Settling || phase_ == Phase::Capturing || phase_ == Phase::Reopen) {
        const bool closedStep = point() == Point::Closed;
        const double settle = settleSec();
        const double capture = captureSec();
        // Seconds left in the step, the capture counted from when it was asked for (frameeyeosc starts it within a
        // tenth of a second), so the ring runs down evenly
        double left = capture;
        if (phase_ == Phase::Settling) {
            left += std::max(0.0, settle - (now - phaseAt_));
        } else if (phase_ == Phase::Reopen) {
            left = 0.0;
        } else {
            left = std::max(0.0, capture - (now - phaseAt_));
        }
        actions.showTarget = true;
        actions.progress = std::clamp(left / (settle + capture), 0.0, 1.0);
        // Under the dot, the seconds being measured (2, 1); nothing while it glides over and the eyes find it
        actions.seconds = phase_ == Phase::Capturing ? std::max(1, static_cast<int>(std::ceil(left - 1e-9))) : 0;
        if (closedStep) {
            actions.style = phase_ == Phase::Settling    ? TargetStyle::CloseEyes
                            : phase_ == Phase::Capturing ? TargetStyle::KeepClosed
                                                         : TargetStyle::OpenEyes;
            // Counting down to closing the eyes; nothing to count while they are shut
            actions.seconds = phase_ == Phase::Settling
                                  ? std::max(1, static_cast<int>(std::ceil(settle - (now - phaseAt_) - 1e-9)))
                                  : 0;
            // The ring runs the whole way round for the 3, 2, 1, and again while the eyes are shut (not half for each)
            actions.progress = phase_ == Phase::Settling
                                   ? std::clamp((settle - (now - phaseAt_)) / settle, 0.0, 1.0)
                                   : std::clamp(left / capture, 0.0, 1.0);
        }
        // Glide from the previous target to this one at the start of a step (smoothstep easing)
        const Target& from = target(previousPoint_);
        const Target& to = target(point());
        double t = phase_ == Phase::Settling ? std::clamp((now - phaseAt_) / kMoveSec, 0.0, 1.0) : 1.0;
        actions.arrived = t >= 1.0 || previousPoint_ == point();
        t = t * t * (3.0 - 2.0 * t);
        actions.yawDeg = from.yawDeg + (to.yawDeg - from.yawDeg) * t;
        actions.pitchDeg = from.pitchDeg + (to.pitchDeg - from.pitchDeg) * t;
    }
    return actions;
}

View Session::view() const {
    View view;
    view.phase = phase_;
    view.mode = mode_;
    view.index = index_;
    view.count = pointCount(mode_);
    view.point = phase_ == Phase::Failed ? failedPoint_ : point();
    view.attempt = attempt_;
    view.failure = failure_;
    view.values = result_;
    return view;
}

}  // namespace gaze_fit
