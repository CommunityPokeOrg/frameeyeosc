// The text tables.
#include "i18n.h"

#include <cstdlib>
#include <fstream>

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
    t.tabGazeFit = "目を合わせる";
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
    t.rowQuality = "不確かな視線を使わない";
    t.hintQuality = "この値より不確かな目は無視";
    t.rowDespike = "一瞬の途切れを消す";
    t.hintDespike = "視線とまぶた。約 11 ms 遅れる";

    t.rowFit = "目を合わせる";
    t.hintFit = "視線とまぶた・約 30 秒";
    t.fitStart = "目を合わせる";
    t.fitAgain = "もう一度合わせる";
    t.fitCenterOnly = "正面だけ合わせ直す";
    t.fitStop = "やめる";
    t.fitIntro = "ダッシュボードを閉じると始まります。頭は動かさず、点を目で追ってください。最後に 3 秒目を閉じます";
    t.fitNeedsRunning = "frameeyeosc が動いているときに使えます";
    t.fitLocked = "視線かまぶたの値がコマンドで固定されているので使えません";
    t.fitWaiting = "ダッシュボードを閉じると始まります";
    t.fitHowTo = "頭は動かさず、点を目で追ってください。最後に 3 秒目を閉じます。ダッシュボードを開くと止まります";
    t.fitWaitingCenter = "正面に点が出ます。頭は動かさず見てください。ダッシュボードを開くと止まります";
    t.fitRunningFormat = "測っています: %s（%d / %d）";
    t.fitRetryFormat = "・%d 回目";
    t.fitDone = "合わせました";
    t.fitDoneCenter = "正面を合わせ直しました";
    t.fitFitted = "合わせてあります";
    t.fitNotYet = "まだ合わせていません";
    t.fitGazeCenterFormat = "視線の正面: 左右 %s・上下 %s";
    t.fitGazeRangeFormat = "視線の幅: 左右 %s・上 %s・下 %s";
    t.fitGazeNone = "視線: 合わせていません";
    t.fitLidFormat = "まぶた %s: 開 %s・閉 %s・下を見ると %s";
    t.fitLidsNone = "まぶた: 合わせていません（自動で覚えています）";
    t.fitFailed = "合わせられませんでした";
    t.failCancelled = "止めました（ダッシュボードを開くと止まります）";
    t.failWaitTimedOut = "1 分のうちにダッシュボードが閉じられませんでした";
    t.failNotRunning = "frameeyeosc が動いていません";
    t.failNoResult = "frameeyeosc から結果が届きませんでした";
    t.failUnsteadyFormat = "%s の点で視線が落ち着きませんでした（目を閉じていたかも）";
    t.failNotClosed = "目を閉じているのが測れませんでした（3 回）";
    t.failNoMovementFormat = "%s の点で視線がほとんど動きませんでした";
    t.failLidRange = "まぶたの開け閉めの差が小さすぎました";
    t.failWrite = "設定ファイルに書けませんでした";
    t.pointCenter = "正面";
    t.pointUp = "上";
    t.pointDown = "下";
    t.pointLeft = "左";
    t.pointRight = "右";
    t.pointClosed = "目を閉じる";
    t.targetClose = "目を閉じて";
    t.targetKeepClosed = "閉じたまま";
    t.targetOpen = "開けて OK";
    t.fitReset = "元に戻す";
    t.fitDetails = "細かく直す";
    t.rowOffset = "正面の位置";
    t.hintOffset = "＋ は右・上";
    t.rowGain = "動く幅";
    t.hintGain = "大きいほどよく動く";
    t.capLeftRight = "左右";
    t.capUpDown = "上下";
    t.capUp = "上";
    t.capDown = "下";
    t.rowDownHold = "真下で左右を止める";
    t.hintDownHold = "0 = オフ";
    t.downHoldFormat = "%s° より下";
    t.rowLidFit = "まぶたの読んだ値";
    t.hintLidFit = "Frame の生の開き具合";
    t.capClosed = "閉じ";
    t.capAhead = "正面";
    t.lidFitInUse = "目を合わせた値を使っています";

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
    t.rowBlink = "まばたきを届ける";
    t.hintBlink = "閉じたまま保つ時間・両目で閉じる";
    t.blinkHold = "保持";
    t.blinkSync = "両目";
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

    t.rowVersion = "バージョン";
    t.checkedFormat = "・確認 %s";
    t.rowUpdateCheck = "新しい版の確認";
    t.hintUpdateCheck = "起動時と 1 日 1 回、GitHub に新しい版がないか見に行きます";
    t.updateUpToDateFormat = "最新版です（%s）";
    t.updateChecking = "新しい版を確かめています…";
    t.updateAvailableFormat = "新しい版 %s があります";
    t.updateButton = "更新する";
    t.updateManual = "ここからは入れられない版です。GitHub から手で更新してね";
    t.updateConfirmFormat = "%s に更新しますか？";
    t.updateConfirmHint = "ダウンロードして入れ替えます。途中でこの画面が閉じて開き直すことがあります";
    t.updateConfirmYes = "更新する";
    t.updateConfirmNo = "やめる";
    t.updateInstallingFormat = "更新中: %s";
    t.updateInstalledFormat = "%s を入れました。開き直すと新しい版になります";
    t.updateInstallFailed = "更新できませんでした（今の版のままです）:";
    t.updateCheckFailed = "新しい版を確かめられませんでした:";
    t.updateCheckNow = "今すぐ確かめる";
    t.updateRetry = "もう一度";
    t.updateDismiss = "閉じる";
    t.updateLogHint = "くわしくは ~/.cache/frameeyeosc/update.log";
    t.stepStart = "準備中";
    t.stepDownload = "ダウンロード中";
    t.stepVerify = "ファイルを確認中";
    t.stepExtract = "展開中";
    t.stepInstall = "入れ替え中";
    t.reasonNetwork = "GitHub につながりません";
    t.reasonRateLimited = "GitHub の回数制限にかかりました。1 時間ほどあとで試してね";
    t.reasonNotFound = "公開されている版がありません";
    t.reasonBadResponse = "GitHub の返事を読めませんでした";
    t.reasonBadVersion = "版の番号を読めませんでした";
    t.reasonBadUrl = "GitHub 以外の場所へ向かったので止めました";
    t.reasonMissingTool = "必要なコマンド（python3）がありません";
    t.reasonNoChecksums = "この版には確認用の SHA256SUMS がありません。手で更新してね";
    t.reasonNoAsset = "この版には入れるファイルがありません";
    t.reasonChecksumMismatch = "ダウンロードしたファイルが壊れています";
    t.reasonUnsafeArchive = "ファイルの中身が安全でないので止めました";
    t.reasonNoInstaller = "ファイルに install.sh がありません";
    t.reasonInstallFailed = "install.sh が失敗しました";
    t.reasonBadArgs = "前回のインストールのオプションを読めません";
    t.reasonBusy = "別の更新が動いています";
    t.reasonNotNewer = "もう最新版です";
    t.reasonDetachFailed = "更新を始められませんでした（systemd-run）";
    t.reasonInterrupted = "更新が途中で止まりました";
    t.reasonIo = "ファイルを書けませんでした";
    t.reasonUpdater = "更新の仕組みが動きませんでした";
    t.reasonOther = "うまくいきませんでした";
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
    t.tabGazeFit = "Eye fit";
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
    t.rowQuality = "Skip unreliable gaze";
    t.hintQuality = "Ignores an eye less sure than this";
    t.rowDespike = "Remove glitches";
    t.hintDespike = "Gaze and lids, ~11 ms later";

    t.rowFit = "Fit your eyes";
    t.hintFit = "Gaze and eyelids, about 30 s";
    t.fitStart = "Fit my eyes";
    t.fitAgain = "Fit again";
    t.fitCenterOnly = "Re-center only";
    t.fitStop = "Stop";
    t.fitIntro = "It starts when you close the dashboard. Keep your head still and follow the dot with your eyes. "
                 "At the end, close your eyes for 3 seconds.";
    t.fitNeedsRunning = "Works while frameeyeosc is running";
    t.fitLocked = "Not available: gaze or eyelid values are locked by the command line";
    t.fitWaiting = "Close the dashboard to start";
    t.fitHowTo = "Keep your head still and follow the dot with your eyes. At the end, close your eyes for 3 seconds. "
                 "Opening the dashboard stops it.";
    t.fitWaitingCenter = "A dot appears straight ahead. Keep your head still and look at it. Opening the dashboard "
                         "stops it.";
    t.fitRunningFormat = "Measuring: %s (%d of %d)";
    t.fitRetryFormat = ", try %d";
    t.fitDone = "Fitted";
    t.fitDoneCenter = "Re-centered";
    t.fitFitted = "Fitted";
    t.fitNotYet = "Not fitted yet";
    t.fitGazeCenterFormat = "Gaze center: L-R %s, U-D %s";
    t.fitGazeRangeFormat = "Gaze range: L-R %s, up %s, down %s";
    t.fitGazeNone = "Gaze: not fitted";
    t.fitLidFormat = "Eyelid %s: open %s, closed %s, looking down %s";
    t.fitLidsNone = "Eyelids: not fitted (learned automatically)";
    t.fitFailed = "Could not fit";
    t.failCancelled = "Stopped (opening the dashboard stops it)";
    t.failWaitTimedOut = "The dashboard wasn't closed within a minute";
    t.failNotRunning = "frameeyeosc is not running";
    t.failNoResult = "No answer from frameeyeosc";
    t.failUnsteadyFormat = "The gaze wasn't steady at the %s dot (eyes closed?)";
    t.failNotClosed = "Couldn't measure your eyes closed (3 tries)";
    t.failNoMovementFormat = "The gaze hardly moved toward the %s dot";
    t.failLidRange = "The eyelids barely changed between open and closed";
    t.failWrite = "Couldn't write the settings file";
    t.pointCenter = "center";
    t.pointUp = "up";
    t.pointDown = "down";
    t.pointLeft = "left";
    t.pointRight = "right";
    t.pointClosed = "eyes closed";
    t.targetClose = "Close your eyes";
    t.targetKeepClosed = "Keep them closed";
    t.targetOpen = "Open them";
    t.fitReset = "Reset";
    t.fitDetails = "Fine-tune";
    t.rowOffset = "Straight ahead";
    t.hintOffset = "+ is right / up";
    t.rowGain = "Range";
    t.hintGain = "Higher moves further";
    t.capLeftRight = "Left-right";
    t.capUpDown = "Up-down";
    t.capUp = "Up";
    t.capDown = "Down";
    t.rowDownHold = "Hold sideways far down";
    t.hintDownHold = "0 = off";
    t.downHoldFormat = "Below %s°";
    t.rowLidFit = "Eyelid readings";
    t.hintLidFit = "Frame openness";
    t.capClosed = "Closed";
    t.capAhead = "Ahead";
    t.lidFitInUse = "Using the eye fit";

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
    t.rowBlink = "Make blinks visible";
    t.hintBlink = "Hold closed, close both eyes";
    t.blinkHold = "Hold";
    t.blinkSync = "Both";
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

    t.rowVersion = "Version";
    t.checkedFormat = " · checked %s";
    t.rowUpdateCheck = "Check for updates";
    t.hintUpdateCheck = "Looks on GitHub for a new version at start and once a day";
    t.updateUpToDateFormat = "Up to date (%s)";
    t.updateChecking = "Checking for updates…";
    t.updateAvailableFormat = "Version %s is available";
    t.updateButton = "Update";
    t.updateManual = "This version can't be installed from here. Update by hand from GitHub";
    t.updateConfirmFormat = "Update to %s?";
    t.updateConfirmHint = "It downloads and installs the new version. This panel may close and reopen meanwhile";
    t.updateConfirmYes = "Update";
    t.updateConfirmNo = "Cancel";
    t.updateInstallingFormat = "Updating: %s";
    t.updateInstalledFormat = "%s is installed. Reopen to use it";
    t.updateInstallFailed = "The update failed (nothing was changed):";
    t.updateCheckFailed = "Couldn't check for updates:";
    t.updateCheckNow = "Check now";
    t.updateRetry = "Try again";
    t.updateDismiss = "Close";
    t.updateLogHint = "Details: ~/.cache/frameeyeosc/update.log";
    t.stepStart = "Preparing";
    t.stepDownload = "Downloading";
    t.stepVerify = "Verifying";
    t.stepExtract = "Unpacking";
    t.stepInstall = "Installing";
    t.reasonNetwork = "Can't reach GitHub";
    t.reasonRateLimited = "GitHub's rate limit was hit. Try again in an hour";
    t.reasonNotFound = "No published release";
    t.reasonBadResponse = "Couldn't read GitHub's answer";
    t.reasonBadVersion = "Couldn't read the version number";
    t.reasonBadUrl = "Stopped: the download led outside GitHub";
    t.reasonMissingTool = "A required command (python3) is missing";
    t.reasonNoChecksums = "This release has no SHA256SUMS. Update by hand";
    t.reasonNoAsset = "This release has no file to install";
    t.reasonChecksumMismatch = "The download is corrupt (checksum mismatch)";
    t.reasonUnsafeArchive = "Stopped: the archive has unsafe contents";
    t.reasonNoInstaller = "The archive has no install.sh";
    t.reasonInstallFailed = "install.sh failed";
    t.reasonBadArgs = "The saved install options are invalid";
    t.reasonBusy = "Another update is running";
    t.reasonNotNewer = "Already up to date";
    t.reasonDetachFailed = "Couldn't start the update (systemd-run)";
    t.reasonInterrupted = "The update was interrupted";
    t.reasonIo = "Couldn't write files";
    t.reasonUpdater = "The updater didn't run";
    t.reasonOther = "Something went wrong";
    return t;
}

