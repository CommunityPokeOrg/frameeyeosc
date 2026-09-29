// The shared rules of the panel.
#include "model.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <limits>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/** Light / medium / strong: lower values smooth more (medium is frameeyeosc's default). */
const GazePreset kGazePresets[3] = {
    {1.0, 1.5, 1.0},
    {0.4, 0.8, 0.5},
    {0.2, 0.4, 0.3},
};

/** Eyelid smoothing recommended for ETVR (provisional; to be tuned on the headset). */
constexpr double kEtvrLidMinCutoff = 10.0;
constexpr double kEtvrLidBeta = 10.0;

/** The smallest gap between two neighbouring lid marks. */
constexpr double kLidGap = 0.01;

}  // namespace

bool SettingsView::locked(const std::string& name) const {
    return model_.status.isLocked(name);
}

JsonValue SettingsView::value(const std::string& name) const {
    if (locked(name)) {
        if (const JsonValue* effective = model_.status.effective.get(name)) return *effective;
    }
    return model_.config.value(name);
}

double SettingsView::number(const std::string& name) const {
    const JsonValue v = value(name);
    if (v.isNumber()) return v.number;
    if (v.isBool()) return v.boolean ? 1.0 : 0.0;
    return kNaN;
}

bool SettingsView::flag(const std::string& name) const {
    const JsonValue v = value(name);
    return v.isBool() && v.boolean;
}

std::string SettingsView::text(const std::string& name) const {
    const JsonValue v = value(name);
    return v.isString() ? v.text : std::string();
}

int SettingsView::port() const {
    const double value = number(key::kPort);
    if (std::isfinite(value)) return static_cast<int>(std::lround(value));
    return text(key::kOutput) == kOutputEtvr ? kPortEtvr : kPortVrchat;
}

bool SettingsView::portIsDefault() const {
    return !std::isfinite(number(key::kPort));
}

const GazePreset* gazePresets() {
    return kGazePresets;
}

int matchingGazePreset(const SettingsView& view) {
    const double minCutoff = view.number(key::kGazeMinCutoff);
    const double beta = view.number(key::kGazeBeta);
    const double dCutoff = view.number(key::kGazeDCutoff);
    for (int i = 0; i < 3; ++i) {
        const GazePreset& preset = kGazePresets[i];
        if (std::fabs(preset.minCutoff - minCutoff) < 1e-6 && std::fabs(preset.beta - beta) < 1e-6 &&
            std::fabs(preset.dCutoff - dCutoff) < 1e-6) {
            return i;
        }
    }
    return -1;
}

std::vector<SettingChange> recommendedSettings(const std::string& output, const SettingsView& view) {
    const bool etvr = output == kOutputEtvr;
    const GazePreset& medium = kGazePresets[1];
    const SettingSpec* lidMinCutoff = findSetting(key::kLidMinCutoff);
    const SettingSpec* lidBeta = findSetting(key::kLidBeta);
    const std::vector<SettingChange> all = {
        {key::kGazeMinCutoff, JsonValue::makeNumber(medium.minCutoff)},
        {key::kGazeBeta, JsonValue::makeNumber(medium.beta)},
        {key::kGazeDCutoff, JsonValue::makeNumber(medium.dCutoff)},
        {key::kLidMinCutoff, JsonValue::makeNumber(etvr ? kEtvrLidMinCutoff : lidMinCutoff->defaultNumber)},
        {key::kLidBeta, JsonValue::makeNumber(etvr ? kEtvrLidBeta : lidBeta->defaultNumber)},
    };
    std::vector<SettingChange> changes;
    for (const SettingChange& change : all) {
        if (!view.locked(change.key)) changes.push_back(change);
    }
    return changes;
}

const char* const kLidFitKeys[2][4] = {
    {key::kLidFitClosedLeft, key::kLidFitUpLeft, key::kLidFitOpenLeft, key::kLidFitDownLeft},
    {key::kLidFitClosedRight, key::kLidFitUpRight, key::kLidFitOpenRight, key::kLidFitDownRight},
};

