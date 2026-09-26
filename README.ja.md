# frameeyeosc

Steam Frame のアイトラッキング（視線とまぶたの開き具合）を、VRCFaceTracking 形式のアバターパラメータとして OSC で VRChat に送るツールです。ヘッドセット上でバックグラウンドのサービスとして動き、Steam Link でストリーミングしている PC 版 VRChat で使えます。

[English](README.md)

[konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc) をフォークしたものです。Steam Frame のまぶたのデータは公開 API からは取れないのですが、元のプロジェクトが内部の共有メモリ（`/dev/shm/eye-server.mmap`）から読めることを見つけてくれたおかげで、このツールを作ることができました。

## このフォークで足したもの

- 送り先の PC は自動で見つけます。Steam Link でつないでいる PC に送るので、アドレスを調べて設定する必要はありません。付属の無線アダプタを使っているときはその直通回線で送るので、家のネットワークの状態にも左右されません
- 視線とまぶたを One Euro フィルタでなめらかにしています。何かを見つめているときの細かい揺れは小さなデッドゾーンで止め、目を閉じている間は視線を固定します。Frame は目を開けた瞬間に視線が跳ねるためです
- 両目に同じ視線を送ります。Frame は左右の目の視線がそれぞれ勝手に揺れるので、そのまま送るとアバターの目がピクピクします。左右別々にしたいときは `--independent-eyes` を付けてください
- まぶたの値を VRCFT の基準（0 で閉じる、0.75 で普通、1 で見開き）に合わせています。Frame の値は、目を閉じ続けても 0.2 前後までしか下がらず、普通に開いているときも 0.75〜0.9 くらいでふらつきます
- まぶたは使っているうちに自動で調整されます。左右それぞれの普段の開き具合を覚えるので、顔や被り方のせいで片目だけ開いて見える人でも、アバターでは揃って見えます。ウインクはそのまま伝わります
- サービスとして常駐し、SteamVR と一緒に起動します。止まっても自動で再起動します

## 必要なもの

- 開発者モードを有効にして SSH で入れる Steam Frame（設定 → システム → 開発者モードを有効化、開発者の項目でパスワードを設定）。SSH を有効にすると、同じネットワークにいてパスワードを知っている人は誰でもヘッドセットに入れるので、推測されにくいパスワードにしてください
- Steam Link でストリーミングしている PC 版 VRChat（Action Menu → Options → OSC → Enabled）
- VRCFaceTracking の目のパラメータ（`FT/v2/EyeLeftX`、`EyeLidLeft` など）を float で持つアバター。パラメータをビットに詰める「バイナリパラメータ」のアバターにはまだ対応していません

## インストール

リリースページから tar.gz をダウンロードして、ヘッドセットにコピーします。PC からなら例えば:

```sh
scp frameeyeosc-*-steamframe-aarch64.tar.gz steamos@<ヘッドセットのIP>:
```

そのあとヘッドセット上で（`ssh steamos@<ヘッドセットのIP>`）:

```sh
tar xzf frameeyeosc-*-steamframe-aarch64.tar.gz
cd frameeyeosc
./install.sh
```

sudo は要りません。全部ホームフォルダ（`~/.local/bin`、`~/.config`）に入るので、SteamOS を更新しても消えません。更新するときも同じコマンドです。

インストールしたら、PC 側で Steam Link の OSC 送信を OFF にしてください（SteamVR の設定 → Steam Link → OSC）。Steam Link もスムージングなしの目のデータを VRChat に送っているので、両方が動いているとアバターの目を2つのデータが取り合ってしまいます。

削除は `./install.sh --uninstall`（設定と学習値も消すなら `--purge` を付ける）。

## 設定

`~/.config/frameeyeosc/env` を書き換えて、`systemctl --user restart frameeyeosc` で反映します。例:

```sh
FRAMEEYEOSC_ARGS="--gaze-min-cutoff 0.3 --lid-sync 0.6"
```

| オプション | 既定値 | 内容 |
|---|---|---|
| `--target HOST:PORT` | `auto` | 送り先。`auto` は Steam Link の接続先 PC の `--port` 番 |
| `--port` | `9000` | `--target auto` のときのポート |
| `--gaze-min-cutoff` | `0.4` | 下げるほど止まっている時の視線が安定（その分遅れる） |
| `--gaze-beta` | `0.8` | 上げるほど素早い視線の動きに遅れず付いていく |
| `--gaze-deadzone` | `0.03` | これより小さい視線の変化は無視（1.0＝45°） |
| `--independent-eyes` | オフ | 共通の視線ではなく、左右それぞれの視線を送る |
| `--lid-closed` / `--lid-open` / `--lid-widen-start` / `--lid-wide` | `0.30` / `0.80` / `0.92` / `1.00` | Frame の開き具合を「閉じ／普通／見開き」にどう対応させるか |
| `--lid-sync` | `0.4` | 左右のまぶたの小さな差を揃える。大きな差（ウインク）はそのまま。`0` で無効 |
| `--lid-scale-left` / `--lid-scale-right` | 学習値 | 学習値の代わりに固定の倍率を使う |
| `--no-lid-calibration` | オフ | まぶたの学習を止める |
| `--raw` | オフ | スムージングしない |

