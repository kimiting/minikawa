# ミニかわロボ

M5Stack Atom Lite 2台で動かすミニロボ用の Arduino IDE スケッチです。

ロボ側は 4 Servo PCB for M5ATOM にサーボを3個つなぎます。コントローラー側はジョイスティックと3ボタンで操作します。通信は ESP-NOW です。

## フォルダ構成

- `MiniKawaRobo_Receiver/MiniKawaRobo_Receiver.ino`
  - ロボ側 Atom Lite に書き込むスケッチです。
  - 腕サーボ1個、移動用360度サーボ2個を制御します。
- `MiniKawaRobo_Controller/MiniKawaRobo_Controller.ino`
  - コントローラー側 Atom Lite に書き込むスケッチです。
  - ジョイスティックで移動、G21/G25/G22ボタンで腕を操作します。
- `AtomLite_Joystick_Visualizer/AtomLite_Joystick_Visualizer.ino`
  - コントローラーの入力確認用スケッチです。
- `joystick_visualizer.py`
  - コントローラーのジョイスティックとボタンをMac画面に表示します。
- `robot_input_monitor.py`
  - ロボ側が受け取っている入力をMac画面に表示します。
- `pc_controller.py`
  - MacからUSBシリアルでロボ側を直接操作するテスト用です。

## 必要なもの

Arduino IDE または `arduino-cli` で次のライブラリを入れてください。

- `M5Unified`
- `ESP32Servo`

ボードは Atom Lite 用に ESP32 の M5Stack Atom 系を選びます。`arduino-cli` では現在 `esp32:esp32:m5stack_atom` でビルドしています。

## ロボ側の接続

4 Servo PCB for M5ATOM の Atom Lite 側ピンは次の設定です。

```cpp
constexpr int DEFAULT_ARM_SERVO_PIN = 33;
constexpr int DEFAULT_LEFT_SERVO_PIN = 22;
constexpr int DEFAULT_RIGHT_SERVO_PIN = 23;
constexpr int SPARE_SERVO_PIN = 19;
```

現在の割り当て:

- G33: 腕サーボ
- G22: 左タイヤ用360度サーボ
- G23: 右タイヤ用360度サーボ
- G19: 予備

サーボの電源は不安定になりやすいので、必要なら外部5V電源を使ってください。その場合、外部電源のGNDとAtom Lite側のGNDは共通にします。

## コントローラー側の接続

現在のコントローラー入力ピンです。

```cpp
constexpr int STICK_H = 34;
constexpr int STICK_V = 33;
constexpr bool STICK_H_INVERT = true;
constexpr bool STICK_V_INVERT = true;

constexpr int ARM_UP_BUTTON = 21;
constexpr int ARM_DOWN_BUTTON = 25;
constexpr int ARM_CENTER_BUTTON = 22;
```

ボタン操作:

- G21: 腕を上げる
- G25: 腕を下げる
- G22: 腕を中央へ戻す

ジョイスティック操作:

- 上: 前進
- 下: 後退
- 左: 左へ曲がる
- 右: 右へ曲がる
- 斜め入力: 斜め移動
- 入力なし: 停止

## 書き込み

ロボ側 Atom Lite をUSB接続して、`MiniKawaRobo_Receiver` を書き込みます。

```bash
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:m5stack_atom MiniKawaRobo_Receiver
~/.local/bin/arduino-cli upload -p /dev/cu.usbserial-D952A71439 --fqbn esp32:esp32:m5stack_atom --upload-property upload.speed=115200 MiniKawaRobo_Receiver
```

コントローラー側 Atom Lite をUSB接続して、`MiniKawaRobo_Controller` を書き込みます。

```bash
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:m5stack_atom MiniKawaRobo_Controller
~/.local/bin/arduino-cli upload -p /dev/cu.usbserial-8D52FF1D39 --fqbn esp32:esp32:m5stack_atom --upload-property upload.speed=115200 MiniKawaRobo_Controller
```

ポート名は環境で変わります。確認する場合:

```bash
ls /dev/cu.*
~/.local/bin/arduino-cli board list
```

## 腕サーボの現在の方式

腕は、タイヤと同じように `1500us` を中心にした速度PWM方式で試しています。

```cpp
constexpr int ARM_STOP_US = 1500;
constexpr int ARM_SPEED_US = 180;
constexpr bool ARM_SPEED_INVERT = false;
```

現在の動き:

- G21押しっぱなし: `1680us`
- G25押しっぱなし: `1320us`
- 離す: 腕サーボを `detach()` して信号を切る

調整:

- 腕が速すぎる: `ARM_SPEED_US` を小さくする。例: `120`
- 腕が遅すぎる: `ARM_SPEED_US` を大きくする。例: `220`
- 上下が逆: `ARM_SPEED_INVERT` を `true` にする

注意: SG90のような通常サーボは本来は角度指定用です。この速度PWM方式は、タイヤ用360度サーボのように動くか試すための設定です。個体によっては止まらない、片方向に寄る、動きが変になることがあります。

## 上LEDの色

Atom Lite上面のRGB LEDを状態表示に使います。

