#pragma once
#include <cstdint>

// ========== 屏幕 ==========
constexpr uint16_t SCR_W = 240;
constexpr uint16_t SCR_H = 320;

// ========== 地图网格 ==========
constexpr uint8_t  GRID_SIZE    = 4;       // 每格像素
constexpr uint16_t MAP_COLS     = SCR_W / GRID_SIZE;  // 60
constexpr uint16_t MAP_ROWS     = SCR_H / GRID_SIZE;  // 80
constexpr uint16_t MAP_CELLS    = MAP_COLS * MAP_ROWS; // 4800
constexpr uint16_t MAP_BYTES    = MAP_CELLS / 4;       // 1200 (2bit/cell, 4cells/byte)

// 格子颜色（2bit 编码）
constexpr uint8_t CLR_NEUTRAL  = 0x00;  // 00 中立
constexpr uint8_t CLR_ENEMY    = 0x01;  // 01 敌方
constexpr uint8_t CLR_ALLY     = 0x02;  // 10 己方
constexpr uint8_t CLR_WALL     = 0x03;  // 11 墙

// ========== 帧率 ==========
constexpr uint8_t  FPS_RENDER  = 24;
constexpr uint8_t  FPS_NETWORK = 12;
constexpr uint32_t FRAME_US    = 41667;

// ========== 玩家 ==========
constexpr uint8_t  PLAYER_SIZE = 7;
constexpr int16_t  HP_INITIAL  = 100;
constexpr int16_t  HP_STEALTH_REGEN = 12;  // 潜入每秒回复
constexpr int16_t  DMG_STEALTH_HIT  = 20;  // 潜入时被击中固定伤害

// 速度（px/s）
constexpr int16_t  SPD_BASE       = 60;
constexpr int16_t  SPD_STEALTH    = 120;  // ×2.0
constexpr int16_t  SPD_ENEMY_TILE = 54;   // ×0.9
constexpr int16_t  SPD_CHARGE_B   = 48;   // ×0.8
constexpr int16_t  SPD_CHARGE_C   = 36;   // ×0.6
constexpr int16_t  SPD_SHIELD     = 96;   // ×1.6

// ========== 摇杆 ==========
constexpr uint16_t JOY_MAX      = 4095;
constexpr uint16_t JOY_DEADZONE = 200;

// ========== 按钮 GPIO（请根据实际接线修改）==========
constexpr uint8_t  PIN_JOY_X   = 1;
constexpr uint8_t  PIN_JOY_Y   = 2;
constexpr uint8_t  PIN_BTN_A   = 4;   // 潜入
constexpr uint8_t  PIN_BTN_B   = 5;   // 攻击
constexpr uint8_t  PIN_BTN_C   = 6;   // 技能
constexpr uint8_t  PIN_BTN_D   = 7;   // 
constexpr uint8_t  PIN_AUDIO   = 17;

// ========== 武器编号 ==========
constexpr uint8_t  WPN_PISTOL  = 0;
constexpr uint8_t  WPN_GRENADE = 1;
constexpr uint8_t  WPN_SNIPER  = 2;
constexpr uint8_t  WPN_SHOTGUN = 3;
constexpr uint8_t  WPN_COUNT   = 4;

// ========== 手枪 (Pistol) ==========
constexpr uint8_t  WPN_P_DAMAGE     = 20;
constexpr uint8_t  WPN_P_FIRE_RATE  = 3;
constexpr uint8_t  WPN_P_SPLASH     = 1;
constexpr uint8_t  WPN_P_RANGE      = 16;
constexpr uint8_t  WPN_P_RANGE_MAX  = 16;
constexpr uint8_t  WPN_P_SPLASH_MIN = 2;
constexpr uint8_t  WPN_P_SPLASH_MAX = 2;
constexpr uint8_t  WPN_P_DAMAGE_MAX = 20;
constexpr uint8_t  WPN_P_PELLETS    = 1;

// ========== 榴弹 (Grenade) ==========
constexpr uint8_t  WPN_G_DAMAGE     = 2;
constexpr uint8_t  WPN_G_FIRE_RATE  = 6;
constexpr uint8_t  WPN_G_SPLASH     = 0;
constexpr uint8_t  WPN_G_RANGE      = 8;
constexpr uint8_t  WPN_G_RANGE_MAX  = 8;
constexpr uint8_t  WPN_G_SPLASH_MIN = 4;
constexpr uint8_t  WPN_G_SPLASH_MAX = 8;
constexpr uint8_t  WPN_G_DAMAGE_MAX = 2;
constexpr uint8_t  WPN_G_PELLETS    = 0;

