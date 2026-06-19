#include "audio.h"
#include <Arduino.h>

namespace Audio {

// 采样率
constexpr uint16_t SAMPLE_RATE = 20000;  // 20kHz
constexpr uint16_t MAX_SAMPLE = 4000;    // 最大音效样本数 (200ms)

// 当前播放状态
static int16_t* sfxData[SFX_COUNT] = {nullptr};
static uint16_t sfxLen[SFX_COUNT] = {0};
static uint8_t  sfxVol[SFX_COUNT] = {0};

static volatile const int16_t* curSamples = nullptr;
static volatile uint16_t curLen = 0;
static volatile uint16_t curPos = 0;
static volatile uint8_t  curVol = 0;

static hw_timer_t* timer = nullptr;
static bool initialized = false;

// ---- 波形生成辅助 ----
// 生成方波 + 指数衰减包络
static void genSquareDecay(int16_t* buf, uint16_t len,
                           uint16_t freqHz, uint8_t vol) {
  float period = (float)SAMPLE_RATE / freqHz;
  for (uint16_t i = 0; i < len; i++) {
    float env = 1.0f - (float)i / len;  // 线性衰减
    int16_t wave = ((int)(i / (period / 2)) % 2) ? 1 : -1;
    buf[i] = (int16_t)(wave * env * vol * 256);
  }
}

// 生成噪声 + 衰减（爆炸类）
static void genNoiseDecay(int16_t* buf, uint16_t len, uint8_t vol) {
  for (uint16_t i = 0; i < len; i++) {
    float env = 1.0f - (float)i / len;
    int16_t noise = (random(256) - 128) * 2;
    buf[i] = (int16_t)(noise * env * vol);
  }
}

// 生成频率扫描（上升/下降音调）
static void genSweep(int16_t* buf, uint16_t len,
                     uint16_t fStart, uint16_t fEnd, uint8_t vol) {
  for (uint16_t i = 0; i < len; i++) {
    float t = (float)i / len;
    float freq = fStart + (fEnd - fStart) * t;
    float period = (float)SAMPLE_RATE / freq;
    float env = 1.0f - t;
    int16_t wave = ((int)((float)i / (period / 2)) % 2) ? 1 : -1;
    buf[i] = (int16_t)(wave * env * vol * 256);
  }
}

// ---- 定时器 ISR ----
static void IRAM_ATTR onTimer() {
  if (curPos < curLen && curSamples) {
    int16_t sample = curSamples[curPos++] * curVol / 255;
    // 转为 0-255 范围送给 sigma-delta
    uint8_t out = (uint8_t)((sample >> 8) + 128);
    sigmaDeltaWrite(0, out);
  } else {
    sigmaDeltaWrite(0, 128);  // 静音中点
    curSamples = nullptr;
    curLen = 0;
    curPos = 0;
  }
}

void init() {
  if (initialized) return;

  // 预分配音效数据
  for (uint8_t i = 0; i < SFX_COUNT; i++) {
    sfxData[i] = (int16_t*)malloc(MAX_SAMPLE * sizeof(int16_t));
  }

  // 手枪射击：短促高频方波
  {
    uint16_t len = 800;   // 40ms
    genSquareDecay(sfxData[SFX_SHOOT_PISTOL], len, 800, 200);
    sfxLen[SFX_SHOOT_PISTOL] = len;
    sfxVol[SFX_SHOOT_PISTOL] = 200;
  }

  // 霰弹枪：更长更低
  {
    uint16_t len = 1600;  // 80ms
    genSquareDecay(sfxData[SFX_SHOOT_SHOTGUN], len, 300, 220);
    sfxLen[SFX_SHOOT_SHOTGUN] = len;
    sfxVol[SFX_SHOOT_SHOTGUN] = 220;
  }

  // 狙击枪：低频长音 + 回响
  {
    uint16_t len = 2000;  // 100ms
    genSweep(sfxData[SFX_SHOOT_SNIPER], len, 400, 100, 180);
    sfxLen[SFX_SHOOT_SNIPER] = len;
    sfxVol[SFX_SHOOT_SNIPER] = 180;
  }

  // 榴弹发射：中频上扫
  {
    uint16_t len = 1200;
    genSweep(sfxData[SFX_GRENADE_LAUNCH], len, 300, 600, 180);
    sfxLen[SFX_GRENADE_LAUNCH] = len;
    sfxVol[SFX_GRENADE_LAUNCH] = 180;
  }

  // 爆炸：白噪声
  {
    uint16_t len = 3000;  // 150ms
    genNoiseDecay(sfxData[SFX_EXPLOSION], len, 255);
    sfxLen[SFX_EXPLOSION] = len;
    sfxVol[SFX_EXPLOSION] = 255;
  }

  // 自爆：更大噪声
  {
    uint16_t len = 4000;
    genNoiseDecay(sfxData[SFX_SELFDESTRUCT], len, 255);
    sfxLen[SFX_SELFDESTRUCT] = len;
    sfxVol[SFX_SELFDESTRUCT] = 255;
  }

  // 命中：短促中频
  {
    uint16_t len = 600;
    genSquareDecay(sfxData[SFX_HIT], len, 500, 180);
    sfxLen[SFX_HIT] = len;
    sfxVol[SFX_HIT] = 180;
  }

  // 潜入开启：上升音
  {
    uint16_t len = 1000;
    genSweep(sfxData[SFX_STEALTH_ON], len, 200, 600, 150);
    sfxLen[SFX_STEALTH_ON] = len;
    sfxVol[SFX_STEALTH_ON] = 150;
  }

  // 潜入关闭：下降音
  {
    uint16_t len = 800;
    genSweep(sfxData[SFX_STEALTH_OFF], len, 600, 200, 150);
    sfxLen[SFX_STEALTH_OFF] = len;
    sfxVol[SFX_STEALTH_OFF] = 150;
  }

  // 冲撞：快速上扫
  {
    uint16_t len = 600;
    genSweep(sfxData[SFX_DASH], len, 100, 1000, 200);
    sfxLen[SFX_DASH] = len;
    sfxVol[SFX_DASH] = 200;
  }

  // 盾开启：低频持续
  {
    uint16_t len = 1200;
    genSquareDecay(sfxData[SFX_SHIELD_ON], len, 200, 160);
    sfxLen[SFX_SHIELD_ON] = len;
    sfxVol[SFX_SHIELD_ON] = 160;
  }

  // 盾破裂：玻璃碎裂感
  {
    uint16_t len = 1500;
    genNoiseDecay(sfxData[SFX_SHIELD_BREAK], len, 200);
    sfxLen[SFX_SHIELD_BREAK] = len;
    sfxVol[SFX_SHIELD_BREAK] = 200;
  }

  // 死亡：低频长降
  {
    uint16_t len = 2500;
    genSweep(sfxData[SFX_DEATH], len, 300, 50, 200);
    sfxLen[SFX_DEATH] = len;
    sfxVol[SFX_DEATH] = 200;
  }

  // 初始化 SigmaDelta
  sigmaDeltaSetup(PIN_AUDIO, 0, 312500);  // 312.5kHz 基频（ESP32-S3 固定）

  // 初始化硬件定时器 (20kHz)
  timer = timerBegin(0, 80, true);  // 80 分频 = 1MHz
  timerAttachInterrupt(timer, onTimer, true);
  timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);  // 20kHz
  timerAlarmEnable(timer);

  initialized = true;
}

void play(SFX sfx) {
  if (!initialized || sfx >= SFX_COUNT) return;
  if (!sfxData[sfx]) return;

  curSamples = sfxData[sfx];
  curLen = sfxLen[sfx];
  curVol = sfxVol[sfx];
  curPos = 0;
}

void stop() {
  curSamples = nullptr;
  curLen = 0;
  curPos = 0;
}

bool isPlaying() {
  return curSamples != nullptr && curPos < curLen;
}

}  // namespace Audio
