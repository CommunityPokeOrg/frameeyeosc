// On-screen text in Japanese and English. Drawing code never contains text itself; it takes it from here.
// Logs stay in English and are not in this table.
#pragma once

#include <string>

/** Display language. */
enum class Language { Ja, En };

/**
 * The Frame's system language, used while config.json has no language.
 * Japanese if Steam's language setting (the "language" value in ~/.steam/registry.vdf, read only) is Japanese;
 * if that can't be read, Japanese if LC_ALL / LC_MESSAGES / LANG is; English otherwise.
 * Looked up once and remembered.
 * @return the language
 */
Language systemLanguage();

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
    const char* trackerRateLabel;   ///< samples from the eye tracker per second (label; the value uses rateFormat)
    const char* trackerRateLowHint;  ///< shown when it is low
    const char* lidsTitle;          ///< eyelids heading
    const char* legendRaw;          ///< raw value (legend)
    const char* legendSent;         ///< sent value (legend)
    const char* left;               ///< "L"
    const char* right;              ///< "R"
    const char* gazeTitle;          ///< gaze heading
    const char* leftEye;            ///< "Left" over the left eye's gaze pad
    const char* rightEye;
    const char* noEyeData;          ///< no eye data
    const char* errorPrefix;        ///< before frameeyeosc's config_error
    const char* errWrite;           ///< writing config.json failed
    const char* errConfigBroken;    ///< config.json can't be parsed
    const char* errAutostart;       ///< systemctl enable/disable failed

    // Tabs
    const char* tabBasic;
    const char* tabGaze;
    const char* tabGazeFit;
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
    const char* rowActiveType;      ///< how EyeTrackingActive is sent
    const char* hintActiveType;
    const char* activeOff;
    const char* rowTarget;
    const char* hintTarget;
    const char* targetAuto;
    const char* targetFixNow;       ///< "Fix to current PC"
    const char* targetEnter;        ///< "Enter IP"
    const char* targetManualFormat;  ///< "Manual %s": a host set by hand (typed or fixed)
    const char* hostEntryTitle;     ///< the keypad for the target PC (IPv4 only; names go in config.json)
    const char* hostEntryHint;
    const char* hostEntryOk;
    const char* hostEntryCancel;
    const char* hostErrEmpty;
    const char* hostErrIpv4;
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
    const char* rowQuality;
    const char* hintQuality;
    const char* rowDespike;
    const char* hintDespike;

    // Eye fit tab
    const char* rowFit;
    const char* hintFit;
    const char* fitStart;            ///< the one big button before any fit
    const char* fitAgain;            ///< the same button once fitted
    const char* fitCenterOnly;       ///< small button: re-center the gaze only
    const char* fitStop;
    const char* fitIntro;            ///< before any fit
    const char* fitNeedsRunning;
    const char* fitLocked;
    const char* fitWaiting;          ///< "Close the dashboard to start"
    const char* fitHowTo;            ///< while waiting / running (full fit)
    const char* fitWaitingCenter;    ///< while waiting / running (re-centering)
    const char* fitRunningFormat;    ///< "Measuring: %s (%d of %d)"
    const char* fitRetryFormat;      ///< appended: ", try %d"
    const char* fitDone;             ///< right after a full fit
    const char* fitDoneCenter;       ///< right after re-centering
    const char* fitFitted;           ///< a fit is in config.json
    const char* fitNotYet;           ///< the folded result line before any fit
    const char* fitGazeCenterFormat; ///< "Gaze center: L-R %s, U-D %s"
    const char* fitGazeRangeFormat;  ///< "Gaze range: L-R %s, up %s, down %s"
    const char* fitGazeNone;
    const char* fitEyeXFormat;       ///< "Each eye: L %s x%s, R %s x%s" (zero point, gain)
    const char* fitLidFormat;        ///< "Eyelid %s: open %s, closed %s, looking down %s"
    const char* fitLidsNone;
    const char* fitFailed;
    const char* failCancelled;
    const char* failWaitTimedOut;
    const char* failNotRunning;
    const char* failNoResult;
    const char* failUnsteadyFormat;  ///< %s = the point
    const char* failNotClosed;
    const char* failNoMovementFormat;
    const char* failLidRange;
    const char* failWrite;
    // The numbers behind a failure (one line under it)
    const char* failDetailSeparator;      ///< between the parts ("・")
    const char* failDetailPointFormat;    ///< "%s dot"
    const char* failDetailSamplesFormat;  ///< "%d of %d samples usable" (from frameeyeosc before 0.5.3)
    const char* failDetailSamplesRateFormat;  ///< "%d of %d samples usable%s (needs %d)", %s = failDetailRateFormat
    const char* failDetailRateFormat;     ///< " at %.0f Hz"
    const char* failDetailSpreadFormat;   ///< "spread %s (max %.1f°)"
    const char* failDetailTriesFormat;    ///< "%d tries"
    const char* failDetailEyeLeft;        ///< "L"
    const char* failDetailEyeRight;
    const char* failDetailClosedFormat;   ///< "%s %.2f (needs below %.2f)"
    const char* failDetailMovedFormat;    ///< "moved %.1f° (needs %.1f°)"
    const char* failDetailSidewaysLeft;   ///< "left eye sideways" (its own fit)
    const char* failDetailSidewaysRight;
    const char* failDetailLidWhereFormat;  ///< "%s eyelid, %s dot"
    const char* failDetailLidFormat;      ///< "open %.2f vs shut %.2f, %.2f apart (needs %.2f)"
    const char* failDetailLidNone;        ///< no open reading
    const char* pointCenter;
    const char* pointUp;
    const char* pointDown;
    const char* pointLeft;
    const char* pointRight;
    const char* pointClosed;
    const char* targetClose;         ///< on the target: "Close your eyes\nfor 3 s" (two lines)
    const char* targetKeepClosed;
    const char* targetOpen;
    const char* fitReset;
    const char* fitDetails;          ///< the fold with the values by hand
    const char* fitSoundsOn;         ///< the sound switch, on ("Sounds: on")
    const char* fitSoundsOff;
    const char* rowOffset;
    const char* hintOffset;
    const char* rowGain;
    const char* hintGain;
    const char* capLeftRight;
    const char* capUpDown;
    const char* capUp;
    const char* capDown;
    const char* detailsGaze;         ///< "Fine-tune" pages
    const char* detailsLids;
    const char* rowEyeX;             ///< each eye's own sideways zero point and gain
    const char* hintEyeX;
    const char* rowDownHold;         ///< holding the sideways gaze when looking far down
    const char* hintDownHold;
    const char* downHoldFormat;      ///< "Below %s°"
    const char* rowLidFit;
    const char* hintLidFit;
    const char* capClosed;
    const char* capAhead;
    const char* lidFitInUse;         ///< Eyelids tab, instead of the learned values

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
    const char* rowBlink;
    const char* hintBlink;
    const char* blinkHold;          ///< label before the blink_hold_ms stepper
    const char* blinkSync;          ///< label before the blink_sync_below stepper
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

    // Updates. The texts are vendor/frame-updater/strings.md word for word (same key names); rowVersion and
    // checkedFormat are this panel's own (the label of the version row on the Advanced tab)
    const char* rowVersion;
    const char* checkedFormat;          ///< after the version under the row label: "・確認 %s" (time or date)
    const char* rowGazeDots;         ///< the debug gaze dots switch
    const char* hintGazeDots;
    const char* dotDistance;         ///< next to its stepper ("Dot distance")
    const char* rowUpdateCheck;
    const char* hintUpdateCheck;
    const char* updateUpToDateFormat;   ///< "Up to date (%s)"
    const char* updateChecking;
    const char* updateAvailableFormat;  ///< "Version %s is available"
    const char* updateButton;
    const char* updateManual;           ///< the release can't be installed from the panel
    const char* updateConfirmFormat;    ///< "Update to %s?"
    const char* updateConfirmHint;
    const char* updateConfirmYes;
    const char* updateConfirmNo;
    const char* updateInstallingFormat; ///< "Updating: %s" (a step below)
    const char* updateInstalledFormat;  ///< "%s is installed. Reopen to use it"
    const char* updateInstallFailed;    ///< followed by the reason
    const char* updateCheckFailed;      ///< followed by the reason
    const char* updateCheckNow;
    const char* updateRetry;
    const char* updateDismiss;
    const char* updateLogHint;
    // Install steps (UpdateStatus::step)
    const char* stepStart;
    const char* stepDownload;
    const char* stepVerify;
    const char* stepExtract;
    const char* stepInstall;
    // Why a check or an install failed (UpdateStatus::error)
    const char* reasonNetwork;
    const char* reasonRateLimited;
    const char* reasonNotFound;
    const char* reasonBadResponse;
    const char* reasonBadVersion;
    const char* reasonBadUrl;
    const char* reasonMissingTool;
    const char* reasonNoChecksums;
    const char* reasonNoAsset;
    const char* reasonChecksumMismatch;
    const char* reasonUnsafeArchive;
    const char* reasonNoInstaller;
    const char* reasonInstallFailed;
    const char* reasonBadArgs;
    const char* reasonBusy;
    const char* reasonNotNewer;
    const char* reasonDetachFailed;
    const char* reasonInterrupted;
    const char* reasonIo;
    const char* reasonUpdater;          ///< usage, script-failed, spawn-failed
    const char* reasonOther;            ///< any other code
};

/**
 * The text table of a language.
 * @param language the language
 * @return the table (valid for the whole program)
 */
const UiText& uiText(Language language);

/**
 * The text of an install step.
 * @param t the text table
 * @param step UpdateStatus::step ("download" and so on)
 * @return the text
 */
const char* updateStepText(const UiText& t, const std::string& step);

/**
 * The text of an update error code.
 * @param t the text table
 * @param code UpdateStatus::error ("network" and so on); unknown codes get a general text
 * @return the text
 */
const char* updateReasonText(const UiText& t, const std::string& code);

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
