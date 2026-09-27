// Gaze calibration: targets, the fit and the session.
#include "gaze_fit.h"

#include <algorithm>
#include <cmath>

namespace gaze_fit {

namespace {

constexpr Target kTargets[5] = {
    {Point::Center, "center", 0.0, 0.0},
    {Point::Up, "up", 0.0, kUpDownDeg},
    {Point::Down, "down", 0.0, -kUpDownDeg},
    {Point::Left, "left", -kSideDeg, 0.0},
    {Point::Right, "right", kSideDeg, 0.0},
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

}  // namespace

const Target& target(Point point) {
    return kTargets[static_cast<int>(point)];
}

int pointCount(Mode mode) {
    return mode == Mode::Center ? 1 : 5;
}

Point pointAt(Mode mode, int index) {
    if (mode == Mode::Center) return Point::Center;
    return kTargets[std::clamp(index, 0, 4)].point;
}

bool usable(const Measured& measured) {
    return measured.samples >= kMinSamples && std::isfinite(measured.x) && std::isfinite(measured.y) &&
           std::isfinite(measured.spread) && measured.spread <= kMaxSpread;
}

Values fitCenter(const Measured& center, const Values& current) {
    Values values = current;
    values.offsetX = offsetSetting(center.x);
    values.offsetY = offsetSetting(center.y);
    return values;
}

bool fitFive(const Measured points[5], Values& out, Point& failed) {
    const Measured& center = points[static_cast<int>(Point::Center)];
    const double side = kSideDeg / kFullScaleDeg;
    const double upDown = kUpDownDeg / kFullScaleDeg;
    // How far each point moved from the center, counted in its own direction
    const double right = points[static_cast<int>(Point::Right)].x - center.x;
    const double left = center.x - points[static_cast<int>(Point::Left)].x;
    const double up = points[static_cast<int>(Point::Up)].y - center.y;
    const double down = center.y - points[static_cast<int>(Point::Down)].y;
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

void Session::start(Mode mode, const Values& current, double now) {
    *this = Session();
    phase_ = Phase::Waiting;
    mode_ = mode;
    current_ = current;
    startedAt_ = now;
}

void Session::cancel() {
    if (active()) fail(Failure::Cancelled);
}

bool Session::active() const {
    return phase_ == Phase::Waiting || phase_ == Phase::Settling || phase_ == Phase::Capturing;
}

void Session::fail(Failure failure) {
    phase_ = Phase::Failed;
    failure_ = failure;
    failedPoint_ = pointAt(mode_, index_);
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
    runningSeenAt_ = -1;
}

void Session::next(double now, Actions& actions) {
    ++index_;
    attempt_ = 1;
    if (index_ < pointCount(mode_)) {
        phase_ = Phase::Settling;
        phaseAt_ = now;
        return;
    }
    if (mode_ == Mode::Center) {
        result_ = fitCenter(measured_[0], current_);
    } else {
        Point failed = Point::Center;
        if (!fitFive(measured_, result_, failed)) {
            index_ = static_cast<int>(failed);
            fail(Failure::NoMovement);
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
            // Targets are only shown with the dashboard closed; opening it stops the run
            if (dashboardOpen) {
                fail(Failure::Cancelled);
            } else if (!status.running) {
                fail(Failure::NotRunning);
            }
            break;
    }
    if (phase_ == Phase::Settling && now - phaseAt_ >= kSettleSec) {
        phase_ = Phase::Capturing;
        phaseAt_ = now;
        requested_ = true;
        captureId_ = 0;
        actions.writeCapture = true;
        actions.target = target(pointAt(mode_, index_)).name;
    } else if (phase_ == Phase::Capturing && !requested_) {
        const GazeCaptureStatus& capture = status.capture;
        const bool ours = captureId_ != 0 && capture.present && capture.id == captureId_;
        if (ours && !capture.done && runningSeenAt_ < 0) runningSeenAt_ = now;
        if (ours && capture.done) {
            Measured measured;
            measured.samples = capture.samples;
            if (capture.hasAverage) {
                measured.x = capture.x;
                measured.y = capture.y;
                measured.spread = capture.spread;
            }
            if (usable(measured)) {
                measured_[static_cast<int>(pointAt(mode_, index_))] = measured;
                next(now, actions);
            } else if (++attempt_ > kMaxAttempts) {
                attempt_ = kMaxAttempts;
                fail(Failure::Unsteady);
            } else {
                phase_ = Phase::Settling;
                phaseAt_ = now;
            }
        } else if (now - phaseAt_ >= kResultTimeoutSec) {
            fail(Failure::NoResult);
        }
    }

    if (phase_ == Phase::Settling || phase_ == Phase::Capturing) {
        double left = kCaptureSec;
        if (phase_ == Phase::Settling) {
            left += std::max(0.0, kSettleSec - (now - phaseAt_));
        } else if (runningSeenAt_ >= 0) {
            left = std::max(0.0, kCaptureSec - (now - runningSeenAt_));
        }
        actions.showTarget = true;
        actions.point = pointAt(mode_, index_);
        actions.seconds = std::max(1, static_cast<int>(std::ceil(left - 1e-9)));
        actions.progress = std::clamp(left / (kSettleSec + kCaptureSec), 0.0, 1.0);
    }
    return actions;
}

View Session::view() const {
    View view;
    view.phase = phase_;
    view.mode = mode_;
    view.index = index_;
    view.count = pointCount(mode_);
    view.point = phase_ == Phase::Failed ? failedPoint_ : pointAt(mode_, index_);
    view.attempt = attempt_;
    view.failure = failure_;
    view.values = result_;
    return view;
}

}  // namespace gaze_fit
