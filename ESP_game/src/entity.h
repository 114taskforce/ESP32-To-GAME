#pragma once
#include "config.h"
#include <cstdint>

namespace Entity {

struct Player {
  int16_t posX, posY;       // 中心像素坐标（本地=实际，远程=网络权威值）
  int16_t rposX, rposY;     // 远程渲染预测位置（24fps 平滑）— 仅 remote 使用
  int16_t hp;
  int16_t maxHp;
  int16_t shieldHp;         // 盾剩余血量（0=无盾）
  int16_t spawnX, spawnY;   // 出生点（复活用）
  uint8_t moveAngle;        // 移动方向 0-255
  uint8_t aimAngle;         // 自瞄锁定角度 0-255
  bool    stealth;          // 潜入状态
  bool    aimLocked;        // 自瞄是否锁定目标
  uint8_t currentWpn;       // 0-3
  uint8_t selectedSkill;    // 0-2 赛前选择
  uint8_t wpnCooldown;      // 武器冷却剩余（网络帧）
  uint8_t chargeLevel;      // 蓄力等级 0-255
  bool    charging;         // 是否正在蓄力
  uint8_t skillCd[3];       // 技能冷却剩余（网络帧）
  int16_t moveSpeed;        // 当前有效速度 px/s
  bool    shielded;         // 盾是否激活
  bool    dead;             // 死亡状态
  uint8_t deathTimer;       // 复活倒计时（网络帧）
  int16_t deathX, deathY;   // 死亡位置（红色 X 标记）
  bool    invincible;       // 无敌状态（复活后短暂无敌）
  uint8_t invincTimer;      // 无敌剩余（网络帧）
};

// 本地玩家
extern Player local;
// 对方玩家（从网络帧更新）
extern Player remote;

void init(bool spawnTop);
void resetLocal();
void updateRemoteFromNetwork();

// 远程玩家渲染位置预测（每渲染帧调用，24fps）
// 基于上次网络帧的位置+速度+方向做航位推算，实现平滑移动
void predictRemotePosition();

// 移动更新（每渲染帧调用）
// 根据摇杆输入更新位置，处理墙壁碰撞和速度计算
void updateMovement(Player& p, uint16_t joyX, uint16_t joyY);

// 更新速度（根据当前状态：潜入/敌方色块/蓄力/盾）
void updateSpeed(Player& p);

// 潜入切换
void toggleStealth(Player& p);

// 更新无敌倒计时（本地和远程每网络帧都需调用）
void updateInvincible(Player& p);

// 更新冷却（每网络帧调用，仅本地玩家）
void updateCooldowns(Player& p);

// 开始死亡（设置死亡状态和复活倒计时）
void startDeath(Player& p);

// 更新死亡倒计时（每网络帧），返回 true 表示刚复活
bool updateDeath(Player& p);

// 受到伤害（返回实际伤害值，处理盾吸收）
int16_t applyDamage(Player& p, int16_t dmg);

// 每秒回复（潜入时）
void applyRegen(Player& p);

// 碰撞检测：检查 13x13 碰撞盒是否与墙重叠
bool collidesWithWall(int16_t cx, int16_t cy);

// 角度计算（使用硬件 trig 表）
uint8_t dirToAngle(int16_t dx, int16_t dy);

}  // namespace Entity
