// What the panel shows, put together from config.json, status.json and the autostart state, and the rules that
// several screens share (gaze presets, recommended settings per output, the order of the four lid marks).
#pragma once

#include "autostart.h"
#include "config.h"
#include "i18n.h"
#include "status.h"

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
 * The recommended settings of an output type (asked once after switching). Locked keys are left out.
 * VRChat: the default gaze and eyelid smoothing. ETVR: default gaze smoothing and lighter eyelid smoothing,
 * because the ETVR module already smooths the eyelids.
 * @param output kOutputVrchat or kOutputEtvr
 * @param view the settings (to skip locked keys)
 * @return the changes
 */
std::vector<SettingChange> recommendedSettings(const std::string& output, const SettingsView& view);

/**
 * The range a lid mark may move in without passing its neighbours
 * (closed < open <= widen start <= widest).
 * @param name one of the four lid keys
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
