# frameeyeosc-panel

frameeyeosc の設定を、Steam Frame を被ったまま SteamVR のダッシュボードから変えるパネル（C++）。

- パネルがするのは、設定ファイル `config.json` を書き換えることと、状態ファイル `status.json` を読んで表示することだけ。送信は frameeyeosc 本体がする
- パネルを閉じても、落ちても、入れていなくても、本体はそのまま送り続ける
- 本体は設定ファイルを 1 秒ごとに見ていて、変わったら再起動なしで反映する

## 画面

ダッシュボードの下の並びに「Eye」のアイコン（目の絵）が出る。選ぶとパネル（1200×700 px、幅 2.8 m）が開き、レーザーポインターで押して操作する。

左の列は、どのタブでも出ている今の状態:

- バッジ: 「● 送信中」「○ 目のデータ待ち」「‖ 一時停止中」「✕ 本体が動いていません」
- 送り先（例: `VRChat → 192.168.0.60:9000`）と、自動（Steam Link の相手）か固定か
- 毎秒の送信回数
- まぶた: 左右それぞれ、細い灰色の棒が生の値（倍率を掛けた後）、太い色の棒が送った値
- 視線: 枠の中に、輪が生の値、塗った点が送った値。「左右の目を別々に動かす」がオンのときは「左目」「右目」の枠を 2 つ並べ、それぞれの目の生の値（輪、どちらも同じ色）と送った値（点、左は水色・右はオレンジ）、その下に送った x / y を出す。自分から見た向きのまま（左目が左、＋ x は右、＋ y は上）。状態ファイルの `raw` / `sent` の `gaze_left` / `gaze_right` を読む
- いちばん下に赤で 1 行（2 行まで）: パネルが設定を書けなかった、設定ファイルが壊れている、自動起動の切り替えに失敗、本体が報告した設定のエラー（この順で 1 つだけ）
- 赤の行が無いときは、新しい版があるあいだ・更新中・入れ終わったあと、いちばん下にお知らせ（「v0.4.1 があります」など）。押すと詳細タブへ

右はタブ:

| タブ | 項目 |
|---|---|
| 基本 | 送信（送る / 止める）、送り先（VRChat に直接 / VRCFaceTracking（ETVR））、送り先の PC（自動 / 今の相手で固定）、ポート（− / ＋、既定に戻す）、言語（日本語 / English）、SteamVR と一緒に起動（オン / オフ）、すべて既定に戻す、アプリを終了 |
| 視線 | スムージング（オン / オフ）、なめらかさ（弱 / 中 / 強）、細かく変える（止まっている時・速い動き・変化の感度の 3 つを − / ＋）、見つめている時の遊び（角度も表示）、まばたき中は視線を止める（オン / オフとしきい値）、左右の目を別々に動かす、不確かな視線を使わない（オン / オフと上限）、一瞬の途切れを消す（オン / オフ） |
| 目を合わせる | ［目を合わせる］（合わせたあとは［もう一度合わせる］）と小さい［正面だけ合わせ直す］、今どうなっているか（始め方・ダッシュボードを閉じると始まります［やめる］・測っています・結果［元に戻す］・失敗の理由）、［♪ 音を鳴らす: オン / オフ］、「細かく直す」（開閉。［視線 / まぶた］で切り替え。視線: 正面の位置・動く幅、真下で左右を止める角度、目ごとの左右の正面の位置・動く幅。まぶた: 目ごとの読んだ値 閉じ / 上 / 正面 / 下 の − / ＋） |
| まぶた | 自動キャリブレーション（オン / オフ、覚えた値、覚えている最中か、リセット）、左右の倍率（自動 / 固定と左右の − / ＋）、生の値の棒と 4 つの目盛りの線、目盛り ①閉じ ②普通 ③見開き始め ④見開き最大 の − / ＋、左右をそろえる強さ、まばたきを届ける（閉じたまま保つ ms と両目で閉じるしきい値の − / ＋）、まぶたのなめらかさ |
| 詳細 | 視線の点を表示（デバッグ用、オン / オフ）、パラメーター名の頭（/FT / なし、送るアドレスの例）、バージョン（今の版と最後に確かめた時刻、［今すぐ確かめる］／新しい版があれば［更新する］、確認を 1 回はさむ。更新に失敗したら［もう一度］［閉じる］）、新しい版の確認（オン / オフ）、設定ファイル・キャリブレーション・状態ファイルの場所、本体の PID と動いている時間、コマンドで固定中の項目と今の値 |

