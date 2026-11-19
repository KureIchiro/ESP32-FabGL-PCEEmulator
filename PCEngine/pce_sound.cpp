// ============================================================================
//  PC エンジンの音: PSG(6 チャンネル。32 サンプルの波形メモリ、ch5・6 はノイズ、DDA)。
//  メガドライブ版・MZ-2500 版と同じ作り:
//   ・エミュレーション側(コア0)は PSG のレジスタの書き込み(0800h〜080Fh)を時刻付きでキューに積むだけ
//     (pce.c の pce_writeIO → pce_glue.c の pce_psg_queue)。CPU が読む値は pce-go の PCE.PSG がそのまま持つ
//   ・サウンド側(コア1 の loop)がキューを読み、その時刻のサンプルで反映しながら 16384Hz で作る。
//     DDA(CPU が 1 サンプルずつ書く PCM)も書いた時刻どおりに鳴る(psg.c は 1 フレームぶんを並べて鳴らしていた)
//   ・音量: 全体(L/R 4 ビット)・チャンネルの左右(4 ビット)は 1 段 3dB、チャンネルの音量(5 ビット)は 1 段 1.5dB。
//     出力はモノラル(左右の平均)。LFO は psg.c と同じく扱わない
//  時刻の単位は pce_glue.h(1 ライン = 1024)
// ============================================================================
#pragma GCC optimize("O3")

#include "pce_glue.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#define PCE_SND_FAST IRAM_ATTR
#else
#define PCE_SND_FAST
#endif

extern "C" {
volatile uint32_t pce_emu_time = 0;
volatile bool pce_sound_mute = false;
}

static const int QUEUE = 4096;
static uint32_t *qTime = nullptr;
static uint16_t *qData = nullptr;
static volatile int qHead = 0, qTail = 0;

extern "C" void PCE_SND_FAST pce_snd_push(uint32_t t, uint8_t reg, uint8_t val) {
  int next = (qHead + 1) & (QUEUE - 1);
  if (next == qTail || !qTime) return;
  qTime[qHead] = t;
  qData[qHead] = (uint16_t)((reg << 8) | val);
  qHead = next;
}

static const uint32_t PSG_CLOCK = 3579545;
static struct Chan {
  uint16_t freq; uint8_t control, balance, noise;
  uint8_t wave[32]; uint8_t waveWr;
  uint32_t acc, inc;
  uint8_t dda;
  int32_t noiseAcc, noiseLevel; uint32_t noiseRand;
  int32_t amp;
} ch[6];
static uint8_t sel = 0, globalVol = 0;
static int sndRate = 16384;
static int16_t ampTab[96];

static void updateAmp(int c) {
  const Chan &h = ch[c];
  const int cv = h.control & 0x1F;
  const int gl = globalVol >> 4, gr = globalVol & 15, bl = h.balance >> 4, br = h.balance & 15;
  int al = 2 * (15 - gl) + 2 * (15 - bl) + (31 - cv), ar = 2 * (15 - gr) + 2 * (15 - br) + (31 - cv);
  int vl = (gl && bl && cv && al < 96) ? ampTab[al] : 0, vr = (gr && br && cv && ar < 96) ? ampTab[ar] : 0;
  ch[c].amp = (vl + vr) / 2;
}
static void updateInc(int c) {
  const uint32_t tp = ch[c].freq;
  ch[c].inc = tp ? (uint32_t)(((uint64_t)PSG_CLOCK << 16) / ((uint64_t)tp * (uint32_t)sndRate)) : 0;
}

static void psgReset() {
  memset(ch, 0, sizeof(ch));
  for (int i = 0; i < 6; i++) ch[i].control = 0x80;
  ch[4].noiseRand = 0x51F63101;
  ch[5].noiseRand = 0x1F631042;
  sel = 0; globalVol = 0;
  for (int i = 0; i < 6; i++) { updateAmp(i); updateInc(i); }
}

static void psgWrite(uint8_t reg, uint8_t v) {
  Chan &h = ch[sel];
  switch (reg) {
    case 0: sel = (v & 7) > 5 ? 5 : (v & 7); break;
    case 1: globalVol = v; for (int i = 0; i < 6; i++) updateAmp(i); break;
    case 2: h.freq = (uint16_t)((h.freq & 0xF00) | v); updateInc(sel); break;
    case 3: h.freq = (uint16_t)((h.freq & 0x0FF) | ((v & 15) << 8)); updateInc(sel); break;
    case 4:
      if ((v & 0xC0) == 0x40) h.waveWr = 0;
      if (!(v & 0x80)) h.acc = 0;
      h.control = v;
      updateAmp(sel);
      break;
    case 5: h.balance = v; updateAmp(sel); break;
    case 6:
      if ((h.control & 0xC0) == 0) { h.wave[h.waveWr] = v & 0x1F; h.waveWr = (h.waveWr + 1) & 31; }
      else if ((h.control & 0xC0) == 0xC0) h.dda = v & 0x1F;
      break;
    case 7: h.noise = v; break;
    default: break;
  }
}

