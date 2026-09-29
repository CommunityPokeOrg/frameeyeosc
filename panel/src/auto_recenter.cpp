#include "auto_recenter.h"

#include <algorithm>
#include <cstdio>

namespace auto_recenter {

std::string Watcher::disarm(const char* why) {
    armed_ = false;
    return std::string("auto re-center: skipped (") + why + ")";
}

Step Watcher::update(double now, const Inputs& in) {
    Step step;
    const double dt = started_ ? std::max(0.0, now - lastNow_) : 0.0;
    started_ = true;
    lastNow_ = now;
    if (in.dashboardOpen != dashboardOpen_) {
        dashboardOpen_ = in.dashboardOpen;
        if (!in.dashboardOpen) closedAt_ = now;
    }
    // Only while frameeyeosc runs: its status says nothing about the eyes otherwise, and a restart while the headset
    // is worn must not look like putting it on (the time in between counts for nothing)
    if (in.running) {
        if (wasRunning_) (tracking_ ? onFor_ : offFor_) += dt;
        if (in.tracking != tracking_) {
            if (in.tracking) {
                // Back after a real break (not one of the tracker's hiccups): the headset was put on
                if (offFor_ >= kOffSec && in.enabled && in.fitted && !in.locked) {
                    armed_ = true;
                    char text[96];
                    std::snprintf(text, sizeof(text), "put on (tracking was off %.1f s): re-centering once the eyes settle",
                                  offFor_);
                    step.log = text;
                }
                onFor_ = 0.0;
            } else {
                // Still adjusting the headset: wait for the eyes to settle again
                if (armed_) step.log = "auto re-center: eyes lost, waiting for them to settle again";
                offFor_ = 0.0;
            }
            tracking_ = in.tracking;
        }
    }
    wasRunning_ = in.running;
    if (!armed_) return step;
    // Nothing to re-center, or it can't run: this wearing is left alone
    if (!in.enabled) {
        step.log = disarm("switched off");
    } else if (!in.fitted) {
        step.log = disarm("no gaze fit yet");
    } else if (in.locked) {
        step.log = disarm("fit values locked by the command line");
    } else if (in.fitActive) {
        step.log = disarm("an eye fit was started");
    } else if (in.running && tracking_ && onFor_ >= kSettleSec && !in.dashboardOpen && now - closedAt_ >= kClosedSec) {
        // One try per wearing: no retry if it fails or is stopped
        armed_ = false;
        step.start = true;
        step.log = "auto re-center: starting";
    }
    return step;
}

}  // namespace auto_recenter