```cpp
constexpr int STATUS_LED_PIN = 27;
```

色の意味:

- 紫: 起動中
- 緑: 通信OK / 停止中
- 青: 移動中
- 赤: 腕操作中
- 黄: コントローラー側の送信失敗
- 白点滅: ロボ側の通信待ち、またはフェイルセーフ停止

コントローラー側とロボ側の両方に同じ色分けを入れています。

## タイヤの調整

タイヤ用360度サーボの速度はここで調整します。

```cpp
constexpr int SERVO_STOP_US = 1500;
constexpr int SERVO_SPEED_US = 350;
```

左右どちらかが逆に回る場合:

```cpp
constexpr bool LEFT_INVERT = false;
constexpr bool RIGHT_INVERT = true;
```

停止時にじわじわ動く場合:

```cpp
constexpr int LEFT_TRIM_US = 0;
constexpr int RIGHT_TRIM_US = 0;
```

## Macからロボ側を直接操作する

ロボ側 Atom Lite に `MiniKawaRobo_Receiver` を書き込んだ状態で、MacにUSB接続します。

必要なPythonライブラリ:

```bash
python3 -m pip install pyserial pygame
```

実行:

```bash
python3 pc_controller.py
```

ポートを指定する場合:

```bash
python3 pc_controller.py --port /dev/cu.usbserial-D952A71439
```

キー操作:

- `w`: 前進
- `s`: 後退
- `a`: 左旋回
- `d`: 右旋回
- `w+a`: 左前
- `w+d`: 右前
- `x`: 停止
- `j`: 腕をマイナス方向へ動かす
- `l`: 腕をプラス方向へ動かす
- `k`: 腕を中央へ戻す
- `q` または `ESC`: 終了

注意: `pc_controller.py` の腕操作はUSBシリアルの角度コマンドで動かすテスト用です。コントローラーAtomからの本番操作とは少し方式が違います。

## コントローラー入力を画面で見る

コントローラー側 Atom Lite に `AtomLite_Joystick_Visualizer` を書き込み、MacにUSB接続して実行します。

```bash
python3 joystick_visualizer.py
```

ポート指定:

```bash
python3 joystick_visualizer.py --port /dev/cu.usbserial-8D52FF1D39
```

操作:

- `c`: 軌跡を消す
- `t`: 軌跡表示オン/オフ
- `f`: 軌跡フェードオン/オフ
- `q` または `ESC`: 終了

ボタンが反応している場合、画面右側に `G21`、`G25`、`G22` などのピン番号が表示されます。

## 複数ペアで使う場合

複数のロボとコントローラーを同じ場所で使う場合は、ロボ側とコントローラー側の `PAIR_ID` をペアごとに変えます。

ロボ側:

```cpp
constexpr uint8_t PAIR_ID = 1;
```

コントローラー側:

```cpp
constexpr uint8_t PAIR_ID = 1;
```

同じ番号同士だけ通信します。

内部的には `PACKET_MAGIC` に `PAIR_ID` を混ぜて判定しています。通信パケットのサイズは変えないので、スティックやタイヤの値がズレにくい方式です。

例:

- ペアA: ロボ側 `PAIR_ID = 1`、コントローラー側 `PAIR_ID = 1`
- ペアB: ロボ側 `PAIR_ID = 2`、コントローラー側 `PAIR_ID = 2`
- ペアC: ロボ側 `PAIR_ID = 3`、コントローラー側 `PAIR_ID = 3`

番号が違うパケットはロボ側で無視されます。書き換えたら、ロボ側とコントローラー側の両方を書き込み直してください。

## ロボ側が受け取っている入力を見る

ロボ側 Atom Lite をMacにUSB接続して実行します。

```bash
python3 robot_input_monitor.py
```

ポート指定:

```bash
python3 robot_input_monitor.py --port /dev/cu.usbserial-D952A71439
```

ロボ側のシリアルログ `RX seq=... L/R=... Arm=... Mode=...` を読み取り、受信している移動入力をジョイスティック形で表示します。

## 通信確認

ロボ側をMacにUSB接続して、コントローラーを動かしたときにシリアルログへ次のような行が出ればESP-NOW通信できています。

```text
RX seq=123 L/R=80/80 Arm=90 Mode=DRIVE age=0ms
RX seq=124 L/R=0/0 Arm=90 Mode=ARM_UP age=0ms
```

`seq` が増えていれば、コントローラーからロボ側へ入力が届いています。

## ロボ側シリアルコマンド

ロボ側はUSBシリアルから簡単なテストコマンドも受け付けます。

```text
D left right [arm]
A arm
R delta
S
P arm left right spare
P?
T
```

- `D 100 100`: 前進
- `D 0 0`: 停止
- `A 90`: 腕を90度へ
- `R 10`: 今の腕角度から+10度
- `S`: 停止
- `P?`: 現在のサーボピン表示
- `T`: 4つのサーボピンを順番にテスト

コントローラーからESP-NOW入力が届いている間は、USBシリアルで送った操作がすぐ上書きされることがあります。