const UiText kJapanese = makeJapanese();
const UiText kEnglish = makeEnglish();

}  // namespace

namespace {

/**
 * Read Steam's language setting: the first "language" value in ~/.steam/registry.vdf ("japanese" and so on).
 * @return the value, or empty if it can't be read
 */
std::string steamLanguage() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return "";
    std::ifstream file(std::string(home) + "/.steam/registry.vdf");
    std::string line;
    while (std::getline(file, line)) {
        // Format: <tab>"language"<tab>"japanese"
        const std::string key = "\"language\"";
        const size_t at = line.find(key);
        if (at == std::string::npos) continue;
        const size_t open = line.find('"', at + key.size());
        const size_t close = open == std::string::npos ? open : line.find('"', open + 1);
        if (close == std::string::npos) return "";
        return line.substr(open + 1, close - open - 1);
    }
    return "";
}

/**
 * Whether the locale (the first non-empty of LC_ALL, LC_MESSAGES, LANG) is Japanese.
 * @return true if Japanese
 */
bool localeIsJapanese() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') return std::string(value).rfind("ja", 0) == 0;
    }
    return false;
}

/**
 * Look up the system language (the body of systemLanguage).
 * @return the language
 */
Language detectSystemLanguage() {
    const std::string steam = steamLanguage();
    if (!steam.empty()) return steam == "japanese" ? Language::Ja : Language::En;
    return localeIsJapanese() ? Language::Ja : Language::En;
}

}  // namespace