すべてのオプションは `~/.local/bin/frameeyeosc --help` で確認できます。

## キャリブレーション

まぶたのキャリブレーションは自動です。被ってから20秒は学習せず、そのあと10秒ほどで左右それぞれの普段の開き具合をつかみ、以降はゆっくり追従します（直近10分くらいを重視）。少し目を細めた程度ではほとんど動きません。学習値は1分ごとに `~/.config/frameeyeosc/calibration` に保存され、次回はそこから始まります。やり直したいときはこのファイルを消してください。

## うまく動かないとき

- ログ: `journalctl --user -u frameeyeosc -f`
- `No Steam Link connection found; waiting for one`: Steam Link がまだつながっていません。または `--target` で PC のアドレスを指定してください
- ログに `Sending OSC to ...` と出ているのにアバターが反応しない: VRChat の OSC が有効かを確認したうえで、Windows のファイアウォールを確認してください。VRChat の受信許可は「パブリック」だけになっていることが多く、「プライベート」の家のネットワークから届く OSC は止められます。許可の対象が `launch.exe` ではなく `VRChat.exe` になっているかにも注意してください。範囲をしぼって許可するには（管理者の PowerShell で）:
  ```powershell
  New-NetFirewallRule -DisplayName "VRChat OSC (LAN UDP 9000)" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 9000 -RemoteAddress LocalSubnet -Program "C:\Program Files (x86)\Steam\steamapps\common\VRChat\VRChat.exe" -Profile Private
  ```
  付属の無線アダプタは Windows では別のネットワークとして見え、たいてい「パブリック」になっています
- ヘッドセットの起動直後に `Error: ... No such file or directory` と出る: 問題ありません。アイトラッキングがまだ起動していないだけで、数秒後に自動で再試行します
- ヘッドセットを外していると何も動かない: 正常です。Frame は被っている間しか目を追いません

## 既知の問題

- パラメータをビットに詰める「バイナリパラメータ」の VRCFT アバターにはまだ対応していません

## プライバシー

- 視線とまぶたの値は上の送り先（あなたの PC）にだけ送ります。テレメトリはなく、インターネットにも接続しません
- 保存するのは、左右それぞれの目の「普段の開き具合」の学習値2つだけです（`~/.config/frameeyeosc/calibration`）。目のデータそのものはディスクに書きません
- OSC は暗号化されないので、同じネットワーク上の他の機器から読める可能性があります

## 免責事項

- 自己責任でお使いください。このフォークでの変更は AI（Claude Opus 5.5）を使って作りました。ユニットテストと自分の Steam Frame で動作は確かめていますが、あなたの環境で何か起きても責任は取れません。使う前にコードを自分の目で確認してください。本ソフトウェアは無保証です（[LICENSE](LICENSE) を参照）
- ヘッドセットのアイトラッキングが使っている、公開されていない共有メモリの形式（バージョン4）を読んでいます。SteamOS の更新でこの形式が変わると、「unsupported eye shared-memory version」というエラーで起動しなくなり、frameeyeosc が対応するまで使えません
- root 権限は使わず、SteamOS のファイルや設定は変更しません。書き込むのは、アイトラッキングの共有メモリにある「次のサンプルをください」という合図だけです。共有メモリのロックも、アイトラッキングの本来の利用側と同じ手順で取ります
- Valve の非公開の内部データを読むことは、リバースエンジニアリングを制限している Steam 利用規約に触れる可能性があります。使うかどうかはご自身で判断してください
- 非公式のプロジェクトで、Valve Corporation および VRChat Inc. とは関係なく、承認も受けていません。Steam、Steam Frame、SteamVR、Steam Link は Valve Corporation の商標、VRChat は VRChat Inc. の商標です。対応製品を示す目的でのみ名前を使っています

## 開発

ビルドとテストはヘッドセット上で行います（ヘッドセットの glibc にリンクする必要があるため。`scripts/package.sh` を参照）:

```sh
cargo test --release
scripts/package.sh   # dist/frameeyeosc-<version>-steamframe-aarch64.tar.gz を作る
```

## ライセンス

MIT。[LICENSE](LICENSE) を参照してください。元の作品は konsti219 によるものです。同梱している Rust のライブラリのライセンスは [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) に、変更履歴は [CHANGELOG.md](CHANGELOG.md) にあります。

## 謝辞

まぶたのデータのありかを見つけて frameeyeosc を公開してくれた konsti219 さんに感謝します。このフォークはその成果の上に作っています。
