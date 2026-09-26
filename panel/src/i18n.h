// On-screen text in Japanese and English. Drawing code never contains text itself; it takes it from here.
// Logs stay in English and are not in this table.
#pragma once

#include <string>

/** Display language. */
enum class Language { Ja, En };

/**
 * All text of the panel. Fields ending in "Format" are printf formats.
 */
struct UiText {
    // Status column
    const char* title;              ///< panel heading
    const char* badgeSending;       ///< sending
    const char* badgeWaiting;       ///< running, sending, but no eye data
    const char* badgePaused;        ///< sending is paused
    const char* badgeNotRunning;    ///< frameeyeosc is not running
    const char* notRunningHint1;    ///< what that means (line 1)
    const char* notRunningHint2;    ///< (line 2)
    const char* destination;        ///< "Destination" label
    const char* outputVrchatShort;  ///< "VRChat" in the destination line
    const char* outputEtvrShort;    ///< "VRCFaceTracking" in the destination line
    const char* searchingPc;        ///< auto target not found yet
    const char* modeAuto;           ///< target chosen automatically
    const char* modeFixed;          ///< target fixed
    const char* rateLabel;          ///< messages per second label
    const char* rateFormat;         ///< "%.0f /s"
    const char* lidsTitle;          ///< eyelids heading
    const char* legendRaw;          ///< raw value (legend)
    const char* legendSent;         ///< sent value (legend)
    const char* left;               ///< "L"
    const char* right;              ///< "R"
    const char* gazeTitle;          ///< gaze heading
    const char* noEyeData;          ///< no eye data
    const char* errorPrefix;        ///< before frameeyeosc's config_error
    const char* errWrite;           ///< writing config.json failed
    const char* errConfigBroken;    ///< config.json can't be parsed
    const char* errAutostart;       ///< systemctl enable/disable failed

    // Tabs
    const char* tabBasic;
    const char* tabGaze;
    const char* tabLids;
    const char* tabAdvanced;

    // Shared
    const char* on;
    const char* off;
    const char* locked;             ///< "Locked by command line"
    const char* lowerSmoother;      ///< lower = smoother
    const char* capStill;           ///< One Euro min cutoff caption
    const char* capFast;            ///< One Euro beta caption
    const char* capChange;          ///< One Euro derivative cutoff caption

    // Basic tab
    const char* rowSending;
    const char* hintSending;
    const char* send;
    const char* stop;
    const char* rowOutput;
    const char* hintOutput;
    const char* outputVrchat;
    const char* outputEtvr;
    const char* rowTarget;
    const char* hintTarget;
    const char* targetAuto;
    const char* targetFixNow;       ///< "Fix to current PC"
    const char* targetFixedFormat;  ///< "Fixed %s"
    const char* rowPort;
    const char* portDefaultHint;    ///< port follows the output type
    const char* portReset;          ///< back to default port
    const char* rowLanguage;
    const char* rowAutostart;
    const char* hintAutostart;
    const char* autostartMissing;
    const char* autostartUnknown;
    const char* resetAll;
    const char* resetConfirm;
    const char* quit;
    const char* quitConfirm;
    const char* footer;             ///< changes apply at once

    // Gaze tab
    const char* rowSmoothing;
    const char* hintSmoothing;
    const char* rowStrength;
    const char* strengthLight;
    const char* strengthMedium;
    const char* strengthStrong;
    const char* custom;             ///< values don't match a preset
    const char* rawOnNote;          ///< smoothing is off, so these don't apply
    const char* rowFine;
    const char* rowDeadzone;
    const char* hintDeadzone;
    const char* rowHold;
    const char* hintHold;
    const char* rowIndependent;
    const char* hintIndependent;

    // Lids tab
    const char* rowCalibration;
    const char* learnedFormat;      ///< "Learned L %s / R %s"
    const char* notLearned;
    const char* learning;
    const char* calibrationReset;
    const char* rowScale;
    const char* hintScaleAuto;
    const char* hintScaleFixed;
    const char* scaleAuto;
    const char* scaleFixed;
    const char* marksTitle;         ///< "Open and close your eyes and match the lines"
    const char* markClosed;
    const char* markOpen;
    const char* markWidenStart;
    const char* markWide;
    const char* rowSync;
    const char* hintSync;
    const char* rowLidSmooth;

    // Advanced tab
    const char* rowPrefix;
    const char* prefixNone;
    const char* prefixExample;      ///< "e.g." before an OSC address
    const char* prefixOther;        ///< "Now: %s"
    const char* rowConfigPath;
    const char* configPathMismatch; ///< frameeyeosc reads another config file
    const char* rowCalibrationPath;
    const char* rowStatusPath;
    const char* rowLockedList;
    const char* hintLockedList;
    const char* noneLocked;
    const char* rowCore;
    const char* coreFormat;         ///< "PID %d, up %s"
    const char* notRunning;
    const char* hoursMinutesFormat; ///< "%d h %d min"
    const char* minutesFormat;      ///< "%d min"

    // Recommendation prompt
    const char* promptVrchat;
    const char* promptEtvr;
    const char* promptVrchatDetail1;
    const char* promptVrchatDetail2;
    const char* promptEtvrDetail1;
    const char* promptEtvrDetail2;
    const char* promptYes;
    const char* promptNo;
};

/**
 * The text table of a language.
 * @param language the language
 * @return the table (valid for the whole program)
 */
const UiText& uiText(Language language);

/**
 * The language name written in config.json.
 * @param language the language
 * @return "ja" / "en"
 */
const char* languageCode(Language language);

/**
 * Read a language name from config.json.
 * @param code "ja" / "en"
 * @param language where to write it
 * @return true if the name is known
 */
bool parseLanguage(const std::string& code, Language& language);
