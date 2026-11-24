/* ============================================================================
 *  NEC PC Engine / TurboGrafx-16 Emulator for ESP32 (Work in Progress)
 *  --- TTGO VGA32 / FabGL。画面は 320x240(VGA 640x480 と同じタイミングで縦横 2 倍)、64 色 ---
 * ============================================================================
 *
 * 【クレジットと謝辞】
 *  - エミュレーションのコア: pce-go(ducalex 氏の retro-go の PC エンジンのコア。retro-go は GPLv2。
 *    出元: https://github.com/ducalex/retro-go。pce-go は HuExpress(Hu-Go! が元。GPLv2)から分かれたもの)。
 *    Retro-Gamer(GPLv3)に入っていたものを使い、下の「変更」のとおり手を入れている
 *  - グラフィック/オーディオエンジン: Fabrizio Di Vittorio 氏による FabGL(GPLv3)
 *
 * 【ライセンス】GPLv3(LICENSE)。このシリーズのほかのエミュレータと同じく(FabGL は GPLv3)、このスケッチ全体を GPLv3 で配る
 *  (配るときは出元を示し、ソースを公開する)
 *
 * 【変更(GPLv3 第 5 条 a)】2026-11-12 pce-go を TTGO VGA32 用のスケッチにした。
 *  ・画面: 色の表(PCE.Palette)を VGA の画素の値にし、描き終えたフレームのバッファ(PSRAM)を次のフレームで
 *    1 ラインずつ VGA の行バッファへ写す(1 フレーム遅れるが、VGA が出している行は書き換えないので絵が裂けない)
 *  ・音: PSG の書き込みを時刻付きで積み、別のコアで 16384Hz で作る(pce_sound.cpp。psg.c の代わり)。
 *    DDA(PCM)も書いた時刻どおりに鳴る
 *  ・CPU の RAM・VRAM などを内部 RAM に置く(pce_alloc)。CPU の実行部(h6280_run)は IRAM
 *  ・遅れて始まったフレームは描画を間引く(pce_render_skip)
 *  ・速くした: CPU(h6280.c・h6280_instr.h)、VDC の書き込み(pce.c の pce_writeVDC)、描画(gfx.c。描く関数は IRAM)。
 *    pce.c は O3 とジャンプテーブルありでコンパイルする
 *  ・EmuLauncher での機種の切り替え(スケッチのフォルダに emu_switch.h があるときだけ。ID は "PCE")
 *  2026-11-13 速くした。
 *  ・描画をもう一方のコア(loop())で: ラインを帯ごとに頼み、エミュレーションは先へ進む(gfx.c の gfx_render_service)
 *  ・前のフレームで描画を間引いたときは、VGA へ写すのを省いた(2026-11-19 に画面バッファの方式ごと外した)
 *  ・pce_glue.c・pce_sound.cpp も O3 でコンパイルする
 *  2026-11-16 操作を変えた。
 *  ・コントローラ(Serial1)を 1P 専用にし、bit7 をボタン4 にした(ボタン3 = SELECT、ボタン4 = RUN)
 *  ・キーボードの I・II を Z・X にした(MARK III 版のボタン1・2 と同じキー)
 *  2026-11-17 速くした。
 *  ・CPU が I/O を読み書きする関数(pce.c の pce_readIO・pce_writeIO・pce_writeVDC)と、ラインごとの処理
 *    (gfx.c の gfx_run・gfx_irq)を IRAM に置く(フラッシュにあると ROM(PSRAM)とキャッシュを取り合う)
 *  ・帯を内部 RAM のバッファに描いて、描いたコアがすぐ VGA の行へ写す(gfx.c の band_buf・pce_band_done)。
 *    1 フレームの遅れがなくなり、PSRAM から写す処理もなくなった(2026-11-18 からの既定。11-19 にこの方式だけにした)
 *  2026-11-19 仕上げ。
 *  ・PSRAM の画面バッファに描いて次のフレームで写す方式と、その画面バッファを外した
 *  ・計測用の表示(RENDER・PARTS の行)を外した(SPEED の行はほかの機種と同じく残す)
 *  ・ROM の一覧のいちばん上の行で、ボタン1・2 の配置(I II / II I)を選べるようにした(/PCE/PAD.CFG に保存)
 *  2026-11-24 足した・直した。
 *  ・ステートセーブ: F9 / F10 / F11 でスロット 2 / 1 / 0 を読み込み、Shift を押しながらで保存(/PCE/SAVE)。
 *    pce-go の SaveState / LoadState を使い、読み込んだあと PSG のレジスタを音の側へ送り直す(pce_glue.c)
 *  ・3M ビットのカードを 4M ビットに水増ししたファイルを、3M ビットのカードと同じ並びで読む(pce-go.c。Rabio Lepus)
 *
 * 【必要なファイル（SDカード）】
 *  /PCE/ROM の .pce : HuCARD の ROM(フォルダに分けてもよい)。起動すると一覧が出る
 *
 * 【操作】
 *  キーボード: ↑↓←→ = 方向、Z = I、X = II、Space = SELECT、Enter = RUN(方向と I・II は MARK III 版と同じ)
 *  コントローラ(Serial1。1P 専用・4 ボタン): 方向、ボタン1 = I、ボタン2 = II、ボタン3 = SELECT(MODE)、ボタン4 = RUN(START)
 *  F12: 一時停止 / 再開。Shift+F12: リセット(ESP32 ごと。ROM の一覧に戻る)
 *  F9 / F10 / F11: ステートのスロット 2 / 1 / 0 を読み込む。Shift+F9 / F10 / F11: 保存
 *  ROM の一覧で TAB / ボタン3: 次に起動する機種(EmuLauncher を使うときだけ)
 * ============================================================================
 */