- まぶたタブの棒は、目を開け閉めしながら目盛りの線を今の値に合わせるためのもの。棒の範囲は 0〜1.2
- 目盛りは「閉じ < 普通 ≦ 見開き始め ≦ 見開き最大」の順を崩さないよう、− / ＋ で動ける範囲を制限している
- なめらかさの 弱 / 中 / 強 は、視線の 3 つの値の組み合わせ（中 = 本体の既定値）

| | 止まっている時（`gaze_min_cutoff`） | 速い動き（`gaze_beta`） | 変化の感度（`gaze_d_cutoff`） |
|---|---|---|---|
| 弱 | 1.0 | 1.5 | 1.0 |
| 中 | 0.4 | 0.8 | 0.5 |
| 強 | 0.2 | 0.4 | 0.3 |

操作の決まり:

- 押すと、設定ファイルを読み直す → その項目だけ変える → 書く。書いた結果はすぐ画面に出る
- 送り先の種類を変えると、ポートはその種類の既定（VRChat 9000、ETVR 8889）に戻り、「〜向けのおすすめ設定にする？ する / しない」を 1 回だけ聞く。「する」で変わるのは視線のなめらかさ 3 つとまぶたのなめらかさ 2 つだけ（VRChat = すべて既定値、ETVR = 視線は既定値・まぶたは 10.0 / 10.0。ETVR のモジュールがまぶたを自分でもなめらかにしているため）
- 「今の相手で固定」は、本体が今送っている相手の IP を `host` に書く（ポートはそのまま）。本体が動いていない、または相手が見つかっていないときは押せない
- 「すべて既定に戻す」と「アプリを終了」は、押すと 3 秒間「もう一度押すと〜」になり、その間にもう一度押したときだけ実行する
- 「すべて既定に戻す」は、言語・キャリブレーションのリセットの回数・パネルが知らない項目は残す
- 言語: `config.json` に `language` が無いときは、起動時に 1 回だけ `~/.steam/registry.vdf` の `language` を読み、`japanese` なら日本語、それ以外は英語（読めなければ `LC_ALL` / `LC_MESSAGES` / `LANG`）。こうして決めた言語はファイルに書かず、言語のボタンを押したときだけ保存する
- 「リセット」（キャリブレーション）は `calibration_reset` を 1 増やすだけ。覚え直すのは本体
- 選択状態は色だけで伝えない（✓・塗り・太字。タブは塗りと下向きの印）

## 本体との関係

| ファイル | 場所 | パネル |
|---|---|---|
| 設定 `config.json` | `$XDG_CONFIG_HOME/frameeyeosc/config.json`（無ければ `~/.config/frameeyeosc/config.json`） | 読んで書く（書くのはパネルだけ） |
| 状態 `status.json` | `$XDG_RUNTIME_DIR/frameeyeosc/status.json`（無ければ `/run/user/<uid>/frameeyeosc/status.json`） | 読むだけ |
| 更新 | `~/.local/share/frameeyeosc/frame-update.sh`、`~/.cache/frameeyeosc/`（`update-check.json`・`update-state.json`・`update.log`） | スクリプトを動かし、状態ファイルを読む |