static inline int PCE_SND_FAST psgSample() {
  int32_t sum = 0;
  for (int c = 0; c < 6; c++) {
    Chan &h = ch[c];
    if (!(h.control & 0x80) || !h.amp) continue;
    int s;
    if (c >= 4 && (h.noise & 0x80)) {
      h.noiseAcc += 3000 + (h.noise & 0x1F) * 512;
      if (h.noiseAcc >= sndRate) {
        int n = h.noiseAcc / sndRate;
        h.noiseAcc -= sndRate * n;
        if (h.noiseRand & 0x00080000) { h.noiseRand = ((h.noiseRand ^ 0x0004) << 1) + 1; h.noiseLevel = -15; }
        else { h.noiseRand <<= 1; h.noiseLevel = 15; }
      }
      s = h.noiseLevel;
    } else if (h.control & 0x40) {
      s = (int)h.dda - 16; if (s >= 0) s++;
    } else if (h.inc) {
      s = (int)h.wave[(h.acc >> 16) & 31] - 16; if (s >= 0) s++;
      h.acc = (h.acc + h.inc) & 0x1FFFFF;
    } else continue;
    sum += s * h.amp;
  }
  return sum >> 8;
}

static uint32_t tPerSampleQ16 = 0;
static uint32_t sndTime = 0, sndFrac = 0;
static bool sndStarted = false;
static const int BUF = 512;
static int16_t buf[BUF];
static volatile int head = 0, tail = 0;
static int16_t lastSample = 0;
static int32_t dcPrev = 0, dcOut = 0;

extern "C" {

void pceSoundBegin(int rate, uint32_t frameUs) {
  sndRate = rate > 0 ? rate : 16384;
  qTime = (uint32_t *)heap_caps_malloc(sizeof(uint32_t) * QUEUE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  qData = (uint16_t *)heap_caps_malloc(sizeof(uint16_t) * QUEUE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  for (int a = 0; a < 96; a++) ampTab[a] = (int16_t)(384.0 * pow(10.0, -a * 1.5 / 20.0) + 0.5);
  tPerSampleQ16 = (uint32_t)((((uint64_t)PCE_LINES_PER_FRAME * PCE_T_PER_LINE * 1000000u) << 16) / ((uint64_t)frameUs * (uint32_t)sndRate));
  psgReset();
}

void PCE_SND_FAST pceSoundFill(void) {
  if (!qTime) return;
  int space = (tail - head - 1 + BUF) % BUF;
  if (pce_sound_mute) {
    while (qTail != qHead) { psgWrite(qData[qTail] >> 8, qData[qTail] & 0xFF); qTail = (qTail + 1) & (QUEUE - 1); }
    while (space-- > 0) { buf[head] = 0; head = (head + 1) % BUF; }
    sndStarted = false;
    return;
  }
  const uint32_t frameT = PCE_LINES_PER_FRAME * PCE_T_PER_LINE;
  const uint32_t emu = pce_emu_time;
  if (!sndStarted || (int32_t)(emu - sndTime) > (int32_t)(frameT + frameT / 4)) {
    while (qTail != qHead) { psgWrite(qData[qTail] >> 8, qData[qTail] & 0xFF); qTail = (qTail + 1) & (QUEUE - 1); }
    sndTime = emu - frameT; sndFrac = 0;
    sndStarted = true;
  }
  while (space > 0) {
    while (qTail != qHead && (int32_t)(qTime[qTail] - sndTime) <= 0) {
      psgWrite(qData[qTail] >> 8, qData[qTail] & 0xFF);
      qTail = (qTail + 1) & (QUEUE - 1);
    }
    uint32_t next = sndTime + ((sndFrac + tPerSampleQ16) >> 16);
    if ((int32_t)(next - emu) > 0) break;
    sndFrac = (sndFrac + tPerSampleQ16) & 0xFFFF;
    sndTime = next;
    int32_t y = psgSample() << 8;
    dcOut = y - dcPrev + dcOut - (dcOut >> 8);
    dcPrev = y;
    int32_t s = dcOut >> 8;
    if (s > 96) s = 96 + (s - 96) / 3; else if (s < -96) s = -96 + (s + 96) / 3;
    if (s > 127) s = 127; else if (s < -128) s = -128;
    buf[head] = (int16_t)s;
    head = (head + 1) % BUF;
    space--;
  }
}

int PCE_SND_FAST pceSoundGetSample(void) {
  if (head == tail) return lastSample;
  lastSample = buf[tail];
  tail = (tail + 1) % BUF;
  return lastSample;
}

}
