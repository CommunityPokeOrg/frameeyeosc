# frameeyeosc

Steam Frame のアイトラッキング（視線とまぶたの開き具合）を、VRCFaceTracking 形式のアバターパラメータとして OSC で VRChat に送るツールです。ヘッドセット上でバックグラウンドのサービスとして動き、Steam Link でストリーミングしている PC 版 VRChat で使えます。

[English](README.md)

[konsti219/frameeyeosc](https://github.com/konsti219/frameeyeosc) のフォークです。元のプロジェクトは、Steam Frame が視線だけでなくまぶたの開き具合も取っていて、それが公開 API ではなく内部の共有メモリ（`/dev/shm/eye-server.mmap`）にだけ出ていることを突き止めました。

## このフォークで足したもの

- **送り先の PC を自動で見つける**: Steam Link でつないでいる PC に送ります。付属の無線アダプタでつないでいる場合は直通回線を使うので、家のネットワークに左右されません
- **スムージング**: One Euro フィルタに加えて、注視中の小さな揺れを無視するデッドゾーンと、目を閉じている間の視線の固定（Frame は目を開ける瞬間に視線が跳ねるため）
- **両目で共通の視線**: Frame では左右の目の視線がそれぞれバラバラに揺れ、アバターだと目がピクピク動いて見えます。`--independent-eyes` で左右別々に戻せます
- **まぶたを VRCFT の基準に変換**: VRCFT は 0＝閉じる、0.75＝普通、1＝見開き。Frame は目を閉じ続けると 0 ではなく 0.2 前後を返し、普通に開いた目は 0.75〜0.9 くらいで揺れるので、その分を補正しています
- **まぶたの自動キャリブレーション**: 左右それぞれの「普段の開き具合」を使いながら学習するので、顔や被り方で片目の開きが違っても揃って見えます。ウインクはそのまま残ります
- **サービスとして常駐**: SteamVR と一緒に起動し、止まっても自動で再起動します

## 必要なもの

- 開発者モードを有効にして SSH で入れる Steam Frame（設定 → システム → 開発者モードを有効化、開発者の項目でパスワードを設定）
- Steam Link でストリーミングしている PC 版 VRChat（Action Menu → Options → OSC → Enabled）
- VRCFaceTracking の目のパラメータ（`FT/v2/EyeLeftX`、`EyeLidLeft` など）を float で持つアバター。パラメータをビットに詰める「バイナリパラメータ」のアバターにはまだ対応していません

## インストール

リリースの tar.gz をヘッドセットにコピーして、ヘッドセット上で:

```sh
tar xzf frameeyeosc-*-steamframe-aarch64.tar.gz
cd frameeyeosc
./install.sh
```

sudo は要りません。全部ホームフォルダ（`~/.local/bin`、`~/.config`）に入るので、SteamOS を更新しても消えません。更新するときも同じコマンドです。

**そのあと PC 側で、Steam Link の OSC 送信を OFF にしてください**（SteamVR の設定 → Steam Link → OSC）。Steam Link もスムージングなしの目のデータを VRChat に送っていて、両方動いているとアバターの目が二重に動かされます。

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
- ヘッドセットを外していると何も動かない: 正常です。Frame は被っている間しか目を追いません

## 注意

- Valve の**非公開・非公式**の共有メモリの形式（バージョン4）を読んでいます。SteamOS の更新で変わる可能性があり、その場合は「unsupported eye shared-memory version」のエラーで起動しなくなります（対応版が出るまで使えません）
- Valve とは関係ありません
- 目のデータは暗号化せずにローカルネットワークで PC に送ります

## 開発

ビルドとテストはヘッドセット上で行います（ヘッドセットの glibc にリンクする必要があるため。`scripts/package.sh` を参照）:

```sh
cargo test --release
scripts/package.sh   # dist/frameeyeosc-<version>-steamframe-aarch64.tar.gz を作る
```

このフォークでの変更は AI アシスタント（Claude）と一緒に書き、ユニットテストと実機の Steam Frame で確認しています。

## ライセンス

MIT。[LICENSE](LICENSE) を参照してください。元の作品は konsti219 によるものです。
