#pragma once
#include "config.h"
#include "raycast.h"
#include <cstdint>

namespace Weapon {

struct Def {
  uint8_t  type;           // 0-3
  bool     isCharged;      // 蓄力型
  bool     isThrowable;    // 投掷物
  uint8_t  damage;         // 基础伤害（每弹丸/小子弹）
  uint8_t  fireRate;       // 发射间隔（网络帧）
  uint8_t  splashRadius;   // 溅射半径（网格）
  uint8_t  range;          // 射程（网格，蓄力型为下限）
  uint8_t  rangeMax;       // 蓄力射程上限
  uint8_t  splashMin;      // 蓄力溅射下限
  uint8_t  splashMax;      // 蓄力溅射上限
  uint8_t  damageMax;      // 蓄力伤害上限
  uint8_t  pellets;        // 霰弹弹丸数
  int8_t   aimAngle;       // 自瞄角度 ±(0-255制)
};

extern const Def weaponDefs[WPN_COUNT];

// 武器状态（嵌入 Player 或独立管理）
struct State {
  uint8_t  wpnId;
  uint8_t  cooldown;       // 剩余冷却（网络帧）
  uint8_t  chargeLevel;    // 0-255
  bool     charging;
};

// 武器开火结果
struct FireResult {
  Raycast::Result rayResult;
  uint8_t  wpnId;
  uint8_t  fireDir;        // 实际发射方向
  bool     fired;          // 是否成功开火
  uint8_t  param1;         // 武器参数1 (榴弹=landX, 狙击=实际射程)
  uint8_t  param2;         // 武器参数2 (榴弹=landY>>1, 狙击=splashRadius)
  uint8_t  param3;         // 武器参数3 (榴弹=splashRadius)
};

// 开始蓄力（蓄力型武器按下攻击键）
void startCharge(State& s);

// 更新蓄力（每网络帧，蓄力型武器）
void updateCharge(State& s);

// 开火（返回射线结果）
// joyMag: 摇杆幅度 0-255（榴弹用，控制投掷距离）
FireResult fire(State& s,
                int16_t shooterX, int16_t shooterY,
                uint8_t moveAngle,
                int16_t targetX, int16_t targetY,
                bool btnHeld, bool btnReleased,
                uint8_t joyMag);

// 自瞄计算：检测敌方是否在锥形范围内且不超过武器射程
// 返回：true=锁定，aimOut=锁定方向
bool calcAimAssist(int16_t shooterX, int16_t shooterY,
                   uint8_t moveAngle, int8_t aimCone,
                   int16_t targetX, int16_t targetY,
                   uint8_t maxGrids,
                   uint8_t& aimOut);

}  // namespace Weapon
