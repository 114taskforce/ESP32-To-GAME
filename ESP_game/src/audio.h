#pragma once
#include "config.h"
#include <cstdint>

namespace Audio {

// 音效 ID
enum SFX : uint8_t {
  SFX_SHOOT_PISTOL   = 0,
  SFX_SHOOT_SHOTGUN  = 1,
  SFX_SHOOT_SNIPER   = 2,
  SFX_GRENADE_LAUNCH = 3,
  SFX_EXPLOSION      = 4,
  SFX_HIT            = 5,
  SFX_STEALTH_ON     = 6,
  SFX_STEALTH_OFF    = 7,
  SFX_DASH           = 8,
  SFX_SHIELD_ON      = 9,
  SFX_SHIELD_BREAK   = 10,
  SFX_SELFDESTRUCT   = 11,
  SFX_DEATH          = 12,
  SFX_COUNT          = 13
};

// 初始化音频（定时器 + SigmaDelta）
void init();

// 播放音效（非阻塞，可打断）
void play(SFX sfx);

// 静音
void stop();

// 是否正在播放
bool isPlaying();

}  // namespace Audio
