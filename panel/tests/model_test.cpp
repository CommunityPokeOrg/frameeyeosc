// Tests for the panel's shared rules (model.cpp): what an eye fit writes and what "Reset" clears, which re-wear fit
// auto_recenter asks for, and the output types behind the destination cards. Built with the panel as model-test;
// exits non-zero on failure.
#include "model.h"

#include <algorithm>
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
 * A config root with a fit and a per-eye scale tweak already in it.
 * @return the root object
 */
JsonValue tweakedRoot() {
    JsonValue root;
    root.type = JsonValue::Type::Object;
    root.set(key::kLidScaleLeft, JsonValue::makeNumber(1.1));
    root.set(key::kLidScaleRight, JsonValue::makeNumber(0.69));
    for (const auto& eye : kLidFitKeys) {
        for (const char* name : eye) root.set(name, JsonValue::makeNumber(0.5));
    }
    return root;
}

/**
 * A number in the root, or NaN when it is null or missing.
 * @param root the root object
 * @param name the key
 * @return the number
 */
double numberIn(const JsonValue& root, const char* name) {
    const JsonValue* value = root.get(name);
    return value != nullptr && value->isNumber() ? value->number : NAN;
}

/**
 * A measured eye fit.
 * @param lids whether it measured the eyelids
 * @return the values
 */
gaze_fit::Values measured(bool lids) {
    gaze_fit::Values values;
    values.offsetX = 0.01;
    values.offsetY = -0.02;
    values.rollDeg = 2.0;
    values.hasLids = lids;
    for (int eye = 0; eye < 2; ++eye) {
        values.lidClosed[eye] = 0.2;
        values.lidUp[eye] = 0.9;
        values.lidOpen[eye] = 0.85;
        values.lidDown[eye] = 0.7;
    }
    return values;
}

/** The whole fit clears the scale tweaks with the new lid readings; the re-wear fits leave them. */
void testFitResetsScales() {
    JsonValue full = tweakedRoot();
    applyFitValues(full, measured(true), gaze_fit::Mode::Full);
    CHECK(full.get(key::kLidScaleLeft) != nullptr && full.get(key::kLidScaleLeft)->isNull());
    CHECK(full.get(key::kLidScaleRight) != nullptr && full.get(key::kLidScaleRight)->isNull());
    CHECK(std::fabs(numberIn(full, key::kLidFitOpenLeft) - 0.85) < 1e-9);

    // Without eyelid readings the old lid fit stays, and so do its tweaks
    JsonValue gazeOnly = tweakedRoot();
    applyFitValues(gazeOnly, measured(false), gaze_fit::Mode::Full);
    CHECK(std::fabs(numberIn(gazeOnly, key::kLidScaleRight) - 0.69) < 1e-9);
    CHECK(std::fabs(numberIn(gazeOnly, key::kLidFitOpenLeft) - 0.5) < 1e-9);

    for (gaze_fit::Mode mode : {gaze_fit::Mode::Center, gaze_fit::Mode::Tilt}) {
        JsonValue rewear = tweakedRoot();
        applyFitValues(rewear, measured(true), mode);
        CHECK(std::fabs(numberIn(rewear, key::kLidScaleLeft) - 1.1) < 1e-9);
        CHECK(std::fabs(numberIn(rewear, key::kLidScaleRight) - 0.69) < 1e-9);
        CHECK(std::fabs(numberIn(rewear, key::kLidFitOpenLeft) - 0.5) < 1e-9);
        CHECK(std::fabs(numberIn(rewear, key::kGazeOffsetX) - 0.01) < 1e-9);
        // Only the re-wear fit with the side dots sets the tilt
        CHECK(std::isnan(numberIn(rewear, key::kGazeRollDeg)) == (mode == gaze_fit::Mode::Center));
    }
}

/** "Reset" clears the scales along with the fit. */
void testResetClearsScales() {
    const std::vector<std::string> keys = fitResetKeys();
    const auto has = [&](const char* name) { return std::find(keys.begin(), keys.end(), name) != keys.end(); };
    CHECK(has(key::kLidScaleLeft) && has(key::kLidScaleRight));
    CHECK(has(key::kGazeOffsetX) && has(key::kGazeRollDeg) && has(key::kGazeGainXRight));
    for (const auto& eye : kLidFitKeys) {
        for (const char* name : eye) CHECK(has(name));
    }
}

/**
 * auto_recenter as written in a config.
 * @param value the value, or null for none
 * @return the kind
 */