- 書き方: 同じフォルダの `config.json.tmp` に書く → `fsync` → `rename`（→ フォルダも `fsync`）。読み込んだ JSON の中身を書き換えて書き戻すので、パネルが知らないキーも消えない。ファイルが無いときは、全部の項目を既定値で書いたファイルを作る
- 設定ファイルが壊れた JSON のときは、ふつうのボタンでは書かない（中身を失わないため）。「設定ファイルが壊れています」と赤で出る。「すべて既定に戻す」だけは、壊れたファイルを `config.json.broken` に写してから既定値で作り直す
- 状態ファイルが無い、`time` が 3 秒より古い、`pid` のプロセスがいない、のどれかなら「本体が動いていません」。それでも設定は変えられる（本体が起動したら反映される）
- 本体のコマンドラインで指定した項目（状態の `locked`）は、グレーにして鍵の印と「コマンドで固定中」を出し、値は状態の `effective` から出す。押せない
- 本体が別の設定ファイル（`--config`）を読んでいるときは、詳細タブに赤で出す
- 状態ファイルは、パネルが**開いている間だけ** 1 秒に 10 回読む。設定ファイルは同じときに更新時刻と大きさだけ見て、変わっていたら読み直す。表示に出る値（丸めた値）が変わったときだけ描き直す
- 閉じている間はどちらのファイルも読まず、描かない（更新の状態ファイルと、目を合わせている間は別。下を参照）
- 更新の確認だけは閉じている間も動く: 起動時と 1 時間ごとに `frame-update.sh check` を裏で動かす（GitHub に行くのは、スクリプトが覚えている答えが 24 時間より古いときだけ。前回失敗していたら 1 時間）。`update_check` が false なら動かさない。［今すぐ確かめる］は 24 時間を待たずに `--force` で確かめ、`update_check` が false でも使える
- ［更新する］→ 確認で「更新する」を押すと `frame-update.sh install --detach` を動かす。スクリプトが SHA256SUMS で確かめてから、新しい版の `install.sh` を前回と同じオプション（`~/.config/frameeyeosc/install-args`、無ければ `--with-panel`）で、systemd のユーザーユニット `frameeyeosc-update` の中で実行する。`install.sh` がパネルを再起動しても更新は続く。パネルは `update-state.json` を 0.5 秒ごとに読んで進み具合を出す。これはパネルが動いている間ずっと（閉じている間も）続く
- 目を合わせる（`gaze_fit.{h,cpp}`）: ［目を合わせる］か［正面だけ合わせ直す］で始め、「ダッシュボードを閉じると始まります」と出して待つ（1 分まで、［やめる］で中止）。`IsDashboardVisible` でダッシュボードが閉じたのを見たら、頭に固定した目印（`target.cpp` の絵、別のオーバーレイ `sasaken.frameeyeosc-panel.target`）を出す。点は正面・上・下・左・右の順で、点から点へ 0.35 秒でなめらかに動き、1 秒後に `config.json` の `gaze_capture` に `{"id": 次の番号, "target": "center"}` などを書く。本体が状態ファイルの `gaze_capture` で同じ id の `done` を返したら次へ。サンプルが 45 未満かばらつきが 0.06 を超えたら同じ点を 3 回までやり直す。5 点が終わったら視線の計算を確かめてから、目印に「目を閉じて 3・2・1」を出して `target: "closed"` を頼み（目を閉じたサンプルも使い、視線は使わない）、左右とも正面の開き具合の 7 割より下なら「開けて OK」を 1.5 秒出して終わり。8 秒答えが無い・本体が止まった・ダッシュボードが開いた・点がほとんど動かなかった・目が閉じなかった・まぶたの開け閉めの差が 0.1 未満だったら止めて理由を出す。終わったら正面の位置・幅・目ごとの左右（`gaze_offset_x_left/right`・`gaze_gain_x_left/right`）・目ごとのまぶたの 4 つの値を書く（正面だけのときは正面の位置と目ごとの左右の 0 点だけ）。目ごとの左右は、HMD の `Prop_UserIpdMeters_Float`（読めないか 45〜85 mm の外なら 63 mm）から、2 m 先の点をその目から見た角度 atan2(2·sinθ ± IPD/2, 2·cosθ)（左目 +、右目 −、+ は右）に合わせる
- 視線の点（デバッグ用、`gaze_dots.{h,cpp}`）: `gaze_debug_dots` がオンの間、本体は処理したサンプルごとに送っている視線を 40 バイトのデータグラム（`FEOD`・版 1・フラグ・時刻・視線 6 つ、リトルエンディアン。`src/dots.rs`）で状態ファイルのフォルダの `gaze-dots.sock` に送る。パネルはそこに Unix データグラムソケットを開き（古いファイルは消してから）、届いた最新の 1 つで点を置く。まとめた視線なら 1 つ（明るい灰色）、`independent_eyes` なら目ごとに 2 つ（左は水色、右はオレンジ。明るさも違う）で、それぞれの目の位置（IPD の半分ずつ左右）からその目の向きに 2 m 先。点は別々のオーバーレイ（`sasaken.frameeyeosc-panel.dot0/1`、幅 3.5 cm）で、絵は 1 回だけ送り、あとは位置（transform）だけ動かす。オンの間は 1/90 秒おきに回る。1 秒届かなければ隠す。目を合わせている間とオフにしたときはソケットを閉じて点を隠す
- 目合わせの音（`sounds.{h,cpp}`）: 起動時に 7 つの音を正弦波 / 三角波と短い立ち上がり・減衰で作り（48 kHz モノラル 16 bit、ピーク −14 dBFS）、`$XDG_RUNTIME_DIR/frameeyeosc/sounds/` に WAV で書く。鳴らす場面は目印の見た目に合わせる: 点がすべり終わって止まったら pop、測れたら pip、測り直しなら buzz、「目を閉じて」の 3・2・1 が変わるたびに tick、目を閉じた手順が終わって「開けて OK」になったら open、終わったら done、止まったら fail。`fit_sounds` が false なら鳴らさない
- 目を合わせている間だけは、ダッシュボードが閉じていても 1/90 秒おきに回って目印を毎回描き直し、状態ファイルを 1 秒に 10 回読む。目印 1 枚を描くのは約 0.4 ms（`--target-bench`）。目印を隠したとき、描いた回数と 1 秒あたりの回数をログに出す。終わるか止まったら目印を隠し、元の読み方に戻る
- 目印は `CreateOverlay`（ダッシュボードのオーバーレイではない）で最初に要るときに作り、`SetOverlayTransformTrackedDeviceRelative(HMD)` で 2 m 先、幅 0.3 m に置く（正面、上下 15°、左右 20°）。作ったあとは隠したまま残し、終了時にパネルと同じ順で片付ける
- 点の計算: 正面 = 正面の点の平均。幅 = 目標の角度（45° を 1 とした値）÷（その点の平均 − 正面の平均）、左右は両側の平均、0.5〜2 に丸める。点が目標の 1/4 も正しい向きに動かなかったら失敗にする（向きが逆のときもここで止まる）
- 仕組みは frame-updater の共通部品（`vendor/frame-updater/`、C++ は `update_check.{h,cpp}`）。ここでは書き換えない。画面の文言は `vendor/frame-updater/strings.md` のまま（版の行の見出し「バージョン」と「・確認 %s」だけはこのパネルのもの）

