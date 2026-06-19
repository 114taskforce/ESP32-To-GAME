#pragma once
#include "config.h"
#include "entity.h"
#include "weapon.h"
#include <cstdint>

namespace Game {

enum State : uint8_t {
  STATE_SELECT    = 0,  // 赛前选武器&技能
  STATE_COUNTDOWN = 1,  // 3秒倒计时
  STATE_PLAYING   = 2,  // 对战 (90s)
  STATE_OVER      = 3,  // 结算
  STATE_CALIBRATE = 4,  // 摇杆校准
};

extern State state;

// 本地赛前选择
extern uint8_t selectedWpn;   // 0-3
extern uint8_t selectedSkill; // 0-2
extern bool    localReady;    // 已确认选择
extern bool    remoteReady;   // 对方已确认

// 倒计时 & 比赛计时
extern uint16_t countdownTimer; // 网络帧
extern uint16_t matchTimer;     // 网络帧

// 结算数据
extern uint16_t allyCells;    // 己方涂色格数
extern uint16_t enemyCells;   // 敌方涂色格数

// 武器状态
extern Weapon::State localWpn;

void init(bool spawnTop);

// 重启游戏（回到选择界面，保留出生点）
void restart();

// 每网络帧调用（12fps）
void updateNetwork();

// Core 0 渲染（每帧调用，24fps）— 仅绘图，不修改状态
void doRender();

// Core 1 渲染帧后处理（24fps）— snapshot + 缓冲交换
void finishRender();

}  // namespace Game