#pragma GCC optimize("O3")

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <SPI.h>
#include <SD.h>
#include "pce_glue.h"
#if __has_include("emu_switch.h")
#include "emu_switch.h"
#define EMU_SWITCH 1
#else
#define EMU_SWITCH 0
#endif
#include <fabgl.h>
#include <vector>
#include <algorithm>
#include "soc/rtc_wdt.h"
#include "esp_task_wdt.h"
#ifndef BOARD_HAS_PSRAM
#include "esp32/spiram.h"
#include "esp_heap_caps.h"
#define ps_malloc(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif
#if defined(BOARD_HAS_PSRAM) && !defined(EMU_PSRAM_NO_ERRATA_FIX)
#define psramFixMissing() false
#else
#define psramFixMissing() (ESP.getChipRevision() < 3)
#endif

extern "C" {
#include "pce-go.h"
#include "gfx.h"
extern int pce_render_skip;
}

//#define USE_SERIAL_DEBUG

#define PCE_DIR "/PCE"
#define PCE_ROM_DIR PCE_DIR "/ROM"
#define PCE_ROM_MAX (4u * 1024 * 1024)

// TTGO VGA32
#define SD_SCLK 14
#define SD_MISO 2
#define SD_MOSI 12
#define SD_CS   13

// ORANGE ESPer
// #define SD_SCLK 14
// #define SD_MISO 35
// #define SD_MOSI 12
// #define SD_CS   13

// Switch Science FabGL
// #define SD_SCLK 14
// #define SD_MISO 35
// #define SD_MOSI 2
// #define SD_CS   13

#define SCREEN_W 320
#define SCREEN_H 240

fabgl::VGAController DisplayController;
fabgl::Canvas *canvas;
fabgl::SoundGenerator soundGenerator;
fabgl::PS2Controller PS2Controller;

class PceWave : public fabgl::WaveformGenerator {
public:
  int getSample() override { return pceSoundGetSample(); }
  void setFrequency(int value) override { (void)value; }
};
static PceWave pceWave;

static volatile bool isPaused = false;
static volatile bool cpuStopped = false;
static volatile bool frameReady = false;
static uint8_t *vpRows[SCREEN_H];
static volatile uint8_t joyBits = 0xFF;
static void updateJoystick(bool hold) {
  uint8_t j = hold ? joyBits : 0xFF;
  while (Serial1.available() > 0) j = (uint8_t)Serial1.read();
  joyBits = j;
}

static int joyMenuPoll() {
  static uint8_t state = 0xFF, last = 0xFF, cand = 0xFF;
  static uint32_t lastRx = 0, repeatAt = 0;
  uint32_t now = millis();
  while (Serial1.available() > 0) {
    const uint8_t b = (uint8_t)Serial1.read();
    if (b == cand) state = b;
    cand = b;
    lastRx = now;
  }
  if (now - lastRx > 500) state = cand = 0xFF;
  uint8_t pressed = (uint8_t)~state, prev = (uint8_t)~last;
  uint8_t edge = pressed & ~prev;
  last = state;
  if (edge & 0x90) return 'A';
  if (edge & 0x20) return 'B';
  if (edge & 0x40) return 'C';
  for (int i = 0; i < 2; i++) {
    if (!(pressed & (1 << i))) continue;
    if (edge & (1 << i)) { repeatAt = now + 400; return i ? 'D' : 'U'; }
    if ((int32_t)(now - repeatAt) >= 0) { repeatAt = now + 120; return i ? 'D' : 'U'; }
  }
  static uint8_t lrHeld = 0;
  static uint32_t lrAt = 0;
  const uint8_t lr = (pressed & 0x03) ? 0 : (uint8_t)(pressed & 0x0C);
  if (lr != lrHeld) { lrHeld = lr; lrAt = now + 150; }
  if ((lr == 0x04 || lr == 0x08) && (int32_t)(now - lrAt) >= 0) { lrAt = now + 400; return lr == 0x04 ? 'L' : 'R'; }
  return 0;
}
static int joyMenuScancode(bool *e0) {
  *e0 = true;
  switch (joyMenuPoll()) {
    case 'U': return 0x75;
    case 'D': return 0x72;
    case 'L': return 0x7D;
    case 'R': return 0x7A;
    case 'A': *e0 = false; return 0x5A;
    case 'B': *e0 = false; return 0x76;
    case 'C': *e0 = false; return 0x0D;
    default: return 0;
  }
}