## ビルド（Frame 上）

必要なもの（SteamOS に入っている）: cmake、ninja、g++、pkg-config、cairo、freetype2、Vulkan のヘッダとローダー（`vulkan` の pkg-config）、SteamVR（`/opt/steamvr/bin/linuxarm64/libopenvr_api.so`）。
`openvr.h` は `third_party/openvr/` に同梱（OpenVR SDK 2.15.6、BSD-3-Clause。`third_party/openvr/LICENSE`）。

PC から送ってビルドする例（Git Bash で、このフォルダから）:

```sh
tar --exclude=build --exclude=out -cf - . | ssh steamos@<Frame の IP> 'mkdir -p ~/frameeyeosc-panel && tar -xf - -C ~/frameeyeosc-panel'
ssh steamos@<Frame の IP> 'cd ~/frameeyeosc-panel && cmake -G Ninja -S . -B build && ninja -C build'
```

実行ファイルは `build/frameeyeosc-panel`。OpenVR のライブラリの場所は rpath に入っているので、別の場所にコピーしても動く。`-Wall -Wextra` で警告ゼロ。

## 手動で動かす

```sh
./build/frameeyeosc-panel          # ダッシュボードにパネルを出して常駐（Ctrl+C で終了）
```

- SteamVR が起動していなければ 3 秒おきに待つ（SteamVR を勝手に起動はしない）
- SteamVR が終わる（`VREvent_Quit`）と静かに終わる（終了コード 0）
- すでに常駐しているときにもう一度起動すると、SteamVR にはつながず、常駐しているほうに SIGUSR1 を送ってすぐ終わる。常駐側はダッシュボードを開いてパネルを出す
- 常駐の見分けは `$XDG_RUNTIME_DIR/frameeyeosc-panel.lock` のロック（flock）と、そこに書いた PID

確認用のオプション（`--probe` 系以外は OpenVR なしで動く）:

```sh
./build/frameeyeosc-panel --print                 # 設定・状態・自動起動を、パネルが読んだとおりに表示
./build/frameeyeosc-panel --dump-png out/panel_2026-09-27_00-00-00.png --tab lids --language en
./build/frameeyeosc-panel --dump-png out/etvr_2026-09-27_00-00-00.png --fake-etvr --fake-prompt etvr
./build/frameeyeosc-panel --dump-png out/t_2026-09-27_00-00-00.png --config /tmp/t/config.json --click 883,148
./build/frameeyeosc-panel --thumbnail-png out/thumbnail_2026-09-27_00-00-00.png --thumbnail-size 256
./build/frameeyeosc-panel --dump-png out/update_2026-09-27_00-00-00.png --fake-update available --tab advanced
./build/frameeyeosc-panel --dump-png out/fit_2026-09-28_00-00-00.png --tab eyefit --fake-fit fitted --fit-details   # 目を合わせるタブの各状態
./build/frameeyeosc-panel --dump-png out/eyes_2026-09-28_00-00-00.png --tab gaze --fake-independent   # 左の列の視線を目ごとに
./build/frameeyeosc-panel --target-png out/target_2026-09-28_00-00-00.png --target-style close --target-seconds 2 --target-bench 900
./build/gaze-fit-test                            # 目合わせの計算と手順のテスト（OpenVR なし）
./build/gaze-dots-test                           # 視線の点のデータ・位置・ソケットのテスト（OpenVR なし）
./build/sounds-test                              # 目合わせの音の WAV と、どの場面でどの音かのテスト（鳴らさない）
./build/frameeyeosc-panel --play-sound open      # 音を 1 つ鳴らして聞く（pop / pip / buzz / tick / open / done / fail）
./build/frameeyeosc-panel --dot-png out/dot_2026-09-28_00-00-00.png --dot-kind left
./build/frameeyeosc-panel --version               # 版（Cargo.toml から）
./build/frameeyeosc-panel --contrast-report       # 色の組み合わせごとのコントラスト比と合否
./build/frameeyeosc-panel --probe                 # 常駐しているパネルを SteamVR 経由で探して状態を出す
./build/frameeyeosc-panel --probe-switch-away 3   # ダッシュボードを一時的な別のオーバーレイに切り替える（閉じたときの確認用）
```

- `--dump-png` は今の設定ファイルと状態ファイルで描く。`--config PATH`・`--status PATH` で別のファイルを読める
- `--fake` か `--fake-*` を付けると、ファイルを読まずに作り物の状態で描く: `--fake-not-running`・`--fake-paused`・`--fake-no-tracking`・`--fake-etvr`・`--fake-fixed`・`--fake-target-null`・`--fake-locked`・`--fake-config-error`・`--fake-broken`・`--fake-write-error`・`--fake-custom`・`--fake-prompt vrchat|etvr`・`--fake-autostart on|off|missing|unknown`。`--preview-quit`・`--preview-reset` で「もう一度押すと〜」の見た目
- 更新の見た目は `--fake-update checking|uptodate|available|manual|installing|installed|checkfailed|installfailed`。`--preview-update-prompt`（`--fake-update available` と一緒に）で更新の確認
- `--update-live` を付けると本物の更新の仕組みを動かす: 最初に確認し、`--click` のあとは始まった確認や更新が終わるまで待ってから描く。更新は本当に行われるので、偽の GitHub（`FRAME_UPDATE_API_URL`・`FRAME_UPDATE_ALLOW_INSECURE=1`）と別の `HOME` で試す
- `--click X,Y`（何回でも）は、描く前にその座標を押したことにする。当たり判定と設定ファイルの書き込みをヘッドセットなしで確かめる用（`--fake` とは一緒に使えない。`--config` の設定ファイルを本当に書き換えるので、試すときは別の場所を指定する）
- `--probe` は Background 型でつなぐだけで、オーバーレイも Vulkan も作らない。`FindOverlay`・名前・幅・閉じるボタン・表示中か・`GetOverlayTextureSize` を出す
- `contrib/icons/frameeyeosc-panel-{48,128,256}.png` は `--thumbnail-png` で書き出したもの（ダッシュボードのサムネイルと同じ絵）

## ＋（プログラムを起動）から使う

Frame の Steam は XDG の `.desktop` を読んで「プログラムを起動」（＋）の一覧を作る。配布の tar.gz から入れるときは、トップの `./install.sh --with-panel` を使う（こちらは自動起動も有効にする）。ソースからビルドしたときは、Frame 上でこのフォルダから（sudo 不要。自動起動は有効にしない）:

```sh
sh contrib/install-panel.sh
```

入るもの:

- `~/.local/bin/frameeyeosc-panel`（systemd のサービスもこれを使う）
- `~/.local/share/applications/frameeyeosc-panel.desktop`（`Exec` を実行ファイルの絶対パスにしたもの）
- `~/.local/share/icons/hicolor/{48x48,128x128,256x256}/apps/frameeyeosc-panel.png`
- `~/.config/systemd/user/frameeyeosc-panel.service`（置いて `daemon-reload` するだけ。enable はしない）

常駐を終わらせたいときは、ダッシュボードの「Eye」アイコンにホバーして「閉じる」、またはパネルの「アプリを終了」（どちらも終了コード 3）。

削除:

```sh
systemctl --user disable frameeyeosc-panel.service
rm -f ~/.local/bin/frameeyeosc-panel ~/.local/share/applications/frameeyeosc-panel.desktop ~/.config/systemd/user/frameeyeosc-panel.service
rm -f ~/.local/share/icons/hicolor/{48x48,128x128,256x256}/apps/frameeyeosc-panel.png
systemctl --user daemon-reload
```

## 自動起動（systemd ユーザーサービス）

パネルの「SteamVR と一緒に起動」で切り替える（先に `install-panel.sh` でユニットを入れておく。入っていなければグレーで「準備されていません」）。

- オン = `systemctl --user enable frameeyeosc-panel.service`、オフ = `disable`。`start` はしない（次に SteamVR が起動したときから効く）
- 今の状態は `systemctl --user is-enabled` で読む（パネルを開いた直後と、開いている間 5 秒おき）
- ユニットは `After`・`PartOf`・`WantedBy=steamvr.service`、`Restart=always`、`RestartPreventExitStatus=3`、`SuccessExitStatus=3`
- サービスから起動されたのに常駐がすでにいるとき（手で起動したものが残っているなど）は、SIGUSR1 を送らずに終了コード 3 で終わる（5 秒ごとにパネルが開き続けないように）。「サービスから起動された」は、`INVOCATION_ID` があり、かつ `/proc/self/cgroup` が `.../frameeyeosc-panel.service` のときだけとみなす（Frame の Steam 自体が `steam.service` で動いていて、＋から起動した子にも `INVOCATION_ID` が引き継がれるため）
- ログ: `journalctl --user -u frameeyeosc-panel -f`

## 色とアクセシビリティ

- 色は `src/theme.h` の 1 か所にまとめてある。`--contrast-report` が同じ定義から WCAG 2.x のコントラスト比を計算する
- 背景は GitHub ダーク系（`#0d1117` / `#161b22` / `#21262d` / `#30363d`）、アクセントは `#e27dfd`
- 文字は大きさによらず 4.5:1 以上、部品の枠・選択状態・図は 3:1 以上。押せないボタンの文字は WCAG では例外だが 3:1 を目安にする
- 「本体が動いていません」のバッジは、赤い文字だと 4.35:1 で足りないので、文字は白に近い色で ✕ だけ赤
- 目盛りの線は、色の棒の上でも暗い地の上でも見えるよう、明るい線の両側に暗い縁を付けている
- 2026-09-27 の結果: 35 組すべて合格。いちばん低いのは「コマンドで固定中の選択肢の枠」`#6e7681` / `#21262d` の 3.31:1

## 守っていること

- 書くのは設定ファイル（と、壊れていたときの `config.json.broken`）だけ。ほかに作るのは、視線の点がオンの間の `gaze-dots.sock`（状態ファイルのフォルダ、閉じるときに消す）だけ。アイトラッキングの共有メモリ・カメラ・GPIO・sysfs・`/persist` には触らない。sudo を使わない
- 外部コマンドは `systemctl --user` と `/bin/sh ~/.local/share/frameeyeosc/frame-update.sh`、目を合わせるときの音の `pw-play`（無ければ `paplay`、`aplay`。どれも無ければ鳴らさない）だけ。音は `posix_spawn` で出力を捨てて起動し、待たずにループで片付ける（同時に 2 つまで）。音量や PipeWire / WirePlumber の設定には触らない。どちらも固定の引数で呼び、コマンドの文字列を組み立ててシェルに渡すことはしない。`systemctl` は 2 秒（enable / disable は 5 秒）、更新の確認は 90 秒で終わらなければ SIGKILL、どの場合も `waitpid` で片付ける
- 更新のスクリプトが書くのは `~/.cache/frameeyeosc/` だけ。新しい版を入れるのは［更新する］を押して確認したときだけ
- `systemctl` はワーカースレッドで実行する（ポインターへの応答 33 ms おきを止めない）。SIGTERM・SIGINT・SIGUSR1 はメインスレッドで受ける
- パネルを閉じている間は描かない。読むのは更新の状態ファイル（`~/.cache/frameeyeosc/update-state.json`、1 秒に 2 回ほど）だけで、実行するコマンドは更新の確認（起動時と 1 時間ごと）だけ。ほかはイベントを 0.25 秒おきに見るだけ。例外は自分で始めた目合わせの間だけ（上を参照）
- 目合わせの目印は、ダッシュボードが閉じている間だけ出す。ダッシュボードが開いたら、その場で隠して止める

