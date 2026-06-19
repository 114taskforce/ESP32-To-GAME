#include "entity.h"
#include "hardware.h"
#include "map.h"
#include "network.h"

namespace Entity {

Player local;
Player remote;

void init(bool spawnTop) {
  memset(&local, 0, sizeof(local));
  memset(&remote, 0, sizeof(remote));

  local.maxHp = HP_INITIAL;
  local.hp = HP_INITIAL;
  remote.maxHp = HP_INITIAL;
  remote.hp = HP_INITIAL;
  local.moveSpeed = SPD_BASE;
  local.currentWpn = WPN_PISTOL;
  local.selectedSkill = SKILL_DASH;

  // 出生点：己方在上，对方在下
  if (spawnTop) {
    local.posX = SCR_W / 2;
    local.posY = 40;
    remote.posX = SCR_W / 2;
    remote.posY = SCR_H - 40;
  } else {
    local.posX = SCR_W / 2;
    local.posY = SCR_H - 40;
    remote.posX = SCR_W / 2;
    remote.posY = 40;
  }
  local.spawnX = local.posX;
  local.spawnY = local.posY;
  remote.spawnX = remote.posX;
  remote.spawnY = remote.posY;
  remote.rposX = remote.posX;
  remote.rposY = remote.posY;
}

void resetLocal() {
  local.posX = local.spawnX;
  local.posY = local.spawnY;
  local.hp = HP_INITIAL;
  local.shieldHp = 0;
  local.shielded = false;
  local.stealth = false;
  local.charging = false;
  local.chargeLevel = 0;
  local.wpnCooldown = 5;  // 初始短暂冷却，防止按键滞留触发开火
  local.aimLocked = false;
  local.dead = false;
  local.deathTimer = 0;
  local.invincible = false;
  local.invincTimer = 0;
  for (uint8_t i = 0; i < 3; i++) local.skillCd[i] = 0;
}

void startDeath(Player& p) {
  if (p.dead) return;
  p.dead = true;
  p.deathTimer = RESPAWN_FRAMES;
  p.deathX = p.posX;
  p.deathY = p.posY;
  p.stealth = false;
  p.shielded = false;
  p.charging = false;
  p.chargeLevel = 0;
}

bool updateDeath(Player& p) {
  if (!p.dead) return false;
  if (p.deathTimer > 0) {
    p.deathTimer--;
  }
  if (p.deathTimer == 0) {
    p.posX = p.spawnX;
    p.posY = p.spawnY;
    p.hp = HP_INITIAL;
    p.shieldHp = 0;
    p.shielded = false;
    p.stealth = false;
    p.charging = false;
    p.chargeLevel = 0;
    p.wpnCooldown = 5;  // 重生后短暂冷却，防止按键滞留触发开火
    p.aimLocked = false;
    p.dead = false;
    p.invincible = true;
    p.invincTimer = INVINCIBILITY_FRAMES;
    for (uint8_t i = 0; i < 3; i++) p.skillCd[i] = 0;
    return true;
  }
  return false;
}

// ==== 双缓冲远程状态（网络帧与画面帧错开）====
struct RemoteSnap {
  int16_t posX, posY;
  uint8_t moveAngle;
  int16_t moveSpeed;
};
static RemoteSnap snapA = {0, 0, 0, 0};
static RemoteSnap snapB = {0, 0, 0, 0};
static RemoteSnap* snapPrev = &snapA;  // 前台缓冲（渲染用）
static RemoteSnap* snapCurr = &snapB;  // 后台缓冲（网络写入）
static float snapT = 0.0f;            // 插值进度 0→1
static bool snapInit = false;

void updateRemoteFromNetwork() {
  if (!Network::remoteFrameNew) return;

  Network::FrameData& rf = Network::remoteFrame;

  // 交换双缓冲：后台变前台，前台变后台（准备接收新数据）
  RemoteSnap* tmp = snapPrev;
  snapPrev = snapCurr;
  snapCurr = tmp;

  // 写入新数据到后台缓冲
  snapCurr->posX = rf.posX;
  snapCurr->posY = rf.posY;
  snapCurr->moveAngle = rf.fireDir;
  snapCurr->moveSpeed = rf.moveSpeed;

  if (!snapInit) {
    snapPrev->posX = rf.posX;
    snapPrev->posY = rf.posY;
    snapPrev->moveAngle = rf.fireDir;
    snapPrev->moveSpeed = rf.moveSpeed;
    snapInit = true;
  }

  snapT = 0.0f;  // 重置插值

  // 同步权威状态（游戏逻辑用，非渲染）
  remote.posX = rf.posX;
  remote.posY = rf.posY;
  remote.stealth = rf.stealth;
  remote.moveAngle = rf.fireDir;
  remote.moveSpeed = rf.moveSpeed;
  remote.hp       = rf.hp;
  remote.shieldHp = rf.shieldHp;
  remote.shielded = (rf.shieldHp > 0);
}

// 每渲染帧（24fps）：前台缓冲(prev) → 后台缓冲(curr) 之间插值 + 1帧碰撞预测
void predictRemotePosition() {
  if (remote.dead || !snapInit) return;

  snapT += 0.5f;  // 每网络帧 2 个渲染帧，每次推进 0.5
  if (snapT > 1.0f) snapT = 1.0f;

  // prev → curr 线性插值
  int16_t ix = snapPrev->posX + (int16_t)((snapCurr->posX - snapPrev->posX) * snapT);
  int16_t iy = snapPrev->posY + (int16_t)((snapCurr->posY - snapPrev->posY) * snapT);

  // 预测 1 帧（含碰撞，与远端 updateMovement 同步）
  float angle = snapCurr->moveAngle * 2.0f * PI / 256.0f;
  float dist = (float)snapCurr->moveSpeed / FPS_RENDER;
  int16_t dx = (int16_t)(cosf(angle) * dist);
  int16_t dy = (int16_t)(sinf(angle) * dist);

  int16_t half = PLAYER_SIZE / 2;
  int16_t nx = ix + dx;
  int16_t ny = iy + dy;

  if (nx - half < 0) nx = half;
  if (nx + half >= SCR_W) nx = SCR_W - 1 - half;
  if (ny - half < 0) ny = half;
  if (ny + half >= SCR_H) ny = SCR_H - 1 - half;

  int16_t wx = ix, wy = iy;
  if (!collidesWithWall(nx, wy)) wx = nx;
  if (!collidesWithWall(wx, ny)) wy = ny;

  remote.rposX = wx;
  remote.rposY = wy;
}

void updateSpeed(Player& p) {
  int16_t spd = SPD_BASE;

  if (p.shielded) {
    spd = SPD_SHIELD;
  } else if (p.charging && p.currentWpn == WPN_GRENADE) {
    spd = SPD_CHARGE_B;
  } else if (p.charging && p.currentWpn == WPN_SNIPER) {
    spd = SPD_CHARGE_C;
  } else if (p.stealth) {
    spd = SPD_STEALTH;
  } else {
    // 检查是否在敌方色块上
    uint8_t cellClr = Map::pixelColor(p.posX, p.posY);
    bool onEnemyTile = (&p == &local)
      ? (cellClr == CLR_ENEMY)
      : (cellClr == CLR_ALLY);
    if (onEnemyTile) spd = SPD_ENEMY_TILE;
  }

  p.moveSpeed = spd;
}

bool collidesWithWall(int16_t cx, int16_t cy) {
  int16_t half = PLAYER_SIZE / 2;
  // 检查 4 个角
  if (Map::isPixelWall(cx - half, cy - half)) return true;
  if (Map::isPixelWall(cx + half, cy - half)) return true;
  if (Map::isPixelWall(cx - half, cy + half)) return true;
  if (Map::isPixelWall(cx + half, cy + half)) return true;
  // 检查 4 边中点
  if (Map::isPixelWall(cx, cy - half)) return true;
  if (Map::isPixelWall(cx, cy + half)) return true;
  if (Map::isPixelWall(cx - half, cy)) return true;
  if (Map::isPixelWall(cx + half, cy)) return true;
  return false;
}

void updateMovement(Player& p, uint16_t joyX, uint16_t joyY) {
  updateSpeed(p);

  // 摇杆死区
  int16_t dx = (int16_t)joyX - (JOY_MAX / 2);
  int16_t dy = (int16_t)joyY - (JOY_MAX / 2);
  if (abs(dx) < JOY_DEADZONE && abs(dy) < JOY_DEADZONE) return;

  // 方向角度和移动量
  float angle = atan2f((float)dy, (float)dx);
  p.moveAngle = (uint8_t)(angle * 256.0f / (2.0f * PI)) & 0xFF;

  // 摇杆幅度（0.0 - 1.0）
  float mag = sqrtf((float)(dx * dx + dy * dy)) / (JOY_MAX / 2.0f);
  if (mag > 1.0f) mag = 1.0f;

  // 本帧移动距离（px）
  float moveDist = (float)p.moveSpeed / FPS_RENDER * mag;

  float mx = cosf(angle) * moveDist;
  float my = sinf(angle) * moveDist;

  int16_t newX = p.posX + (int16_t)mx;
  int16_t newY = p.posY + (int16_t)my;

  // 边界限制
  int16_t half = PLAYER_SIZE / 2;
  if (newX - half < 0) newX = half;
  if (newX + half >= SCR_W) newX = SCR_W - 1 - half;
  if (newY - half < 0) newY = half;
  if (newY + half >= SCR_H) newY = SCR_H - 1 - half;

  // 墙壁碰撞（分轴检测，允许滑墙）
  // X 轴
  if (!collidesWithWall(newX, p.posY)) {
    p.posX = newX;
  }
  // Y 轴
  if (!collidesWithWall(p.posX, newY)) {
    p.posY = newY;
  }

  // 潜入状态下检查是否被强制现形：中心像素在敌方色块上
  if (p.stealth) {
    uint8_t cellClr = Map::pixelColor(p.posX, p.posY);
    bool onEnemyTile = (&p == &local)
      ? (cellClr == CLR_ENEMY)
      : (cellClr == CLR_ALLY);
    if (onEnemyTile) {
      p.stealth = false;
    }
  }
}

void toggleStealth(Player& p) {
  if (p.shielded) return;

  if (!p.stealth) {
    uint8_t cellClr = Map::pixelColor(p.posX, p.posY);
    // 允许在己方或中立色块上进入潜入；敌方色块上禁止
    bool onEnemyTile = (&p == &local)
      ? (cellClr == CLR_ENEMY)
      : (cellClr == CLR_ALLY);
    if (!onEnemyTile) {
      p.stealth = true;
    }
  } else {
    p.stealth = false;
  }
}

void updateInvincible(Player& p) {
  if (p.invincible) {
    p.invincTimer--;
    if (p.invincTimer == 0) p.invincible = false;
  }
}

void updateCooldowns(Player& p) {
  if (p.wpnCooldown > 0) p.wpnCooldown--;
  for (uint8_t i = 0; i < 3; i++) {
    if (p.skillCd[i] > 0) p.skillCd[i]--;
  }
  // 无敌倒计时
  if (p.invincible) {
    p.invincTimer--;
    if (p.invincTimer == 0) p.invincible = false;
  }
  // 盾消耗
  if (p.shielded) {
    p.shieldHp--;
    if (p.shieldHp <= 0) {
      p.shieldHp = 0;
      p.shielded = false;
    }
  }
}

int16_t applyDamage(Player& p, int16_t dmg) {
  if (p.invincible) return 0;
  if (p.stealth) {
    p.stealth = false;
    dmg = DMG_STEALTH_HIT;
  }

  if (p.shielded) {
    int16_t shieldAbsorb = (dmg < p.shieldHp) ? dmg : p.shieldHp;
    p.shieldHp -= shieldAbsorb;
    dmg -= shieldAbsorb;
    if (p.shieldHp <= 0) {
      p.shieldHp = 0;
      p.shielded = false;
    }
  }

  p.hp -= dmg;
  if (p.hp < 0) p.hp = 0;
  return dmg;
}

void applyRegen(Player& p) {
  if (!p.stealth) return;
  p.hp += HP_STEALTH_REGEN / FPS_NETWORK;  // 12HP/s ÷ 12fps = 1HP/帧
  if (p.hp > p.maxHp) p.hp = p.maxHp;
}

uint8_t dirToAngle(int16_t dx, int16_t dy) {
  return Hardware::vecToAngle(dx, dy);
}

}  // namespace Entity
