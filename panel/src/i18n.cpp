// The text tables.
#include "i18n.h"

namespace {

/**
 * Build the Japanese table. Short words; technical terms are replaced with everyday ones
 * (One Euro -> "なめらかさ", deadzone -> "見つめている時の遊び").
 * @return the table
 */
UiText makeJapanese() {
    UiText t {};
    t.title = "目の送信";
    t.badgeSending = "送信中";
    t.badgeWaiting = "目のデータ待ち";
    t.badgePaused = "一時停止中";
    t.badgeNotRunning = "本体が動いていません";
    t.notRunningHint1 = "設定の変更は保存され、";
    t.notRunningHint2 = "本体が起動すると反映されます";
    t.destination = "送り先";
    t.outputVrchatShort = "VRChat";
    t.outputEtvrShort = "VRCFaceTracking";
    t.searchingPc = "PC を探しています…";
    t.modeAuto = "自動（Steam Link の相手）";
    t.modeFixed = "固定";
    t.rateLabel = "送信の回数";
    t.rateFormat = "毎秒 %.0f 回";
    t.lidsTitle = "まぶた";
    t.legendRaw = "生の値";
    t.legendSent = "送った値";
    t.left = "左";
    t.right = "右";
    t.gazeTitle = "視線";
    t.noEyeData = "目のデータがありません";
    t.errorPrefix = "設定のエラー: ";
    t.errWrite = "設定を書けません: ";
    t.errConfigBroken = "設定ファイルが壊れています。「すべて既定に戻す」で作り直せます";
    t.errAutostart = "自動起動の切り替えに失敗（systemctl）";

    t.tabBasic = "基本";
    t.tabGaze = "視線";
    t.tabLids = "まぶた";
    t.tabAdvanced = "詳細";

    t.on = "オン";
    t.off = "オフ";
    t.locked = "コマンドで固定中";
    t.lowerSmoother = "低いほどなめらか";
    t.capStill = "止まっている時";
    t.capFast = "速い動き";
    t.capChange = "変化の感度";

    t.rowSending = "送信";
    t.hintSending = "止めても本体は動いたまま";
    t.send = "送る";
    t.stop = "止める";
    t.rowOutput = "送り先";
    t.hintOutput = "VRCFT へは ETVR 形式";
    t.outputVrchat = "VRChat に直接";
    t.outputEtvr = "VRCFaceTracking（ETVR）";
    t.rowTarget = "送り先の PC";
    t.hintTarget = "自動 = Steam Link の相手";
    t.targetAuto = "自動";
    t.targetFixNow = "今の相手で固定";
    t.targetFixedFormat = "固定 %s";
    t.rowPort = "ポート";
    t.portDefaultHint = "送り先の種類の既定";
    t.portReset = "既定に戻す";
    t.rowLanguage = "言語";
    t.rowAutostart = "SteamVR と一緒に起動";
    t.hintAutostart = "次の SteamVR の起動から";
    t.autostartMissing = "準備されていません";
    t.autostartUnknown = "状態を読めません";
    t.resetAll = "すべて既定に戻す";
    t.resetConfirm = "もう一度押すと戻す";
    t.quit = "アプリを終了";
    t.quitConfirm = "もう一度押すと終了";
    t.footer = "押すとすぐ本体に反映され、再起動しても残ります。パネルを終了しても送信は続きます";

    t.rowSmoothing = "スムージング";
    t.hintSmoothing = "オフ = 生の値をそのまま";
    t.rowStrength = "なめらかさ";
    t.strengthLight = "弱";
    t.strengthMedium = "中";
    t.strengthStrong = "強";
    t.custom = "カスタム（下の値）";
    t.rawOnNote = "スムージングがオフです";
    t.rowFine = "細かく変える";
    t.rowDeadzone = "見つめている時の遊び";
    t.hintDeadzone = "これより小さい揺れは無視";
    t.rowHold = "まばたき中は視線を止める";
    t.hintHold = "この値より閉じたら止める";
    t.rowIndependent = "左右の目を別々に動かす";
    t.hintIndependent = "Frame ではぶれやすい";

    t.rowCalibration = "自動キャリブレーション";
    t.learnedFormat = "覚えた値 左 %s・右 %s";
    t.notLearned = "まだ覚えていません";
    t.learning = "覚えている最中";
    t.calibrationReset = "リセット";
    t.rowScale = "左右の倍率";
    t.hintScaleAuto = "自動 = キャリブレーションの値";
    t.hintScaleFixed = "固定の倍率を使います";
    t.scaleAuto = "自動";
    t.scaleFixed = "固定";
    t.marksTitle = "目を開け閉めしながら、線を今の値に合わせます（倍率を掛けた後の生の値）";
    t.markClosed = "閉じ";
    t.markOpen = "普通";
    t.markWidenStart = "見開き始め";
    t.markWide = "見開き最大";
    t.rowSync = "左右をそろえる強さ";
    t.hintSync = "0 = そろえない。ウインクは通す";
    t.rowLidSmooth = "まぶたのなめらかさ";

    t.rowPrefix = "パラメーター名の頭";
    t.prefixNone = "なし";
    t.prefixExample = "例: ";
    t.prefixOther = "今: %s";
    t.rowConfigPath = "設定ファイル";
    t.configPathMismatch = "本体は別の設定ファイルを読んでいます: ";
    t.rowCalibrationPath = "キャリブレーション";
    t.rowStatusPath = "状態ファイル";
    t.rowLockedList = "コマンドで固定中";
    t.hintLockedList = "FRAMEEYEOSC_ARGS の項目";
    t.noneLocked = "なし";
    t.rowCore = "本体";
    t.coreFormat = "PID %d・動いて %s";
    t.notRunning = "動いていません";
    t.hoursMinutesFormat = "%d 時間 %d 分";
    t.minutesFormat = "%d 分";

    t.promptVrchat = "VRChat 向けのおすすめ設定にする？";
    t.promptEtvr = "VRCFaceTracking（ETVR）向けのおすすめ設定にする？";
    t.promptVrchatDetail1 = "視線とまぶたのなめらかさを標準に戻します";
    t.promptVrchatDetail2 = "";
    t.promptEtvrDetail1 = "視線のなめらかさは標準、まぶたのなめらかさは弱めにします";
    t.promptEtvrDetail2 = "（ETVR 側でもまぶたをなめらかにしているため）";
    t.promptYes = "する";
    t.promptNo = "しない";
    return t;
}

/**
 * Build the English table (same meaning, kept short).
 * @return the table
 */
UiText makeEnglish() {
    UiText t {};
    t.title = "Eye tracking";
    t.badgeSending = "Sending";
    t.badgeWaiting = "Waiting for eye data";
    t.badgePaused = "Paused";
    t.badgeNotRunning = "frameeyeosc is not running";
    t.notRunningHint1 = "Changes are saved and apply";
    t.notRunningHint2 = "when frameeyeosc starts";
    t.destination = "Destination";
    t.outputVrchatShort = "VRChat";
    t.outputEtvrShort = "VRCFaceTracking";
    t.searchingPc = "Looking for the PC…";
    t.modeAuto = "Auto (Steam Link PC)";
    t.modeFixed = "Fixed";
    t.rateLabel = "Send rate";
    t.rateFormat = "%.0f /s";
    t.lidsTitle = "Eyelids";
    t.legendRaw = "Raw";
    t.legendSent = "Sent";
    t.left = "L";
    t.right = "R";
    t.gazeTitle = "Gaze";
    t.noEyeData = "No eye data";
    t.errorPrefix = "Config error: ";
    t.errWrite = "Can't save settings: ";
    t.errConfigBroken = "config.json is broken. \"Reset all\" makes a new one";
    t.errAutostart = "Autostart change failed (systemctl)";

    t.tabBasic = "Basic";
    t.tabGaze = "Gaze";
    t.tabLids = "Eyelids";
    t.tabAdvanced = "Advanced";

    t.on = "On";
    t.off = "Off";
    t.locked = "Locked by command line";
    t.lowerSmoother = "Lower = smoother";
    t.capStill = "When still";
    t.capFast = "Fast moves";
    t.capChange = "Change sensitivity";

    t.rowSending = "Sending";
    t.hintSending = "frameeyeosc keeps running";
    t.send = "Send";
    t.stop = "Pause";
    t.rowOutput = "Send to";
    t.hintOutput = "VRCFT gets the ETVR format";
    t.outputVrchat = "VRChat directly";
    t.outputEtvr = "VRCFaceTracking (ETVR)";
    t.rowTarget = "Target PC";
    t.hintTarget = "Auto = the Steam Link PC";
    t.targetAuto = "Auto";
    t.targetFixNow = "Fix to current PC";
    t.targetFixedFormat = "Fixed %s";
    t.rowPort = "Port";
    t.portDefaultHint = "Default for the output";
    t.portReset = "Default";
    t.rowLanguage = "Language";
    t.rowAutostart = "Start with SteamVR";
    t.hintAutostart = "From the next SteamVR start";
    t.autostartMissing = "Not installed";
    t.autostartUnknown = "Can't read the state";
    t.resetAll = "Reset all";
    t.resetConfirm = "Press again to reset";
    t.quit = "Quit app";
    t.quitConfirm = "Press again to quit";
    t.footer = "Changes apply at once and survive restarts. Sending goes on after you quit the panel";

    t.rowSmoothing = "Smoothing";
    t.hintSmoothing = "Off = send raw values";
    t.rowStrength = "Smoothness";
    t.strengthLight = "Light";
    t.strengthMedium = "Medium";
    t.strengthStrong = "Strong";
    t.custom = "Custom (values below)";
    t.rawOnNote = "Smoothing is off";
    t.rowFine = "Fine tune";
    t.rowDeadzone = "Fixation deadzone";
    t.hintDeadzone = "Smaller wobbles are ignored";
    t.rowHold = "Hold gaze while blinking";
    t.hintHold = "Holds below this openness";
    t.rowIndependent = "Move eyes separately";
    t.hintIndependent = "Jittery on the Frame";

    t.rowCalibration = "Auto calibration";
    t.learnedFormat = "Learned L %s / R %s";
    t.notLearned = "Nothing learned yet";
    t.learning = "Learning";
    t.calibrationReset = "Reset";
    t.rowScale = "Eye scales";
    t.hintScaleAuto = "Auto = from calibration";
    t.hintScaleFixed = "Uses fixed scales";
    t.scaleAuto = "Auto";
    t.scaleFixed = "Fixed";
    t.marksTitle = "Blink and open wide, then move the lines to match (raw value after scaling)";
    t.markClosed = "Closed";
    t.markOpen = "Open";
    t.markWidenStart = "Widen start";
    t.markWide = "Widest";
    t.rowSync = "Sync both lids";
    t.hintSync = "0 = off. Winks pass through";
    t.rowLidSmooth = "Eyelid smoothing";

    t.rowPrefix = "Parameter prefix";
    t.prefixNone = "None";
    t.prefixExample = "e.g. ";
    t.prefixOther = "Now: %s";
    t.rowConfigPath = "Config file";
    t.configPathMismatch = "frameeyeosc reads another config file: ";
    t.rowCalibrationPath = "Calibration";
    t.rowStatusPath = "Status file";
    t.rowLockedList = "Locked by command";
    t.hintLockedList = "Set in FRAMEEYEOSC_ARGS";
    t.noneLocked = "None";
    t.rowCore = "frameeyeosc";
    t.coreFormat = "PID %d, up %s";
    t.notRunning = "Not running";
    t.hoursMinutesFormat = "%d h %d min";
    t.minutesFormat = "%d min";

    t.promptVrchat = "Use the recommended settings for VRChat?";
    t.promptEtvr = "Use the recommended settings for VRCFaceTracking (ETVR)?";
    t.promptVrchatDetail1 = "Gaze and eyelid smoothing go back to default";
    t.promptVrchatDetail2 = "";
    t.promptEtvrDetail1 = "Gaze smoothing goes to default, eyelid smoothing gets lighter";
    t.promptEtvrDetail2 = "(ETVR already smooths the eyelids)";
    t.promptYes = "Yes";
    t.promptNo = "No";
    return t;
}

const UiText kJapanese = makeJapanese();
const UiText kEnglish = makeEnglish();

}  // namespace

const UiText& uiText(Language language) {
    return language == Language::En ? kEnglish : kJapanese;
}

const char* languageCode(Language language) {
    return language == Language::En ? "en" : "ja";
}

bool parseLanguage(const std::string& code, Language& language) {
    if (code == "ja") {
        language = Language::Ja;
        return true;
    }
    if (code == "en") {
        language = Language::En;
        return true;
    }
    return false;
}