AutoRecenter recenterOf(const JsonValue& value) {
    ConfigFile config;
    config.exists = true;
    config.root.type = JsonValue::Type::Object;
    if (!value.isNull()) config.root.set(key::kAutoRecenter, value);
    return autoRecenter(config);
}

/** Re-centering only is the default; the tilt only when chosen; the button re-centers when it is off. */
void testRecenterDefault() {
    CHECK(recenterOf(JsonValue::makeNull()) == AutoRecenter::Center);
    CHECK(recenterOf(JsonValue::makeString("center")) == AutoRecenter::Center);
    CHECK(recenterOf(JsonValue::makeString("tilt")) == AutoRecenter::Tilt);
    CHECK(recenterOf(JsonValue::makeString("off")) == AutoRecenter::Off);
    CHECK(recenterOf(JsonValue::makeString("sideways")) == AutoRecenter::Center);
    CHECK(recenterOf(JsonValue::makeBool(true)) == AutoRecenter::Center);
    CHECK(recenterOf(JsonValue::makeBool(false)) == AutoRecenter::Off);
    CHECK(rewearMode(AutoRecenter::Off) == gaze_fit::Mode::Center);
    CHECK(rewearMode(AutoRecenter::Center) == gaze_fit::Mode::Center);
    CHECK(rewearMode(AutoRecenter::Tilt) == gaze_fit::Mode::Tilt);
    const SettingSpec* spec = findSetting(key::kAutoRecenter);
    CHECK(spec != nullptr && std::string(spec->defaultText) == "center");
}

/**
 * widenState for a made-up model: both eyes fitted with these straight-ahead readings.
 * @param left the left eye's open reading
 * @param right the right eye's
 * @param mode lid_widen
 * @return the state
 */
WidenState widenOf(double left, double right, const char* mode) {
    PanelModel model;
    model.config.exists = true;
    model.config.root.type = JsonValue::Type::Object;
    const double open[2] = {left, right};
    for (int eye = 0; eye < 2; ++eye) {
        const double readings[4] = {0.2, open[eye] + 0.01, open[eye], open[eye] - 0.1};
        for (int i = 0; i < 4; ++i) model.config.root.set(kLidFitKeys[eye][i], JsonValue::makeNumber(readings[i]));
    }
    if (mode != nullptr) model.config.root.set(key::kLidWiden, JsonValue::makeString(mode));
    return widenState(SettingsView(model));
}

/** Which eye can widen by itself, as frameeyeosc decides it. */
void testWidenState() {
    // One user's eyes: the left reads 0.945 straight ahead (no room), the right 0.835; "normal" by default
    WidenState s = widenOf(0.945, 0.835, nullptr);
    CHECK(s.mode == 2 && s.fitted[0] && s.fitted[1] && !s.room[0] && s.room[1]);
    // 0.90 + 0.07 is just within 0.97, 0.91 is not; "low" needs 0.87 or less, "high" 0.93
    s = widenOf(0.90, 0.91, "normal");
    CHECK(s.room[0] && !s.room[1]);
    s = widenOf(0.87, 0.88, "low");
    CHECK(s.mode == 1 && s.room[0] && !s.room[1]);
    s = widenOf(0.93, 0.94, "high");
    CHECK(s.mode == 3 && s.room[0] && !s.room[1]);
    // Off: nobody widens
    s = widenOf(0.8, 0.8, "off");
    CHECK(s.mode == 0 && !s.room[0] && !s.room[1]);
    // Not fitted: the setting doesn't apply
    PanelModel plain;
    plain.config.exists = true;
    plain.config.root.type = JsonValue::Type::Object;
    s = widenState(SettingsView(plain));
    CHECK(!s.fitted[0] && !s.fitted[1] && !s.room[0] && !s.room[1]);
    const SettingSpec* spec = findSetting(key::kLidWiden);
    CHECK(spec != nullptr && std::string(spec->defaultText) == "normal");
}

/** The destination cards' button arguments stand for the output types both ways. */
void testOutputArgs() {
    for (int arg = 0; arg < 3; ++arg) CHECK(argOfOutput(outputOfArg(arg)) == arg);
    CHECK(std::string(outputOfArg(2)) == kOutputLivelink);
    CHECK(argOfOutput("osc") == -1);
}

}  // namespace

/**
 * Run the tests.
 * @return 0 if all passed
 */
int main() {
    testFitResetsScales();
    testResetClearsScales();
    testRecenterDefault();
    testOutputArgs();
    testWidenState();
    if (gFailures == 0) std::printf("model-test: all passed\n");
    return gFailures == 0 ? 0 : 1;
}
