# frameeyeosc

Steam Frame のアイトラッキング（視線とまぶたの開き具合）を、VRCFaceTracking 形式のアバターパラメータとして OSC で VRChat に送るツールです。ヘッドセット上でバックグラウンドのサービスとして動き、Steam Link でストリーミングしている PC 版 VRChat で使えます。PC の VRCFaceTracking に送って、ほかのトラッカーとまとめることもできます。

[English](README.md)

[konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc) をフォークしたものです。Steam Frame のまぶたのデータは公開 API からは取れないのですが、元のプロジェクトが内部の共有メモリ（`/dev/shm/eye-server.mmap`）から読めることを見つけてくれたおかげで、このツールを作ることができました。

## このフォークで足したもの

- 送り先の PC は自動で見つけます。Steam Link でつないでいる PC に送るので、アドレスを調べて設定する必要はありません。付属の無線アダプタを使っているときはその直通回線で送るので、家のネットワークの状態にも左右されません
- 視線とまぶたを One Euro フィルタでなめらかにしています。何かを見つめているときの細かい揺れは小さなデッドゾーンで止め、目を閉じている間は視線を固定します。Frame は目を開けた瞬間に視線が跳ねるためです
- 両目に同じ視線を送ります。Frame は左右の目の視線がそれぞれ勝手に揺れるので、そのまま送るとアバターの目がピクピクします。左右別々にしたいときは `--independent-eyes` を付けてください
- まぶたの値を VRCFT の基準（0 で閉じる、0.75 で普通、1 で見開き）に合わせています。Frame の値は、目を閉じ続けても 0.2 前後までしか下がらず、普通に開いているときも 0.75〜0.9 くらいでふらつきます
- まぶたは使っているうちに自動で調整されます。左右それぞれの普段の開き具合を覚えるので、顔や被り方のせいで片目だけ開いて見える人でも、アバターでは揃って見えます。ウインクはそのまま伝わります
- サービスとして常駐し、SteamVR と一緒に起動します。止まっても自動で再起動します
- 設定はファイルに置き、動いたまま反映します。SteamVR のダッシュボードに出すパネル（入れなくてもよい）で、被ったまま変えられます
- VRCFaceTracking 用の ETVR Tracking Module が読む形式でも送れます（[VRCFaceTracking（ETVR）モード](#vrcfacetrackingetvrモード)）

## 必要なもの

- 開発者モードを有効にして SSH で入れる Steam Frame（設定 → システム → 開発者モードを有効化、開発者の項目でパスワードを設定）。SSH を有効にすると、同じネットワークにいてパスワードを知っている人は誰でもヘッドセットに入れるので、推測されにくいパスワードにしてください
- Steam Link でストリーミングしている PC 版 VRChat（Action Menu → Options → OSC → Enabled）
- VRCFaceTracking の目のパラメータ（`FT/v2/EyeLeftX`、`EyeLidLeft` など）を float で持つアバター。VRChat に直接送るときは、パラメータをビットに詰める「バイナリパラメータ」のアバターには対応していません
- VRCFaceTracking（ETVR）モードで使うときは、PC に VRCFaceTracking と ETVR Tracking Module

## インストール

リリースページから tar.gz をダウンロードして、ヘッドセットにコピーします。PC からなら例えば:

```sh
scp frameeyeosc-*-steamframe-aarch64.tar.gz steamos@<ヘッドセットのIP>:
```

そのあとヘッドセット上で（`ssh steamos@<ヘッドセットのIP>`）:

```sh
tar xzf frameeyeosc-*-steamframe-aarch64.tar.gz
cd frameeyeosc
./install.sh               # frameeyeosc だけ
./install.sh --with-panel  # frameeyeosc とダッシュボードのパネル
```

sudo は要りません。全部ホームフォルダ（`~/.local/bin`、`~/.config`、`~/.local/share`）に入るので、SteamOS を更新しても消えません。更新するときも同じコマンドです。`--with-panel` を付けないときは、入っているパネルはそのまま残ります。

インストールしたら、PC 側で Steam Link の OSC 送信を OFF にしてください（SteamVR の設定 → Steam Link → OSC）。Steam Link もスムージングなしの目のデータを VRChat に送っているので、両方が動いているとアバターの目を2つのデータが取り合ってしまいます。ETVR モードでも同じです（アバターの目を動かすのが VRCFaceTracking になるだけです）。

### 0.2.0 から更新するとき

新しい tar.gz を上と同じ手順でコピーして広げ、`./install.sh --with-panel`（パネルがいらなければ `./install.sh`）を実行するだけです。`~/.config/frameeyeosc/env` と学習したまぶたの値はそのまま使われ、サービスも新しい版で起動し直します。

`env` の `FRAMEEYEOSC_ARGS` に書いたオプションは、今までどおり効きます。ただし、そこに書いた項目はパネルでは「コマンドで固定中」になって変えられません。パネルで変えたい項目は `env` から消して、`systemctl --user restart frameeyeosc` してください（値はパネルで設定し直します）。

削除は `./install.sh --uninstall`（パネルも消えます。設定と学習値も消すなら `--purge` を付ける）。

### パネルから更新する（0.4.0 から）

パネルの「詳細」に、入っている版が出ます。パネルは起動時と、その後 1 日 1 回まで、GitHub に新しい版がないか確かめます。「今すぐ確かめる」を押すとその場で確かめます。新しい版があれば「更新する」で、ダウンロードしてリリースの `SHA256SUMS` と照らし合わせ、前回と同じオプション（`~/.config/frameeyeosc/install-args` に残っています）でその `install.sh` を実行します。本体とパネルは新しい版で起動し直します。`install.sh` を実行する前に失敗したときは何も変わりません。ログは `~/.cache/frameeyeosc/update.log` です。毎日の確認は「新しい版の確認」をオフにすると止まります（「今すぐ確かめる」は使えます）。更新そのものは、ボタンを押したときにしか行いません。

## パネル

`./install.sh --with-panel` で、SteamVR のダッシュボードに「Eye」のパネルが入ります。次に SteamVR を起動したときから一緒に起動します。すぐ開きたいときは、ダッシュボードの「プログラムを起動」（＋）から「frameeyeosc パネル」を選んでください。

- 左の列には、いつでも今の状態が出ます: 送信中か止めているか、送り先、毎秒の送信回数、左右のまぶたと視線（生の値と送った値）、設定のエラー
- 基本: 送信の一時停止、VRChat か VRCFaceTracking（ETVR）か、送り先の PC（自動か、今送っている PC で固定。VR の中で IP を打たなくて済みます）、ポート、言語（日本語 / English）、SteamVR と一緒に起動、すべて既定に戻す、アプリを終了
- 視線: スムージングのオン / オフ、なめらかさの弱 / 中 / 強と 3 つの値、見つめている時の遊び、まばたき中は視線を止める、左右の目を別々に動かす、不確かな視線を使わない、一瞬の途切れを消す
- まぶた: 自動キャリブレーションと覚えた値、左右の倍率、左右の今の開き具合の上に重ねた 4 つの目盛り（目を閉じたり見開いたりしながら合わせる）、左右をそろえる強さ、まばたきを届ける（閉じたまま保つ時間・両目で閉じる）、まぶたのなめらかさ
- 詳細: パラメーター名の頭、版の表示と更新の確認・更新、ファイルの場所、コマンドで固定中の項目

パネルがするのは `config.json` を書くことと状態ファイルを読むことだけです。既定の言語を決めるために、起動時に 1 回だけ Steam の `~/.steam/registry.vdf` の `language` の行も読みます（読むだけ）。更新には `~/.local/share/frameeyeosc/frame-update.sh` を使います（上を参照）。閉じても、終了しても、入れていなくても frameeyeosc は送り続けます。ダッシュボードで開いていない間は、更新の確認のほかは、何も読まず、何も描きません。「SteamVR と一緒に起動」は、パネルの systemd ユーザーサービス（`frameeyeosc-panel.service`）を有効 / 無効にします。ビルド方法や確認用のオプションは [panel/README.md](panel/README.md) にあります。

## 設定

設定は `~/.config/frameeyeosc/config.json` にあります。パネルが書きますが、手で書いてもかまいません。frameeyeosc は 1 秒ごとにこのファイルを見て、変わっていたら再起動せずに反映します。書いていない項目は既定値、知らない項目は無視します。ファイルが壊れていたり値が範囲外だったりしたときは、前の設定のまま動き続け、エラーを出します（パネルと状態ファイルに出ます）。

```json
{ "output": "vrchat", "gaze_min_cutoff": 0.3, "lid_sync": 0.6 }
```

| キー | オプション | 既定値 | 内容 |
|---|---|---|---|
| `sending` | | `true` | `false` で送信を一時停止（VRChat モードでは `EyeTrackingActive=false` を 1 回送る） |
| `output` | `--output` | `"vrchat"` | `"vrchat"` は VRChat にアバターパラメータを送る、`"etvr"` は VRCFaceTracking の ETVR Tracking Module に送る |
| `host` | `--target` | `"auto"` | `"auto"` は Steam Link の接続先 PC。それ以外は IP アドレスかホスト名（ポートは付けない） |
| `port` | `--port`、`--target` | `null` | `null` は `vrchat` なら 9000、`etvr` なら 8889 |
| `prefix` | `--prefix` | `"/FT"` | パラメータ名の頭。`""` で頭なし |
| `raw` | `--raw` | `false` | スムージングしない。時間を使う処理（途切れ消し、視線を止める、品質チェック、閉じたまま保つ）もしない |
| `gaze_min_cutoff` | `--gaze-min-cutoff` | `0.4` | 下げるほど止まっている時の視線が安定（その分遅れる） |
| `gaze_beta` | `--gaze-beta` | `0.8` | 上げるほど素早い視線の動きに遅れず付いていく |
| `gaze_d_cutoff` | `--gaze-d-cutoff` | `0.5` | 下げるほど、トラッキングのノイズで視線のフィルタがゆるみにくい |
| `gaze_deadzone` | `--gaze-deadzone` | `0.03` | これより小さい視線の変化は無視（1.0＝45°） |
| `gaze_hold_below` | `--gaze-hold-below` | `0.5` | どちらかの目の開き具合がこれより小さい間は視線を止める。`0` で無効 |
| `independent_eyes` | `--independent-eyes` | `false` | 共通の視線ではなく、左右それぞれの視線を送る |
| `gaze_quality_limit` | `--gaze-quality-limit` | `0`（オフ） | 念のための安全策: Frame が出す視線の不確かさ（共分散）がこれ（例: `0.03`）より大きい目の視線は使わない。片目だけならもう片方の目で両目を動かし、両目ともなら視線を止める。まぶたには影響しない。きちんと合ったヘッドセットでは測って差が出なかった。不確かさが上がるのはほぼ目を閉じかけている間だけで、そこは `gaze_hold_below` がもう視線を止めているため |
| `despike` | `--no-despike` | `true` | 視線と開き具合の 1 サンプルだけの途切れを消す（3 サンプルの中央値。全体が約 11 ms 遅れる） |
| `lid_min_cutoff` / `lid_beta` | `--lid-min-cutoff` / `--lid-beta` | `6.0` / `5.0` | まぶたのなめらかさ（視線と同じ考え方） |
| `lid_closed` / `lid_open` / `lid_widen_start` / `lid_wide` | `--lid-closed` など | `0.30` / `0.80` / `0.92` / `1.00` | Frame の開き具合を「閉じ／普通／見開き」にどう対応させるか |
| `lid_scale_left` / `lid_scale_right` | `--lid-scale-left` / `--lid-scale-right` | `null`（学習値） | 学習値の代わりに固定の倍率を使う |
| `lid_calibration` | `--no-lid-calibration` | `true` | まぶたを学習する |
| `lid_sync` | `--lid-sync` | `0.4` | 左右のまぶたの小さな差を揃える。大きな差（ウインク）はそのまま。`0` で無効 |
| `blink_hold_ms` | `--blink-hold-ms` | `80` | 目が閉じたら、少なくともこの時間は完全に閉じた値を送る。短いまばたきもほかの人に届くように。`0` で無効 |
| `blink_sync_below` | `--blink-sync-below` | `0.35` | 片目が閉じていて、もう片方がこれより小さい（VRCFT の値）とき、両目とも閉じて送る。もう片方が開いているウインクはそのまま。`0` で無効 |
| `calibration_reset` | | `0` | 増やすと、まぶたの学習をやり直す |
| `language` | | Steam の言語 | パネルの言語。`"ja"` か `"en"`。書いていないときは、Steam の言語が日本語なら日本語、それ以外なら英語 |
| `update_check` | | `true` | パネルが起動時と 1 日 1 回、GitHub に新しい版がないか確かめる。frameeyeosc 本体は使わない |

コマンドラインのオプションは、このファイルより優先されます。オプションは `~/.config/frameeyeosc/env` に書き、`systemctl --user restart frameeyeosc` で反映します:

```sh
FRAMEEYEOSC_ARGS="--gaze-min-cutoff 0.3 --lid-sync 0.6"
```

ここで指定した項目はファイルからは変えられず、パネルでは「コマンドで固定中」と出ます。すべてのオプション（別の設定ファイルを使う `--config` など）は `~/.local/bin/frameeyeosc --help` で確認できます。

## 状態ファイル

frameeyeosc は 1 秒に 10 回、今の様子を `$XDG_RUNTIME_DIR/frameeyeosc/status.json`（ふつうは `/run/user/1000/frameeyeosc/status.json`）に書きます。中身は、送信中か、送り先、毎秒の送信回数、最新の生の値と送った値、キャリブレーション、今効いている設定、コマンドで固定中の項目、設定のエラーです。パネルはこれを読んで表示します。フォルダは本人しか読めず、メモリの上にあって再起動すると消えます。残るのは最新の値だけです。

## VRCFaceTracking（ETVR）モード

frameeyeosc は、VRCFaceTracking 用の ETVR Tracking Module が読む形式で送れます。アバターを動かすのは VRCFaceTracking になるので、口のトラッカーなど、ほかのトラッカーと 1 つにまとめられます。ETVR Tracking Module は別のプロジェクトのモジュール（[EyeTrackVR/ETVRTrackingModule](https://github.com/EyeTrackVR/ETVRTrackingModule)）で、frameeyeosc はその一部ではありません。

1. PC に VRCFaceTracking を入れ、モジュールの一覧から ETVR Tracking Module を追加します。既定では UDP 8889 番で受けます
2. パネルで送り先を「VRCFaceTracking（ETVR）」にするか、`"output": "etvr"`（または `--output etvr`）にします。送り先の PC はいつもどおり（Steam Link の相手か固定）、ポートは 8889 です

注意:

- 送るのは `EyeLeftX`・`EyeLeftY`・`EyeRightX`・`EyeRightY`・`EyeLidLeft`・`EyeLidRight` の 6 個です。`EyeX` / `EyeY` は送りません。これを受け取るとモジュールが片目用の読み方に切り替わり、送っていないまぶたの値を読むので、まぶたが開いたまま動かなくなるためです
- モジュールはまぶたの 1.0 を「普通に開いた目」として扱うので、このモードでは見開きは伝わりません（1.0 で止めます）
- モジュールもまぶたを自分でなめらかにしています。パネルで切り替えると、frameeyeosc 側のまぶたのなめらかさを弱めるか聞かれます。そのなめらかさのせいで、`blink_hold_ms` の間閉じて送ってもアバターでは閉じきらないことがあります。短いまばたきが半目に見えるときは、120 くらいに上げてください
- VRCFaceTracking を起動してからモジュールの準備ができるまで、2 分近くウィンドウが「応答なし」になることがあります。壊れてはいないので、そのまま待ってください
- PC で UDP 8889 番の受信が許可されている必要があります。VRCFaceTracking の ModuleProcess には、たいてい最初から受信の許可が入っています

## キャリブレーション

まぶたのキャリブレーションは自動です。被ってから20秒は学習せず、そのあと10秒ほどで左右それぞれの普段の開き具合をつかみ、以降はゆっくり追従します（直近10分くらいを重視）。少し目を細めた程度ではほとんど動きません。学習値は1分ごとに `~/.config/frameeyeosc/calibration` に保存され、次回はそこから始まります。やり直したいときは、パネルの「リセット」を押してください（または `calibration_reset` を増やす）。

## うまく動かないとき

- ログ: `journalctl --user -u frameeyeosc -f`（パネルは `journalctl --user -u frameeyeosc-panel -f`）
- `No Steam Link connection found; waiting for one`: Steam Link がまだつながっていません。または送り先の PC を固定してください
- ログに `Sending OSC to ...` と出ているのにアバターが反応しない: VRChat の OSC が有効かを確認したうえで、Windows のファイアウォールを確認してください。VRChat の受信許可は「パブリック」だけになっていることが多く、「プライベート」の家のネットワークから届く OSC は止められます。許可の対象が `launch.exe` ではなく `VRChat.exe` になっているかにも注意してください。範囲をしぼって許可するには（管理者の PowerShell で）:
  ```powershell
  New-NetFirewallRule -DisplayName "VRChat OSC (LAN UDP 9000)" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 9000 -RemoteAddress LocalSubnet -Program "C:\Program Files (x86)\Steam\steamapps\common\VRChat\VRChat.exe" -Profile Private
  ```
  付属の無線アダプタは Windows では別のネットワークとして見え、たいてい「パブリック」になっています
- ヘッドセットの起動直後に `Error: ... No such file or directory` と出る: 問題ありません。アイトラッキングがまだ起動していないだけで、数秒後に自動で再試行します
- ヘッドセットを外していると何も動かない: 正常です。Frame は被っている間しか目を追いません
- パネルに「本体が動いていません」と出る: `systemctl --user status frameeyeosc` を確認してください。パネルで変えた設定は保存されていて、動き出したら反映されます

## 既知の問題

- VRChat に直接送るときは、パラメータをビットに詰める「バイナリパラメータ」の VRCFT アバターには対応していません。ETVR モードでは、アバター側は VRCFaceTracking しだいです

## プライバシー

- 視線とまぶたの値は上の送り先（あなたの PC）にだけ送ります。テレメトリはなく、インターネットにも接続しません
- パネルは、起動時と 1 日 1 回まで、GitHub（`api.github.com`）に最新のリリースを問い合わせます（「新しい版の確認」がオフなら問い合わせません）。ふつうの Web アクセスと同じく、GitHub には IP アドレスが見えます。ほかには何も送らず、ダウンロードも GitHub からだけです
- ディスクに保存するのは、左右それぞれの目の「普段の開き具合」の学習値2つ（`~/.config/frameeyeosc/calibration`）と設定だけです。最新の目の値は状態ファイルにありますが、これはメモリの上にあって本人しか読めず、1 秒に 10 回上書きされます。履歴は残しません
- OSC は暗号化されないので、同じネットワーク上の他の機器から読める可能性があります

## 免責事項

- 自己責任でお使いください。このフォークでの変更は AI（Claude Opus 5.5）を使って作りました。ユニットテストと自分の Steam Frame で動作は確かめていますが、あなたの環境で何か起きても責任は取れません。使う前にコードを自分の目で確認してください。本ソフトウェアは無保証です（[LICENSE](LICENSE) を参照）
- ヘッドセットのアイトラッキングが使っている、公開されていない共有メモリの形式（バージョン4）を読んでいます。SteamOS の更新でこの形式が変わると、「unsupported eye shared-memory version」というエラーで起動しなくなり、frameeyeosc が対応するまで使えません
- root 権限は使わず、SteamOS のファイルや設定は変更しません。書き込むのは、アイトラッキングの共有メモリにある「次のサンプルをください」という合図だけです。共有メモリのロックも、アイトラッキングの本来の利用側と同じ手順で取ります。パネルが書くのは frameeyeosc の設定ファイルだけです
- Valve の非公開の内部データを読むことは、リバースエンジニアリングを制限している Steam 利用規約に触れる可能性があります。使うかどうかはご自身で判断してください
- 非公式のプロジェクトで、Valve Corporation、VRChat Inc.、VRCFaceTracking プロジェクト、EyeTrackVR プロジェクトとは関係なく、承認も受けていません。Steam、Steam Frame、SteamVR、Steam Link は Valve Corporation の商標、VRChat は VRChat Inc. の商標です。対応製品を示す目的でのみ名前を使っています

## 開発

ビルドとテストはヘッドセット上で行います（本体はヘッドセットの glibc に、パネルは SteamVR の OpenVR ライブラリにリンクする必要があるため。`scripts/package.sh` を参照）:

```sh
cargo test --release
cmake -G Ninja -S panel -B panel/build && ninja -C panel/build
scripts/package.sh   # 両方入った dist/frameeyeosc-<version>-steamframe-aarch64.tar.gz と dist/SHA256SUMS を作る
```

`vendor/frame-updater/` は、私の Steam Frame 用アプリで共通の更新の仕組みのコピーです。ここでは書き換えないでください。`MANIFEST.sha256` と違っていると `scripts/package.sh` が止まります。

リリースを公開するときは、2 つとも添付します。`SHA256SUMS` が無いリリースは、パネルの「更新する」では入れず、手で更新してもらう表示になります:

```sh
gh release create v0.4.0 --title v0.4.0 --notes-file notes.md
gh release upload v0.4.0 dist/frameeyeosc-0.4.0-steamframe-aarch64.tar.gz dist/SHA256SUMS
```

目の処理を実データで調整するときは、アイトラッカーの生の値を記録してから再生します（記録中は何も送らないので、サービスと並べて動かせます）。再生すると、今の設定と、同じ設定から 0.4.0 の処理を外したものの指標を並べて出します。設定はいつもどおり `config.json` とオプションから読みます。記録は個人のデータなので、リポジトリに入れないでください。

```sh
frameeyeosc --record ~/eyes.csv              # Ctrl+C で止める
frameeyeosc --replay ~/eyes.csv --blink-hold-ms 120 --replay-out ~/processed.csv   # 処理後の値を CSV にも書く
```

## ライセンス

MIT。[LICENSE](LICENSE) を参照してください。元の作品は konsti219 によるものです。同梱している Rust のライブラリと、パネルが使っている OpenVR SDK のヘッダのライセンスは [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) に、変更履歴は [CHANGELOG.md](CHANGELOG.md) にあります。

## 謝辞

まぶたのデータのありかを見つけて frameeyeosc を公開してくれた konsti219 さんに感謝します。このフォークはその成果の上に作っています。