負荷（2026-09-27 に Frame で実測、`/proc/<pid>/stat` と `/proc/<pid>/io`）:

| 状態 | CPU | 読み込みの回数 |
|---|---|---|
| パネルを閉じている | 10 秒で 1 tick（10 ms） | 0 |
| パネルが開いていて、値が 1 秒に 10 回変わる | 10 秒で 45 tick（1 コアの約 4.5%） | 1 秒に約 24 回 |

## 画像の送り方と終了処理

- パネルとサムネイルの画像は Vulkan の `VkImage` を `IVROverlay::SetOverlayTexture` で渡す（`SetOverlayRaw` は差し替えのたびに画像が無い瞬間ができてちらつくので使わない）。画像は 2 枚を交互に使う
- 見えているときだけ、変化があったときだけ描く。つないだ直後に 1 枚入れておき、初めて選ばれたときに画像が無い瞬間を作らない
- 別のプロセスから `GetOverlayImageData` を呼ぶと、Vulkan のテクスチャが入ったオーバーレイでは呼んだ側が落ちることがあるので、`--probe` では `GetOverlayTextureSize` だけを使う

終了処理の順番（SIGTERM / SIGINT・`VREvent_Quit`・vrserver の消滅・「閉じる」・「アプリを終了」のどれでも同じ。各手順の結果はログに `[VR] shutdown: ...` と出る）:

1. `ClearOverlayTexture`（パネル・サムネイル）
2. `DestroyOverlay`（パネル。サムネイルは一緒に消える）
3. 400 ms 待つ（コンポジタに外したテクスチャを手放してもらう）
4. `VR_Shutdown`
5. Vulkan の画像・デバイス・インスタンスを壊す（OpenVR の決まりで `VR_Shutdown` の後）
6. 自動起動のワーカースレッドを止める

## ファイル

| ファイル | 役割 |
|---|---|
| `src/main.cpp` | コマンドライン、常駐のループ、ボタンの処理（設定ファイルへの書き込み）、二重起動、確認用オプション |
| `src/panel.*` | パネルの描画とボタンの当たり判定（描くたびに配置を作り直す）、サムネイル（目の絵） |
| `src/model.*` | 表示に使う値の組み立て（コマンドで固定中なら `effective`）、なめらかさの 3 段階、おすすめ設定、目盛りの順番 |
| `src/config.*` | `config.json` の項目の表（型・既定値・範囲・刻み）、読み書き（一時ファイル → fsync → rename） |
| `src/status.*` | `status.json` の読み込みと、本体が動いているかの判断 |
| `src/autostart.*` | `systemctl --user` で自動起動を読む・切り替えるワーカースレッド |
| `src/command.*` | fork＋execvp・パイプ・タイムアウト・waitpid |
| `src/theme.*` | 色の定義と WCAG のコントラスト比（`--contrast-report`） |
| `../vendor/frame-updater/` | 更新の確認・更新の共通部品（frame-updater の `sync.sh` で写したもの）。CMake が `cpp/update_check.cpp` を一緒にビルドする |
| `src/i18n.*` | 画面の文言（日本語・英語）。ログは英語 |
| `src/vr_overlay.*` | OpenVR の接続、ダッシュボードのオーバーレイ、イベント、終了処理、`--probe` |
| `src/vk_texture.*`・`src/draw.*`・`src/json.*` | Vulkan の画像、描画の部品、JSON の読み書き |
| `contrib/` | `.desktop`・`.service`・アイコン・`install-panel.sh` |
| `third_party/openvr/` | `openvr.h`（OpenVR SDK 2.15.6）と、そのライセンス（BSD-3-Clause） |