static volatile uint8_t kbdPad = 0;
static bool shiftDown = false;

#define PCE_PAD_FILE PCE_DIR "/PAD.CFG"
static bool padSwap = false;
static void loadPadLayout() {
  File f = SD.open(PCE_PAD_FILE, FILE_READ);
  if (!f) return;
  padSwap = f.read() == '2';
  f.close();
}
static void savePadLayout() {
  SD.remove(PCE_PAD_FILE);
  File f = SD.open(PCE_PAD_FILE, FILE_WRITE);
  if (!f) return;
  f.write((uint8_t)(padSwap ? '2' : '1'));
  f.close();
}
static const char *padLayoutLabel() { return padSwap ? "BUTTON 1 2 = II I  (ENTER: CHANGE)" : "BUTTON 1 2 = I II  (ENTER: CHANGE)"; }

static void setKbdBit(uint8_t bit, bool down) {
  if (down) kbdPad |= bit;
  else kbdPad &= (uint8_t)~bit;
}

static void showStatus(const char *msg);
static volatile int stateReq = 0;
static String romPath;
static uint32_t statusUntil = 0;
static char statusMsg[48];
static void flashStatus(const char *msg, uint32_t ms) {
  snprintf(statusMsg, sizeof(statusMsg), "%s", msg);
  showStatus(msg);
  statusUntil = millis() + ms;
  if (!statusUntil) statusUntil = 1;
}

static void updateKeyboard() {
  static bool isBreak = false, isE0 = false;
  auto kb = PS2Controller.keyboard();
  if (!kb) return;
  while (kb->scancodeAvailable() > 0) {
    int sc = kb->getNextScancode(0);
    if (sc == 0xF0) { isBreak = true; continue; }
    if (sc == 0xE0) { isE0 = true; continue; }
    const bool down = !isBreak, e0 = isE0;
    isBreak = false; isE0 = false;
    if (!e0 && (sc == 0x12 || sc == 0x59)) { shiftDown = down; continue; }
    if (e0) {
      switch (sc) {
        case 0x75: setKbdBit(JOY_UP, down); break;
        case 0x72: setKbdBit(JOY_DOWN, down); break;
        case 0x6B: setKbdBit(JOY_LEFT, down); break;
        case 0x74: setKbdBit(JOY_RIGHT, down); break;
        case 0x5A: setKbdBit(JOY_RUN, down); break;
      }
      continue;
    }
    switch (sc) {
      case 0x1A: setKbdBit(padSwap ? JOY_B : JOY_A, down); break;
      case 0x22: setKbdBit(padSwap ? JOY_A : JOY_B, down); break;
      case 0x5A: setKbdBit(JOY_RUN, down); break;
      case 0x29: setKbdBit(JOY_SELECT, down); break;
      case 0x01: case 0x09: case 0x78:
        if (!down) break;
        stateReq = 0x10 | (sc == 0x01 ? 2 : sc == 0x09 ? 1 : 0) | (shiftDown ? 0x80 : 0);
        break;
      case 0x07:
        if (!down) break;
        if (shiftDown) ESP.restart();
        if (!isPaused) {
          isPaused = true;
          pce_sound_mute = true;
          while (!cpuStopped) {
            gfx_render_service();
            if (!pce_render_async) vTaskDelay(1);
          }
          showStatus("PAUSED  F12:RESUME  SHIFT+F12:RESET");
        } else {
          isPaused = false;
          pce_sound_mute = false;
        }
        break;
    }
  }
}

static void updatePad() {
  const uint8_t j = (uint8_t)~joyBits;
  uint8_t p = kbdPad;
  if (j & 0x01) p |= JOY_UP;
  if (j & 0x02) p |= JOY_DOWN;
  if (j & 0x04) p |= JOY_LEFT;
  if (j & 0x08) p |= JOY_RIGHT;
  if (j & 0x10) p |= padSwap ? JOY_B : JOY_A;
  if (j & 0x20) p |= padSwap ? JOY_A : JOY_B;
  if (j & 0x40) p |= JOY_SELECT;
  if (j & 0x80) p |= JOY_RUN;
  pce_pad = p;
}

static void showStatus(const char *msg) {
  const uint8_t bg = DisplayController.createRawPixel(fabgl::RGB222(0, 0, 0));
  const uint8_t fg = DisplayController.createRawPixel(fabgl::RGB222(3, 3, 3));
  const fabgl::FontInfo &fnt = fabgl::FONT_8x8;
  for (int r = 0; r < 10; r++) memset(vpRows[SCREEN_H - 10 + r], bg, SCREEN_W);
  for (int i = 0; msg[i] && i < SCREEN_W / 8; i++) {
    const uint8_t *g = fnt.data + (uint8_t)msg[i] * 8;
    for (int r = 0; r < 8; r++)
      for (int k = 0; k < 8; k++)
        vpRows[SCREEN_H - 9 + r][(i * 8 + k) ^ 2] = (g[r] & (0x80 >> k)) ? fg : bg;
  }
}
struct RomItem { String name; int kind; };
static std::vector<RomItem> romItems;