// ========== 狙击枪 (Sniper) ==========
constexpr uint8_t  WPN_S_DAMAGE     = 20;
constexpr uint8_t  WPN_S_FIRE_RATE  = 6;
constexpr uint8_t  WPN_S_SPLASH     = 1;
constexpr uint8_t  WPN_S_RANGE      = 12;
constexpr uint8_t  WPN_S_RANGE_MAX  = 32;
constexpr uint8_t  WPN_S_SPLASH_MIN = 1;
constexpr uint8_t  WPN_S_SPLASH_MAX = 2;
constexpr uint8_t  WPN_S_DAMAGE_MAX = 60;
constexpr uint8_t  WPN_S_PELLETS    = 1;

// ========== 霰弹枪 (Shotgun) ==========
constexpr uint8_t  WPN_SH_DAMAGE     = 25;
constexpr uint8_t  WPN_SH_FIRE_RATE  = 6;
constexpr uint8_t  WPN_SH_SPLASH     = 1;
constexpr uint8_t  WPN_SH_RANGE      = 12;
constexpr uint8_t  WPN_SH_RANGE_MAX  = 12;
constexpr uint8_t  WPN_SH_SPLASH_MIN = 1;
constexpr uint8_t  WPN_SH_SPLASH_MAX = 1;
constexpr uint8_t  WPN_SH_DAMAGE_MAX = 25;
constexpr uint8_t  WPN_SH_PELLETS    = 3;

// ========== 技能编号 ==========
constexpr uint8_t  SKILL_SELFDESTRUCT = 0;
constexpr uint8_t  SKILL_DASH         = 1;
constexpr uint8_t  SKILL_SHIELD       = 2;
constexpr uint8_t  SKILL_COUNT        = 3;

// ========== 技能冷却（网络帧数，12fps）==========
constexpr uint8_t  CD_SELFDESTRUCT = 96;
constexpr uint8_t  CD_DASH         = 36;
constexpr uint8_t  CD_SHIELD       = 120;

// ========== 技能数值 ==========
constexpr uint8_t  DASH_GRIDS = 20;  // 冲刺位移格数

// ========== 网络超时 ==========
constexpr uint16_t PACKET_TIMEOUT_MS = 2000; // 2秒无包即断连回主菜单

// ========== 比赛计时 ==========
constexpr uint16_t MATCH_DURATION    = 1080; // 90秒 (90×12网络帧)
constexpr uint8_t  COUNTDOWN_SECS    = 3;
constexpr uint16_t COUNTDOWN_FRAMES  = 36;   // 3秒 (3×12网络帧)

// ========== 蓄力 ==========
constexpr uint8_t  CHARGE_SPEED = 21;  // 每网络帧 +21, 12帧满 (≈1秒)

// ========== 死亡/复活 ==========
constexpr uint8_t  RESPAWN_FRAMES       = 36;  // 3秒复活 (36网络帧)
constexpr uint8_t  INVINCIBILITY_FRAMES = 12;  // 复活后1秒无敌

// ========== 盾 ==========
constexpr int16_t  SHIELD_HP  = 100;
constexpr int16_t  SHIELD_DPS = 24;

// ========== 自爆/榴弹小子弹 ==========
constexpr uint8_t  SELFDESTRUCT_SPLASH = 8;
constexpr uint8_t  SUB_BULLET_SPLASH   = 5;
constexpr int16_t  SUB_DMG_SELFDESTRUCT = 12;
constexpr int16_t  SUB_DMG_GRENADE     = 8;

// ========== 自瞄角度阈值 ==========
constexpr int16_t  AIM_CONE_PISTOL  = 15;
constexpr int16_t  AIM_CONE_SNIPER  = 8;
constexpr int16_t  AIM_CONE_SHOTGUN = 20;

// ========== 双核渲染 ==========
constexpr uint16_t RENDER_STACK_SIZE = 8192;  // Core 0 渲染任务栈

// ========== sin/cos 查表 ==========
constexpr uint16_t TRIG_TABLE_SIZE = 256;