bool fitKeysLocked(const SettingsView& view) {
    for (const char* name : {key::kGazeOffsetX, key::kGazeOffsetY, key::kGazeGainX, key::kGazeGainUp, key::kGazeGainDown,
                             key::kGazeOffsetXLeft, key::kGazeOffsetXRight, key::kGazeGainXLeft, key::kGazeGainXRight}) {
        if (view.locked(name)) return true;
    }
    for (const auto& eye : kLidFitKeys) {
        for (const char* name : eye) {
            if (view.locked(name)) return true;
        }
    }
    return false;
}

FitInConfig fitInConfig(const SettingsView& view) {
    FitInConfig fit;
    gaze_fit::Values& v = fit.values;
    v.offsetX = view.number(key::kGazeOffsetX);
    v.offsetY = view.number(key::kGazeOffsetY);
    v.gainX = view.number(key::kGazeGainX);
    v.gainUp = view.number(key::kGazeGainUp);
    v.gainDown = view.number(key::kGazeGainDown);
    fit.gazeFitted = std::fabs(v.offsetX) > 1e-9 || std::fabs(v.offsetY) > 1e-9 || std::fabs(v.gainX - 1) > 1e-9 ||
                     std::fabs(v.gainUp - 1) > 1e-9 || std::fabs(v.gainDown - 1) > 1e-9;
    for (int eye = 0; eye < 2; ++eye) {
        double readings[4];
        bool all = true;
        for (int i = 0; i < 4; ++i) {
            readings[i] = view.number(kLidFitKeys[eye][i]);
            all &= std::isfinite(readings[i]);
        }
        fit.lidsFitted[eye] = all;
        if (!all) continue;
        v.lidClosed[eye] = readings[0];
        v.lidUp[eye] = readings[1];
        v.lidOpen[eye] = readings[2];
        v.lidDown[eye] = readings[3];
    }
    v.hasLids = fit.lidsFitted[0] && fit.lidsFitted[1];
    const double eyeX[4] = {view.number(key::kGazeOffsetXLeft), view.number(key::kGazeOffsetXRight),
                            view.number(key::kGazeGainXLeft), view.number(key::kGazeGainXRight)};
    fit.eyeXFitted = std::all_of(std::begin(eyeX), std::end(eyeX), [](double value) { return std::isfinite(value); });
    if (fit.eyeXFitted) {
        v.hasEyeX = true;
        v.eyeOffsetX[0] = eyeX[0];
        v.eyeOffsetX[1] = eyeX[1];
        v.eyeGainX[0] = eyeX[2];
        v.eyeGainX[1] = eyeX[3];
    }
    return fit;
}

void lidMarkBounds(const std::string& name, const SettingsView& view, double& low, double& high) {
    const double closed = view.number(key::kLidClosed);
    const double open = view.number(key::kLidOpen);
    const double widenStart = view.number(key::kLidWidenStart);
    const double wide = view.number(key::kLidWide);
    low = -1e9;
    high = 1e9;
    if (name == key::kLidClosed) {
        high = open - kLidGap;
    } else if (name == key::kLidOpen) {
        low = closed + kLidGap;
        high = widenStart;
    } else if (name == key::kLidWidenStart) {
        low = open;
        high = wide;
    } else if (name == key::kLidWide) {
        low = widenStart;
    }
    for (int eye = 0; eye < 2; ++eye) {
        for (int i = 0; i < 4; ++i) {
            if (name != kLidFitKeys[eye][i]) continue;
            const double closedReading = view.number(kLidFitKeys[eye][0]);
            if (i == 0) {
                double lowestOpen = 1e9;
                for (int j = 1; j < 4; ++j) lowestOpen = std::fmin(lowestOpen, view.number(kLidFitKeys[eye][j]));
                high = lowestOpen - gaze_fit::kMinLidRange;
            } else {
                low = closedReading + gaze_fit::kMinLidRange;
            }
        }
    }
}

std::string formatSetting(const std::string& name, double value) {
    if (!std::isfinite(value)) return "—";
    const SettingSpec* spec = findSetting(name);
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", spec != nullptr ? spec->decimals : 2, value);
    return text;
}

std::string hostOfTarget(const std::string& target) {
    if (target.empty()) return "";
    if (target[0] == '[') {
        const size_t close = target.find(']');
        return close == std::string::npos ? "" : target.substr(1, close - 1);
    }
    const size_t colon = target.find_last_of(':');
    return colon == std::string::npos ? target : target.substr(0, colon);
}