Language systemLanguage() {
    static const Language cached = detectSystemLanguage();
    return cached;
}

const UiText& uiText(Language language) {
    return language == Language::En ? kEnglish : kJapanese;
}

const char* updateStepText(const UiText& t, const std::string& step) {
    if (step == "download") return t.stepDownload;
    if (step == "verify") return t.stepVerify;
    if (step == "extract") return t.stepExtract;
    if (step == "install") return t.stepInstall;
    return t.stepStart;
}

const char* updateReasonText(const UiText& t, const std::string& code) {
    static const struct {
        const char* code;
        const char* UiText::*text;
    } kReasons[] = {
        {"network", &UiText::reasonNetwork},
        {"rate-limited", &UiText::reasonRateLimited},
        {"not-found", &UiText::reasonNotFound},
        {"bad-response", &UiText::reasonBadResponse},
        {"bad-version", &UiText::reasonBadVersion},
        {"bad-url", &UiText::reasonBadUrl},
        {"missing-tool", &UiText::reasonMissingTool},
        {"no-checksums", &UiText::reasonNoChecksums},
        {"no-asset", &UiText::reasonNoAsset},
        {"checksum-mismatch", &UiText::reasonChecksumMismatch},
        {"unsafe-archive", &UiText::reasonUnsafeArchive},
        {"no-installer", &UiText::reasonNoInstaller},
        {"install-failed", &UiText::reasonInstallFailed},
        {"bad-args", &UiText::reasonBadArgs},
        {"busy", &UiText::reasonBusy},
        {"not-newer", &UiText::reasonNotNewer},
        {"detach-failed", &UiText::reasonDetachFailed},
        {"interrupted", &UiText::reasonInterrupted},
        {"io", &UiText::reasonIo},
        {"usage", &UiText::reasonUpdater},
        {"script-failed", &UiText::reasonUpdater},
        {"spawn-failed", &UiText::reasonUpdater},
    };
    for (const auto& reason : kReasons) {
        if (code == reason.code) return t.*reason.text;
    }
    return t.reasonOther;
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
