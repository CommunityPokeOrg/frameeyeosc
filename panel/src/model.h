// What the panel shows, put together from config.json, status.json and the autostart state, and the rules that
// several screens share (gaze presets, recommended settings per output, the order of the four lid marks).
#pragma once

#include "autostart.h"
#include "config.h"
#include "gaze_fit.h"
#include "i18n.h"
#include "recorder.h"
#include "status.h"
#include "update_check.h"

#include <string>
#include <vector>

/** Everything one frame of the panel is drawn from. */
struct PanelModel {
    ConfigFile config;           ///< config.json as last read
    EyeStatus status;            ///< status.json as last read
    AutostartState autostart;    ///< the systemd unit state
    std::string configPath;      ///< the file the panel writes
    std::string statusPath;      ///< the file the panel reads
    std::string panelError;      ///< the panel's own last write failure (English detail); empty if none
    bool panelErrorBroken = false;  ///< that failure was because config.json is broken
    Language language = Language::Ja;
    frame_updater::UpdateStatus update;  ///< new-release check and install (see frame-updater)
    gaze_fit::View fit;          ///< the eye fit session (Eye fit tab)
    recorder::View recording;    ///< the eye log (Advanced tab, and a mark in the left column while it records)
};

/**
 * The value shown for a setting: the command-line value from status.json while it is locked there,
 * otherwise the value in config.json (or its default).
 */
class SettingsView {
public:
    /**
     * @param model the model (must outlive the view)
     */
    explicit SettingsView(const PanelModel& model) : model_(model) {}

    /**
     * Whether a key is set on frameeyeosc's command line.
     * @param name the key
     * @return true if the panel must not change it
     */
    bool locked(const std::string& name) const;

    /**
     * The value shown for a key.
     * @param name the key
     * @return the value (null for a nullable key that is automatic)
     */
    JsonValue value(const std::string& name) const;

    /**
     * A number key.
     * @param name the key
     * @return the number, or NaN if null
     */
    double number(const std::string& name) const;

    /**
     * A boolean key.
     * @param name the key
     * @return the value
     */
    bool flag(const std::string& name) const;

    /**
     * A string key.
     * @param name the key
     * @return the value
     */
    std::string text(const std::string& name) const;

    /**
     * The port in use: the "port" key, or the default of the output type when it is null.
     * @return the port
     */
    int port() const;

    /**
     * Whether "port" is null (follows the output type).
     * @return true if automatic
     */
    bool portIsDefault() const;

private:
    const PanelModel& model_;
};

/** The eye fit as config.json holds it (shown on the Eye fit tab even when frameeyeosc is not running). */
struct FitInConfig {
    bool gazeFitted = false;              ///< the zero point, a gain or the tilt is not the default
    bool eyeXFitted = false;              ///< each eye's own sideways zero point and gain are set
    bool lidsFitted[2] = {false, false};  ///< all four readings of that eye are set
    gaze_fit::Values values;              ///< hasLids when both eyes are fitted
};

/** The lid fit keys, [eye][closed, up, open, down]. */
extern const char* const kLidFitKeys[2][4];

/**
 * The eye fit in the settings.
 * @param view the settings
 * @return what is fitted, and the values
 */
FitInConfig fitInConfig(const SettingsView& view);

/**
 * Whether a key the eye fit writes is set on frameeyeosc's command line (then the fit can't run).
 * @param view the settings
 * @return true if any is locked
 */
bool fitKeysLocked(const SettingsView& view);

/** The fit run by itself when the headset is put on (auto_recenter). */
enum class AutoRecenter { Off, Center, Tilt };

/**
 * auto_recenter as written: "off", "center" or "tilt". A true / false from before it had three values reads as
 * "tilt" / "off", and anything else as the default, "tilt".
 * @param config the config
 * @return the kind
 */
AutoRecenter autoRecenter(const ConfigFile& config);

/** A gaze smoothing preset (the three One Euro values). */
struct GazePreset {
    double minCutoff;
    double beta;
    double dCutoff;
};

/**
 * The three gaze presets: light, medium (= the defaults), strong.
 * @return the presets
 */
const GazePreset* gazePresets();

/**
 * Which preset the current values match.
 * @param view the settings
 * @return 0..2, or -1 for custom values
 */
int matchingGazePreset(const SettingsView& view);

/** One key and the value to write. */
struct SettingChange {
    const char* key;
    JsonValue value;
};

/**
 * The output type a SetOutput / PromptYes button stands for.
 * @param arg 0 VRChat, 1 ETVR, 2 LiveLink (anything else is VRChat)
 * @return kOutputVrchat, kOutputEtvr or kOutputLivelink
 */
const char* outputOfArg(int arg);

/**
 * The button argument of an output type (the other way round from outputOfArg).
 * @param output the "output" value
 * @return 0 VRChat, 1 ETVR, 2 LiveLink; -1 for anything else
 */
int argOfOutput(const std::string& output);

/**
 * The recommended settings of an output type (asked once after switching). Locked keys are left out.
 * VRChat and LiveLink: the default gaze and eyelid smoothing (the LiveLink module smooths nothing). ETVR: default
 * gaze smoothing and lighter eyelid smoothing, because the ETVR module already smooths the eyelids.
 * @param output kOutputVrchat, kOutputEtvr or kOutputLivelink
 * @param view the settings (to skip locked keys)
 * @return the changes
 */
std::vector<SettingChange> recommendedSettings(const std::string& output, const SettingsView& view);

/**
 * The range a lid mark may move in without passing its neighbours (closed < open <= widen start <= widest), or
 * a lid fit reading in (closed at least 0.1 below the three open readings).
 * @param name one of the four lid keys, or a lid fit key
 * @param view the settings
 * @param low lower bound (written)
 * @param high upper bound (written)
 */
void lidMarkBounds(const std::string& name, const SettingsView& view, double& low, double& high);

/**
 * Format a number of a key with its decimals (e.g. "0.30").
 * @param name the key
 * @param value the number
 * @return the text ("—" for NaN)
 */
std::string formatSetting(const std::string& name, double value);

/**
 * The IP part of an "IP:PORT" target ("[v6]:port" loses its brackets).
 * @param target the target
 * @return the host, or "" if there is none
 */
std::string hostOfTarget(const std::string& target);