static bool isRomName(const String &name) {
  String u = name; u.toUpperCase();
  return u.endsWith(".PCE");
}

static void listRoms(const String &path, bool canGoUp) {
  romItems.clear();
  File dir = SD.open(path.c_str());
  if (!dir) return;
  std::vector<RomItem> dirs, files;
  File e;
  while ((e = dir.openNextFile())) {
    String name = e.name();
    int s = name.lastIndexOf('/');
    if (s >= 0) name = name.substring(s + 1);
    if (name.length() && name[0] != '.' && name != "System Volume Information") {
      if (e.isDirectory()) dirs.push_back({ name, 1 });
      else if (isRomName(name)) files.push_back({ name, 2 });
    }
    e.close();
  }
  dir.close();
  auto byName = [](const RomItem &a, const RomItem &b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; };
  std::sort(dirs.begin(), dirs.end(), byName);
  std::sort(files.begin(), files.end(), byName);
  if (canGoUp) romItems.push_back({ "..", 0 });
  romItems.insert(romItems.end(), dirs.begin(), dirs.end());
  romItems.insert(romItems.end(), files.begin(), files.end());
}

#if EMU_SWITCH
static int emuNext = -1;
#endif

static String selectRom() {
  String sub = "", want = "";
  canvas->selectFont(&fabgl::FONT_8x8);
  auto kb = PS2Controller.keyboard();
  const int visible = 24;
  loadPadLayout();
#if EMU_SWITCH
  emuNext = emuNextInit("PCE");
#endif

  for (;;) {
    String path = String(PCE_ROM_DIR) + (sub == "" ? "" : "/" + sub);
    listRoms(path, sub != "");
    if (romItems.empty() && sub != "") { sub = ""; want = ""; continue; }
    const bool noRom = romItems.empty();
    if (sub == "") romItems.insert(romItems.begin(), RomItem{ "", 3 });
    int cursor = (sub == "" && romItems.size() > 1) ? 1 : 0;
    for (int i = 0; i < (int)romItems.size(); i++)
      if (romItems[i].kind != 3 && romItems[i].name == want) { cursor = i; break; }
    bool isBreak = false, isE0 = false, changeDir = false;
    int shownTop = -1, shownCursor = -1, listTop = 0;

    auto drawItem = [&](int idx, int top) {
      int y = 32 + (idx - top) * 8;
      canvas->setBrushColor(fabgl::Color::Blue);
      canvas->fillRectangle(0, y, SCREEN_W - 1, y + 7);
      if (idx < 0 || idx >= (int)romItems.size()) return;
      const RomItem &it = romItems[idx];
      String label = it.kind == 3 ? String(padLayoutLabel()) : it.kind == 2 ? it.name : it.name + "/";
      if (label.length() > 38) label = label.substring(0, 37) + "~";
      canvas->setPenColor(idx == cursor ? fabgl::Color::BrightYellow
                          : it.kind == 3 ? fabgl::Color::BrightGreen
                          : it.kind == 2 ? fabgl::Color::BrightWhite : fabgl::Color::BrightCyan);
      canvas->drawText(8, y, label.c_str());
    };
#if EMU_SWITCH
    auto drawNext = [&]() {
      char m[48], line[64];
      emuNextLabel(emuNext, "PCE", m, sizeof(m));
      snprintf(line, sizeof(line), "TAB/C: NEXT = %s", m);
      canvas->setBrushColor(fabgl::Color::Blue);
      canvas->fillRectangle(0, 228, SCREEN_W - 1, 235);
      canvas->setPenColor(fabgl::Color::BrightCyan);
      canvas->drawText(8, 228, line);
    };
#endif
    auto draw = [&]() {
      if (cursor < listTop) listTop = cursor;
      if (cursor >= listTop + visible) listTop = cursor - visible + 1;
      const int top = listTop;
      if (shownTop < 0) {
        canvas->setBrushColor(fabgl::Color::Blue);
        canvas->clear();
        canvas->setPenColor(fabgl::Color::White);
        canvas->drawText(8, 4, "PC ENGINE  SELECT A ROM");
        canvas->drawText(8, 12, "ENTER:OK  BS:UP  (JOY: 1=OK 2=UP)");
        String shown = path + "/";
        if (shown.length() > 38) shown = "..." + shown.substring(shown.length() - 35);
        canvas->setPenColor(fabgl::Color::Cyan);
        canvas->drawText(8, 20, shown.c_str());
        if (noRom) {
          canvas->setPenColor(fabgl::Color::BrightRed);
          canvas->drawText(8, 40, "NO ROM IN " PCE_ROM_DIR);
        }
#if EMU_SWITCH
        drawNext();
#endif
      }
      if (shownTop < 0 || top != shownTop) {
        for (int i = 0; i < visible; i++) drawItem(top + i, top);
      } else if (cursor != shownCursor) {
        drawItem(shownCursor, top);
        drawItem(cursor, top);
      }
      shownTop = top;
      shownCursor = cursor;
    };
    draw();

    while (!changeDir) {
      int jsc = 0;
      if (!kb->scancodeAvailable() && (jsc = joyMenuScancode(&isE0)) != 0) isBreak = false;
      if (jsc || kb->scancodeAvailable()) {
        int sc = jsc ? jsc : kb->getNextScancode(0);
        if (sc == 0xF0) { isBreak = true; continue; }
        if (sc == 0xE0) { isE0 = true; continue; }
        if (!isBreak) {
          const int n = (int)romItems.size();
          if (sc == 0x75 && isE0) { if (cursor > 0) cursor--; draw(); }
          else if (sc == 0x72 && isE0) { if (cursor < n - 1) cursor++; draw(); }
          else if (sc == 0x7D && isE0) { cursor = cursor - visible < 0 ? 0 : cursor - visible; draw(); }
          else if (sc == 0x7A && isE0) { cursor = cursor + visible >= n ? n - 1 : cursor + visible; if (cursor < 0) cursor = 0; draw(); }
#if EMU_SWITCH
          else if (sc == 0x0D) { emuNext = emuNextCycle(emuNext, "PCE"); drawNext(); }
#endif
          else if (n > 0 && ((sc == 0x66 || sc == 0x76) && sub != "")) {
            int p = sub.lastIndexOf('/');
            want = p >= 0 ? sub.substring(p + 1) : sub;
            sub = p >= 0 ? sub.substring(0, p) : "";
            changeDir = true;
          } else if (n > 0 && sc == 0x5A && romItems[cursor].kind == 3) {
            padSwap = !padSwap;
            savePadLayout();
            drawItem(cursor, listTop);
          } else if (n > 0 && sc == 0x5A && romItems[cursor].kind == 0) {
            int p = sub.lastIndexOf('/');
            want = p >= 0 ? sub.substring(p + 1) : sub;
            sub = p >= 0 ? sub.substring(0, p) : "";
            changeDir = true;
          } else if (n > 0 && sc == 0x5A && romItems[cursor].kind == 1) {
            sub = sub == "" ? romItems[cursor].name : sub + "/" + romItems[cursor].name;
            want = "";
            changeDir = true;
          } else if (n > 0 && sc == 0x5A) {
            return path + "/" + romItems[cursor].name;
          }
        }
        isBreak = false; isE0 = false;
      }
      vTaskDelay(1);
    }
  }
}

