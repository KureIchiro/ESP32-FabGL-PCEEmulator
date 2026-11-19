# NEC PC Engine / TurboGrafx-16 Emulator for ESP32 (TTGO VGA32)

A PC Engine (HuCARD) emulator for the LILYGO TTGO VGA32 (ESP32 + FabGL), built on the pce-go core (the PC Engine core of retro-go).
It uses the same setup as the other emulators in this collection: ROM list at start-up, PS/2 keyboard, the joystick adapter on Serial1 and F12 pause.

> ⚠️ **Notice Regarding Development & Licenses**
>
> Most of the main source code is built and modified based on AI-generated code. Therefore, the developer does not completely grasp all internal logic and cannot answer detailed questions regarding source code specifications. Thank you for your understanding.
> Please refer to the header comments at the beginning of the source code for license details and agree to them before use.

---

## Credits & License

- **Emulation core**: pce-go, the PC Engine core of retro-go by ducalex (retro-go is GPLv2, https://github.com/ducalex/retro-go; pce-go is a fork of HuExpress, which is based on Hu-Go!, GPLv2), as included in Retro-Gamer (GPLv3). Modified as listed in the header of `PCEngine.ino` (changes dated 2026-11-12, 2026-11-13, 2026-11-16, 2026-11-18 and 2026-11-19).
- **Graphics/audio engine**: FabGL by Fabrizio Di Vittorio (GPLv3).

Like the other emulators in this collection, this sketch as a whole is distributed under the GPLv3 (`LICENSE`), with its full source.

---

## 🛠️ Environment & Build Settings

- **Board Manager**: ESP32 by Espressif Systems (**v2.0.5 Recommended**)
- **Library**: FabGL v1.0.9
- **Board**: `ESP32 Dev Module` (TTGO VGA32)
- **PSRAM** (ON = `"Enabled"`, OFF = `"Disabled"`): With PSRAM ON, a software workaround for the chip errata (Espressif errata CPU-3.9) is built in, and the speed drops by about 20%. With PSRAM OFF, the sketch enables PSRAM by itself and the software workaround is not built in. However, this causes failures unless the ESP32 is chip revision 3 or later, so on any other revision a PSRAM OFF build is made not to start.
- **Partition Scheme**: the default is fine.
- **SD card pins** (`SD_SCLK` / `SD_MISO` / `SD_MOSI` / `SD_CS` near the top of the `.ino`): they differ by board. The default is TTGO VGA32 (14 / 2 / 12 / 13). ORANGE ESPer: 14 / 35 / 12 / 13. Switch Science FabGL development board: 14 / 35 / 2 / 13. The FabGL development board wires the SD card to MISO = 16 and MOSI = 17, but on an ESP32-WROVER these two pins are not connected (they are used inside the module for the PSRAM), so jumper 16 → 35 and 17 → 2 on the FabGL board. Enable the block for your board and comment out the others.
- **Switching machines with EmuLauncher** (optional): copy `emu_switch.h` and `partitions.csv` from the `EmuLauncher` folder into this sketch folder. The ROM list then shows the machine to start next at the bottom (TAB or controller button 3 changes it), and the flash is laid out as in `partitions.csv` (the Partition Scheme setting is not used). Without the two files the sketch works on its own as before. See `EmuLauncher/README.md`.

---

## 📂 SD Card

```text
/PCE/ROM/   .pce  (sub-folders are fine; up to 4 MB)
```

---

## 🎮 Controls

| | Keyboard | Controller (Serial1) |
|---|---|---|
| D-pad | Arrow keys | D-pad |
| I / II | Z / X | Button 1 / 2 |
| SELECT | Space | Button 3 |
| RUN | Enter | Button 4 |

- **Controller**: player 1 only, 4 buttons. The adapter on Serial1 sends one byte at a time as on the other machines (0 = pressed; bit 0–3 = up/down/left/right, bit 4–6 = buttons 1–3), but bit 7 is button 4 here (the other machines use bit 7 to tell player 1 from player 2), so the adapter must be in its 1-controller 4-button mode.
- **F12**: pause / resume. **Shift + F12**: reset (restarts the ESP32 and goes back to the ROM list).
- **ROM list**: Up/Down = select, PageUp/PageDown = page, Enter = start / open the folder, BackSpace or ESC = folder up. With the controller: up/down = select, left/right = page (hold left/right briefly while up/down is not pressed), button 1 or 4 = Enter, button 2 = folder up. TAB (controller button 3) = machine to start next (only with EmuLauncher).
- **Button layout**: each Enter on the top row of the ROM list (BUTTON 1 2 = …) swaps buttons 1 and 2 (keyboard Z and X) between I II and II I. It is saved to `/PCE/PAD.CFG` on the SD card. SELECT and RUN do not change.

---

## How it runs

- **Picture**: the palette holds raw VGA pixels. The lines are drawn on the other CPU core (the one that runs `loop()`, together with the sound) into a buffer in internal RAM a band (up to 24 lines) at a time, and each finished band is copied at once into FabGL's VGA line buffers (320×240, double-scanned to 640×480 at 59.94 Hz). The emulation goes on while a band is drawn; a VRAM change made just after a band can show in that band. Screens narrower than 320 dots are centred; wider ones show the middle 320 dots. When a frame starts late, its drawing is skipped (never two frames in a row). If the VGA scan overtakes the drawing, that one refresh shows a seam with the previous frame partway down the screen.
- **Colours**: the VGA output of the TTGO VGA32 has 2 bits per colour (64 colours); the PC Engine's 3 bits per colour are reduced to the top 2.
- **Sound**: the PSG (6 wave channels, noise on channels 5–6, DDA) is generated at 16384 Hz on the other CPU core. The emulation only queues the register writes with their time, so DDA (PCM) samples play at the time they were written.
- **Memory**: CPU RAM, VRAM, the memory maps and the band buffer are in the ESP32's internal RAM; the ROM is in PSRAM.

## Known limitations

- HuCARD only (no CD-ROM², no SuperGrafx, no Arcade Card); 2-button pad, player 1 only.
- No save states, no backup RAM.
- The PSG LFO is not emulated (as in pce-go).

---
---

# NEC PC エンジン / TurboGrafx-16 エミュレータ for ESP32(TTGO VGA32)

LILYGO TTGO VGA32(ESP32 + FabGL)用の PC エンジン(HuCARD)のエミュレータです。retro-go の PC エンジンのコア pce-go を元にしています。
このシリーズのほかのエミュレータと同じ作りです(起動すると ROM の一覧、PS/2 キーボード、Serial1 のジョイスティック、F12 の一時停止)。

> ⚠️ **開発・ライセンスに関するご注意**
>
> メインソースコードの大部分は AI 生成コードをベースに構築・改変しているため、開発者もすべてのロジックを完全に把握しているわけではありません。そのため、ソースコードの詳細な仕様や内部ロジックに関する質問にはお答えできかねます。あらかじめご了承ください。
> ライセンスの詳細な条件はソースコード冒頭のヘッダコメントを参照し、同意の上でご利用ください。

---

## クレジットとライセンス

- **エミュレーションのコア**: pce-go。ducalex さんの retro-go の PC エンジンのコアです(retro-go は GPLv2、https://github.com/ducalex/retro-go。pce-go は Hu-Go! を元にした HuExpress から分かれたもの。GPLv2)。Retro-Gamer(GPLv3)に入っていたものを使い、`PCEngine.ino` の先頭に書いたとおりに変えています(2026-11-12・2026-11-13・2026-11-16・2026-11-18・2026-11-19 の変更)。
- **グラフィック/オーディオエンジン**: Fabrizio Di Vittorio さんの FabGL(GPLv3)。

このシリーズのほかのエミュレータと同じく、このスケッチ全体を GPLv3 で、ソースごと配ります(`LICENSE`)。

---

## 🛠️ 開発環境とビルド設定

- **ボードマネージャ**: ESP32 by Espressif Systems(**v2.0.5 推奨**)
- **ライブラリ**: FabGL v1.0.9
- **ボード**: `ESP32 Dev Module`(TTGO VGA32)
- **PSRAM**（ON = `"Enabled"`、OFF = `"Disabled"`）: PSRAMをONにするとソフトでのエラッタ対策が入り、20%程度速度が低下します。PSRAMをOFFにするとソフトでPSRAMを有効にし、ソフトでのエラッタ対策は入りません。但し、ESP32のリビジョンが3以降でないと不具合が発生するため、それ以外のリビジョンでPSRAMをOFFにしても起動しなくしてあります。
- **Partition Scheme**: 既定のままでかまいません。
- **SD カードのピン**（`.ino` の先頭近くの `SD_SCLK` / `SD_MISO` / `SD_MOSI` / `SD_CS`）: ボードによって違います。既定は TTGO VGA32（14 / 2 / 12 / 13）です。ORANGE ESPer は 14 / 35 / 12 / 13、スイッチサイエンスの FabGL 開発ボードは 14 / 35 / 2 / 13 です。FabGL 開発ボードは SD カードを MISO = 16、MOSI = 17 で配線していますが、ESP32-WROVER ではこの 2 本はモジュールの中で PSRAM に使われていて外にはつながっていない（NC）ため、FabGL ボード上で 16 → 35、17 → 2 にジャンパしてください。使うボードのブロックを有効にし、ほかはコメントにしてください。
- **EmuLauncher での機種の切り替え**（使うときだけ）: `EmuLauncher` のフォルダの `emu_switch.h` と `partitions.csv` を、このスケッチのフォルダにコピーします。ROM の一覧のいちばん下に次に起動する機種が出て（TAB かコントローラの ボタン 3で変える）、フラッシュの分け方が `partitions.csv` のとおりになります（Partition Scheme の設定は使われません）。2 つのファイルが無ければ、今までどおり単体で動きます。詳しくは `EmuLauncher/README.md` を見てください。

---

## 📂 SD カード

```text
/PCE/ROM/   .pce(フォルダに分けてもよい。4MB まで)
```

---

## 🎮 操作

| | キーボード | コントローラ(Serial1) |
|---|---|---|
| 方向 | 矢印キー | 方向 |
| I / II | Z / X | ボタン 1 / 2 |
| SELECT | Space | ボタン 3 |
| RUN | Enter | ボタン 4 |

- **コントローラ**: 1P 専用・4 ボタンです。Serial1 のアダプタからはほかの機種と同じく 1 バイトずつ届きます(0 = 押下。bit0〜3 = 上下左右、bit4〜6 = ボタン 1〜3)が、この機種では bit7 がボタン 4 です(ほかの機種は bit7 で 1P と 2P を分けています)。アダプタは 1 コン 4 ボタンのモードにしてください。
- **F12**: 一時停止 / 再開。**Shift + F12**: リセット(ESP32 ごと再起動して ROM の一覧に戻る)。
- **ROM の一覧**: ↑↓ = 選ぶ、PageUp / PageDown = ページ、Enter = 起動 / フォルダに入る、BackSpace か ESC = 上のフォルダ。コントローラは上下 = 選ぶ、左右 = ページ(上下を押していないときに左右を少し押し続ける)、ボタン1 か 4 = Enter、ボタン2 = 上のフォルダ。TAB(コントローラはボタン 3)= 次に起動する機種(EmuLauncher を使うときだけ)。
- **ボタンの配置**: ROM の一覧のいちばん上の行(BUTTON 1 2 = …)で Enter を押すたびに、ボタン 1・2(キーボードの Z・X)が I II と II I で入れ替わります。SD の `/PCE/PAD.CFG` に保存されます。SELECT・RUN は変わりません。

---

## 動き方

- **画面**: 色の表を VGA の画素の値にしておき、ラインをもう一方の CPU コア(`loop()` のコア。音と同じ)で帯(最大 24 ライン)ごとに内部 RAM のバッファへ描き、描き終えた帯をすぐ FabGL の VGA の行バッファへ写します(320×240 を縦横 2 倍にして 640×480・59.94Hz で出す)。描いているあいだもエミュレーションは先へ進みます。帯を描いた直後の VRAM の変化が、その帯に出ることがあります。幅が 320 ドットより狭い画面は真ん中に置き、広い画面は真ん中の 320 ドットを出します。予定より遅れて始まったフレームは描画を間引きます(2 フレーム続けては飛ばしません)。描画が VGA の走査に追い越されると、そのリフレッシュだけ画面の途中に前のフレームとの継ぎ目が出ます。
- **色**: TTGO VGA32 の VGA は 1 色 2 ビット(64 色)なので、PC エンジンの 1 色 3 ビットの上の 2 ビットを使います。
- **音**: PSG(波形メモリの 6 チャンネル、5・6 チャンネルのノイズ、DDA)を、もう一方の CPU コアで 16384Hz で作ります。エミュレーション側はレジスタの書き込みを時刻付きで積むだけなので、DDA(PCM)も書いた時刻どおりに鳴ります。
- **メモリ**: CPU の RAM・VRAM・メモリの割り当ての表と描画の帯のバッファは ESP32 の内部 RAM、ROM は PSRAM です。

## できないこと

- HuCARD だけです(CD-ROM²・スーパーグラフィックス・アーケードカードには対応していません)。2 ボタンのパッド、1P だけです。
- ステートセーブ、バックアップ RAM には対応していません。
- PSG の LFO は扱いません(pce-go と同じ)。