static uint8_t *loadRom(const String &path, uint32_t *size) {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return nullptr;
  uint32_t sz = f.size();
  if (sz < 0x2000 || sz > PCE_ROM_MAX) { f.close(); return nullptr; }
  uint8_t *buf = (uint8_t *)ps_malloc(sz);
  if (!buf) { f.close(); return nullptr; }
  uint32_t done = 0;
  while (done < sz) {
    size_t n = f.read(buf + done, std::min((uint32_t)16384, sz - done));
    if (n == 0) break;
    done += n;
  }
  f.close();
  if (done != sz) { free(buf); return nullptr; }
  *size = sz;
  return buf;
}
#include "soc/i2s_struct.h"
static lldesc_t volatile *vgaDesc = nullptr;
static int vgaDescCount = 0;
static uint8_t *vgaDescRow = nullptr;

static inline int vgaRowNow() {
  uint32_t a = I2S1.out_link_dscr;
  uint32_t i = (a - (uint32_t)vgaDesc) / sizeof(lldesc_t);
  return i < (uint32_t)vgaDescCount ? vgaDescRow[i] : SCREEN_H;
}

static void vgaSyncSetup() {
  lldesc_t volatile *start = (lldesc_t volatile *)I2S1.out_link_dscr;
  if (!start) { Serial.println("VGA sync: off (no DMA descriptor)"); return; }
  lldesc_t volatile *best = nullptr;
  int bestLen = 0, n = 0;
  lldesc_t volatile *d = start;
  do {
    if ((uint32_t)d < 0x3FF80000u || (uint32_t)d >= 0x40000000u) { Serial.println("VGA sync: off (bad descriptor)"); return; }
    lldesc_t volatile *next = (lldesc_t volatile *)d->qe.stqe_next;
    lldesc_t volatile *prevCont = d - 1;
    bool head = true;
    if ((uint32_t)prevCont >= 0x3FF80000u && (lldesc_t volatile *)prevCont->qe.stqe_next == d) head = false;
    if (head) {
      int len = 1;
      lldesc_t volatile *e = d;
      while ((lldesc_t volatile *)e->qe.stqe_next == e + 1 && len < 4096) { e++; len++; }
      if (len > bestLen) { bestLen = len; best = d; }
    }
    d = next;
    n++;
  } while (d && d != start && n < 4096);
  if (d != start || !best) { Serial.println("VGA sync: off (not a ring)"); return; }
  vgaDescRow = (uint8_t *)heap_caps_malloc(bestLen, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (!vgaDescRow) { Serial.println("VGA sync: off (no memory)"); return; }
  int lastVis = -1, row = 0, views = 0;
  for (int i = 0; i < bestLen; i++) {
    const volatile uint8_t *b = (const volatile uint8_t *)best[i].buf;
    for (int y = 0; y < SCREEN_H; y++) if (b == vpRows[y]) { row = y; lastVis = i; views++; break; }
    vgaDescRow[i] = (uint8_t)row;
  }
  for (int i = lastVis + 2; i < bestLen; i++) vgaDescRow[i] = SCREEN_H;
  if (views < SCREEN_H) {
    Serial.printf("VGA sync: off (%d of %d descriptors are rows)\n", views, bestLen);
    heap_caps_free(vgaDescRow); vgaDescRow = nullptr;
    return;
  }
  vgaDesc = best;
  vgaDescCount = bestLen;
  int seenVis = 0, seenBlank = 0;
  for (int64_t t0 = esp_timer_get_time(); esp_timer_get_time() - t0 < 20000; ) {
    if (vgaRowNow() < SCREEN_H) seenVis++; else seenBlank++;
    ets_delay_us(50);
  }
  if (!seenVis || !seenBlank) {
    Serial.printf("VGA sync: off (position test failed: visible %d, blank %d)\n", seenVis, seenBlank);
    vgaDesc = nullptr;
    return;
  }
  Serial.printf("VGA sync: on (%d desc, %d rows, test visible %d / blank %d)\n", bestLen, views, seenVis, seenBlank);
}

static void vgaWaitVblank(int64_t frameStart) {
  for (;;) {
    int64_t el = esp_timer_get_time() - frameStart;
    if (el >= 17500) return;
    if (el < 8000) { vTaskDelay(pdMS_TO_TICKS((8000 - el) / 1000 + 1)); continue; }
    int r = vgaRowNow();
    if (r >= SCREEN_H) return;
    int us = (SCREEN_H - r) * 64;
    if (us > 2500) vTaskDelay(pdMS_TO_TICKS(us / 1000 - 1));
  }
}

static int shownW = 256, shownH = 224;
static int nextW = 256, nextH = 224;

static inline uint32_t swapHalves(uint32_t w) { return (w >> 16) | (w << 16); }

static void IRAM_ATTR putRow(const uint8_t *srcLine, int r, int width, int height) {
  const int fbH = height > XBUF_HEIGHT ? XBUF_HEIGHT : height;
  const int h = fbH > SCREEN_H ? SCREEN_H : fbH;
  const int top = (SCREEN_H - h) / 2;
  const int srcTop = (fbH - h) / 2;
  const int k = r - srcTop;
  if (k < 0 || k >= h) return;
  uint32_t *d = (uint32_t *)vpRows[top + k];
  const int w = width > XBUF_WIDTH - 16 ? XBUF_WIDTH - 16 : width;
  if (w >= SCREEN_W) {
    const uint32_t *s = (const uint32_t *)(srcLine + (w - SCREEN_W) / 2);
    for (int i = 0; i < SCREEN_W / 4; i++) d[i] = swapHalves(s[i]);
  } else {
    const int xofs = (SCREEN_W - w) / 2;
    const uint32_t bw = pce_bg_pix() * 0x01010101u;
    for (int i = 0; i < xofs / 4; i++) { d[i] = bw; d[(SCREEN_W - xofs) / 4 + i] = bw; }
    const uint32_t *s = (const uint32_t *)srcLine;
    for (int i = 0; i < w / 4; i++) d[xofs / 4 + i] = swapHalves(s[i]);
  }
}

extern "C" void IRAM_ATTR pce_band_done(const uint8_t *buf, int y0, int y1, int width, int height) {
  for (int r = y0; r < y1; r++) putRow(buf + r * XBUF_WIDTH, r, width, height);
}

extern "C" void IRAM_ATTR pce_line_done(int scanline) {
  pce_line_abs++;
  pce_emu_time = pce_line_abs << 10;
  if (scanline == SCREEN_H) frameReady = true;
}

static void clearOutside(int h) {
  const uint8_t black = DisplayController.createRawPixel(fabgl::RGB222(0, 0, 0));
  if (h > XBUF_HEIGHT) h = XBUF_HEIGHT;
  if (h > SCREEN_H) h = SCREEN_H;
  const int top = (SCREEN_H - h) / 2;
  for (int y = 0; y < SCREEN_H; y++) if (y < top || y >= top + h) memset(vpRows[y], black, SCREEN_W);
}

#define PCE_FRAME_US 16683

static int clearedH = -1;
static void doStateRequest(int req) {
  const int slot = req & 0x0F;
  const bool save = (req & 0x80) != 0;
  String name = romPath.substring(romPath.lastIndexOf('/') + 1);
  const int dot = name.lastIndexOf('.');
  if (dot > 0) name = name.substring(0, dot);
  const String path = String(PCE_DIR "/SAVE/") + name + "_" + String(slot) + ".sta";
  const String vfs = String("/sd") + path;
  char msg[40];
  if (save) {
    if (!SD.exists(PCE_DIR "/SAVE")) SD.mkdir(PCE_DIR "/SAVE");
    snprintf(msg, sizeof(msg), SaveState(vfs.c_str()) == 0 ? "STATE %d SAVED" : "STATE %d SAVE ERROR", slot);
  } else if (!SD.exists(path)) {
    snprintf(msg, sizeof(msg), "NO STATE %d", slot);
  } else if (LoadState(vfs.c_str()) == 0) {
    pce_psg_restore();
    clearedH = -1;
    snprintf(msg, sizeof(msg), "STATE %d LOADED", slot);
  } else snprintf(msg, sizeof(msg), "STATE %d LOAD ERROR", slot);
  Serial.println(msg);
  flashStatus(msg, 1000);
}

void cpuTask(void *pvParameters) {
  const int64_t frameTimeUs = PCE_FRAME_US;
  int64_t nextFrameTime = esp_timer_get_time();
  bool renderSkipped = false;
  bool gaveUp = false;
  bool noWait = false, prevNoWait = false;

#ifdef USE_SERIAL_DEBUG
  int64_t dbgMin = INT64_MAX, dbgMax = 0, dbgSum = 0, dbgWinStart = 0, dbgIdle = 0;
  int dbgOverBudget = 0, dbgFrameCnt = 0, dbgSkipped = 0;
#endif

  while (1) {
    gfx_render_wait();
    if (stateReq) { const int r = stateReq; stateReq = 0; doStateRequest(r); }
    if (statusUntil && !isPaused) showStatus(statusMsg);
    if (isPaused) {
      cpuStopped = true;
      vTaskDelay(pdMS_TO_TICKS(10));
      nextFrameTime = esp_timer_get_time();
      continue;
    }
    cpuStopped = false;
#ifdef USE_SERIAL_DEBUG
    int64_t frameStart = esp_timer_get_time();
    if (dbgFrameCnt == 0) dbgWinStart = frameStart;
#endif
    updateJoystick(prevNoWait);
    prevNoWait = noWait;
    updatePad();

    const bool late = !renderSkipped && (gaveUp || esp_timer_get_time() > nextFrameTime + 1000);
    gaveUp = false;
    pce_render_skip = late;
    renderSkipped = late;
#ifdef USE_SERIAL_DEBUG
    if (late) dbgSkipped++;
#endif
    nextFrameTime += frameTimeUs;

    if (!late) { shownW = nextW = pce_screen_width(); shownH = nextH = pce_screen_height(); }
    if (shownH != clearedH) { clearOutside(shownH); clearedH = shownH; }
    pce_frame();

#ifdef USE_SERIAL_DEBUG
    int64_t elapsed = esp_timer_get_time() - frameStart;
    if (elapsed < dbgMin) dbgMin = elapsed;
    if (elapsed > dbgMax) dbgMax = elapsed;
    dbgSum += elapsed;
    if (elapsed > frameTimeUs) dbgOverBudget++;
    if (++dbgFrameCnt >= 300) {
      const int64_t wall = esp_timer_get_time() - dbgWinStart;
      Serial.printf("SPEED: %d%% of real | frame us min=%lld max=%lld avg=%lld over=%d/%d (budget %lld) | idle avg=%lldus | skipped %d | %dx%d\n",
                    (int)((int64_t)dbgFrameCnt * frameTimeUs * 100 / (wall > 0 ? wall : 1)),
                    (long long)dbgMin, (long long)dbgMax, (long long)(dbgSum / dbgFrameCnt), dbgOverBudget, dbgFrameCnt,
                    (long long)frameTimeUs, (long long)(dbgIdle / dbgFrameCnt), dbgSkipped, nextW, nextH);
      dbgMin = INT64_MAX; dbgMax = dbgSum = dbgIdle = 0;
      dbgOverBudget = dbgFrameCnt = dbgSkipped = 0;
    }
#endif

    int64_t now = esp_timer_get_time();
    int64_t waitTime = nextFrameTime - now;
    noWait = waitTime <= 0;
    if (vgaDesc) {
      if (waitTime > 0) {
        vgaWaitVblank(nextFrameTime - frameTimeUs);
        nextFrameTime = esp_timer_get_time();
      } else if (waitTime < -2 * frameTimeUs) {
        nextFrameTime = now;
        gaveUp = true;
      }
    } else if (waitTime > 0) {
      if (waitTime >= 1000) vTaskDelay(pdMS_TO_TICKS(waitTime / 1000));
      while (esp_timer_get_time() < nextFrameTime) asm volatile("nop");
    } else if (waitTime < -2 * frameTimeUs) {
      nextFrameTime = now;
      gaveUp = true;
    }
#ifdef USE_SERIAL_DEBUG
    if (waitTime > 0) dbgIdle += esp_timer_get_time() - now;
#endif
  }
}

#ifndef BOARD_HAS_PSRAM
bool psramEnable() {
  if (esp_spiram_init() != ESP_OK) return false;
  esp_spiram_init_cache();
  if (!esp_spiram_test()) return false;
  if (esp_spiram_add_to_heapalloc() != ESP_OK) return false;
  heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
  return true;
}
#endif

static void haltMessage(const char *l1, const char *l2) {
  Serial.printf("%s %s\n", l1, l2 ? l2 : "");
  canvas->selectFont(&fabgl::FONT_8x8);
  canvas->setPenColor(fabgl::Color::BrightRed);
  canvas->drawText(8, 16, l1);
  if (l2) canvas->drawText(8, 32, l2);
  while (1) delay(1000);
}

void setup() {
  Serial.begin(115200);
#ifndef BOARD_HAS_PSRAM
  Serial.printf("PSRAM: %s\n", psramEnable() ? "enabled by the sketch" : "init FAILED");
#endif
  setCpuFrequencyMhz(240);
  disableCore0WDT();
  disableCore1WDT();
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  esp_task_wdt_delete(NULL);
  delay(100);

  PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1, fabgl::KbdMode::NoVirtualKeys);
  PS2Controller.keyboard()->begin(GPIO_NUM_33, GPIO_NUM_32, false, false);
  PS2Controller.keyboard()->setLEDs(false, false, false);
  Serial1.begin(115200, SERIAL_8N1, 39, -1);

  DisplayController.begin();
  DisplayController.setResolution(QVGA_320x240_60Hz);
  canvas = new fabgl::Canvas(&DisplayController);
  canvas->setBrushColor(fabgl::Color::Black);
  canvas->clear();

  if (psramFixMissing()) {
    char msg[48];
    snprintf(msg, sizeof(msg), "ESP32 REV.%d NEEDS THE PSRAM BUG FIX", ESP.getChipRevision());
    haltMessage(msg, "TOOLS > PSRAM > \"Enabled\", BUILD AGAIN");
  }

  pce_vga_sync = DisplayController.createRawPixel(fabgl::RGB222(0, 0, 0)) & 0xC0;
  if (InitPCE(16384, false) != 0) haltMessage("MEMORY ALLOCATION FAILED", nullptr);
  Serial.printf("internal RAM free: %u (largest %u), PSRAM free: %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  pceSoundBegin(16384, PCE_FRAME_US);
  soundGenerator.attach(&pceWave);
  pceWave.setSampleRate(16384);
  pceWave.enable(true);
  soundGenerator.setVolume(100);
  soundGenerator.play(true);

  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI)) haltMessage("SD CARD NOT FOUND", nullptr);
#if EMU_SWITCH
  emuSwitchCheck("PCE");
#endif
  uint32_t romSize = 0;
  uint8_t *rom = nullptr;
  while (!rom) {
    String path = selectRom();
    canvas->setBrushColor(fabgl::Color::Blue);
    canvas->clear();
    canvas->setPenColor(fabgl::Color::White);
    canvas->drawText(8, 16, "LOADING...");
    rom = loadRom(path, &romSize);
    if (!rom) {
      canvas->setPenColor(fabgl::Color::BrightRed);
      canvas->drawText(8, 32, "ROM LOAD ERROR (MAX 4MB)");
      delay(1500);
    } else { Serial.printf("ROM %s: %u bytes\n", path.c_str(), (unsigned)romSize); romPath = path; }
  }

  canvas->setBrushColor(fabgl::Color::Black);
  canvas->clear();
  DisplayController.processPrimitives();
  canvas->waitCompletion();
  for (int y = 0; y < SCREEN_H; y++) {
    vpRows[y] = DisplayController.getScanline(y);
    if (!vpRows[y] || ((uintptr_t)vpRows[y] & 3)) {
      Serial.printf("VIEWPORT row %d is NULL or unaligned (%p)\n", y, vpRows[y]);
      while (1) delay(1000);
    }
  }
  vgaSyncSetup();

  if (LoadCard(rom, romSize) != 0) haltMessage("ROM NOT SUPPORTED", nullptr);

  pce_render_async = xPortGetCoreID() != 0;
  Serial.printf("render on core %d\n", pce_render_async ? xPortGetCoreID() : 0);
  Serial.println("PC ENGINE Booting...");
#ifndef USE_SERIAL_DEBUG
  Serial.end();
#endif
  xTaskCreatePinnedToCore(cpuTask, "cpuTask", 8192, NULL, 24, NULL, 0);
}

static void statusTick() {
  if (!statusUntil || isPaused || (int32_t)(millis() - statusUntil) < 0) return;
  statusUntil = 0;
  clearedH = -1;
}

void loop() {
  if (pce_render_async) {
    static int64_t lastFill = 0;
    gfx_render_service();
    const int64_t now = esp_timer_get_time();
    if (now - lastFill < 500) return;
    lastFill = now;
    pceSoundFill();
    if (frameReady || isPaused) {
      frameReady = false;
      updateKeyboard();
      statusTick();
    }
    return;
  }
  pceSoundFill();
  if (frameReady || isPaused) {
    frameReady = false;
    updateKeyboard();
    statusTick();
  }
  vTaskDelay(1);
}
