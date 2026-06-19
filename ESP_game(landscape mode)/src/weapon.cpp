#include "weapon.h"
#include "hardware.h"
#include "map.h"
#include <cstring>

namespace Weapon {

const Def weaponDefs[WPN_COUNT] = {
  // WPN_PISTOL
  { WPN_PISTOL,  false, false,
    WPN_P_DAMAGE, WPN_P_FIRE_RATE, WPN_P_SPLASH,
    WPN_P_RANGE, WPN_P_RANGE_MAX,
    WPN_P_SPLASH_MIN, WPN_P_SPLASH_MAX, WPN_P_DAMAGE_MAX,
    WPN_P_PELLETS, AIM_CONE_PISTOL },
  // WPN_GRENADE
  { WPN_GRENADE, true,  true,
    WPN_G_DAMAGE, WPN_G_FIRE_RATE, WPN_G_SPLASH,
    WPN_G_RANGE, WPN_G_RANGE_MAX,
    WPN_G_SPLASH_MIN, WPN_G_SPLASH_MAX, WPN_G_DAMAGE_MAX,
    WPN_G_PELLETS, 0 },
  // WPN_SNIPER
  { WPN_SNIPER,  true,  false,
    WPN_S_DAMAGE, WPN_S_FIRE_RATE, WPN_S_SPLASH,
    WPN_S_RANGE, WPN_S_RANGE_MAX,
    WPN_S_SPLASH_MIN, WPN_S_SPLASH_MAX, WPN_S_DAMAGE_MAX,
    WPN_S_PELLETS, AIM_CONE_SNIPER },
  // WPN_SHOTGUN
  { WPN_SHOTGUN, false, false,
    WPN_SH_DAMAGE, WPN_SH_FIRE_RATE, WPN_SH_SPLASH,
    WPN_SH_RANGE, WPN_SH_RANGE_MAX,
    WPN_SH_SPLASH_MIN, WPN_SH_SPLASH_MAX, WPN_SH_DAMAGE_MAX,
    WPN_SH_PELLETS, AIM_CONE_SHOTGUN },
};

// 线性插值
static inline uint8_t lerpU8(uint8_t a, uint8_t b, uint8_t t) {
  return a + ((b - a) * t) / 255;
}
static inline int16_t lerpI16(int16_t a, int16_t b, uint8_t t) {
  return a + ((b - a) * (int16_t)t) / 255;
}

void startCharge(State& s) {
  if (s.cooldown > 0) return;
  s.charging = true;
  s.chargeLevel = 0;
}

void updateCharge(State& s) {
  if (!s.charging) return;
  if (s.chargeLevel < 255) {
    int16_t v = (int16_t)s.chargeLevel + CHARGE_SPEED;
    s.chargeLevel = (v > 255) ? 255 : (uint8_t)v;
  }
}

bool calcAimAssist(int16_t shooterX, int16_t shooterY,
                   uint8_t moveAngle, int8_t aimCone,
                   int16_t targetX, int16_t targetY,
                   uint8_t maxGrids,
                   uint8_t& aimOut) {
  if (aimCone <= 0 || targetX < 0) return false;

  // 距离检查：超出射程格数不自瞄
  if (maxGrids > 0) {
    int16_t sgx = Map::pxToGrid(shooterX);
    int16_t sgy = Map::pxToGrid(shooterY);
    int16_t tgx = Map::pxToGrid(targetX);
    int16_t tgy = Map::pxToGrid(targetY);
    int16_t dgx = tgx - sgx;
    int16_t dgy = tgy - sgy;
    if (dgx < 0) dgx = -dgx;
    if (dgy < 0) dgy = -dgy;
    // 切比雪夫距离（菱形范围）或曼哈顿中取较大值
    if (dgx > maxGrids || dgy > maxGrids) return false;
  }

  int16_t dx = targetX - shooterX;
  int16_t dy = targetY - shooterY;
  uint8_t toEnemy = Hardware::vecToAngle(dx, dy);

  // 检查移动方向
  int16_t diff = Hardware::angleDiff(toEnemy, moveAngle);
  if (abs(diff) <= aimCone) {
    aimOut = toEnemy;
    return true;
  }
  // 检查反方向（180°反向 = +128，支持边退后边攻击）
  uint8_t revAngle = (moveAngle + 128) & 0xFF;
  diff = Hardware::angleDiff(toEnemy, revAngle);
  if (abs(diff) <= aimCone) {
    aimOut = toEnemy;
    return true;
  }
  return false;
}

FireResult fire(State& s,
                int16_t shooterX, int16_t shooterY,
                uint8_t moveAngle,
                int16_t targetX, int16_t targetY,
                bool btnHeld, bool btnReleased,
                uint8_t joyMag) {
  FireResult fr;
  memset(&fr, 0, sizeof(fr));
  fr.wpnId = s.wpnId;

  const Def& d = weaponDefs[s.wpnId];

  // 蓄力型武器处理
  if (d.isCharged) {
    if (btnHeld && !s.charging) {
      startCharge(s);
      return fr;
    }
    if (s.charging) {
      updateCharge(s);
      if (btnHeld) return fr; // 继续蓄力
      // 松开 = 发射
    } else {
      return fr;
    }
  } else {
    // 非蓄力：冷却中或未按下则不发射
    if (s.cooldown > 0 || !btnHeld) return fr;
  }

  // === 计算发射方向和自瞄 ===
  uint8_t fireDir = moveAngle;
  int8_t aimCone = d.aimAngle;

  // 转换 aimAngle 从度数到 0-255 制
  // aimAngle 存储的是度数（如15），需要转换
  // 0-255 = 0-360°, so 1° = 256/360 ≈ 0.711
  // 15° → 15*256/360 ≈ 11

  if (d.aimAngle > 0 && targetX >= 0) {
    uint8_t aimLock;
    // aimAngle 是度数（如 15, 8, 20）
    int8_t cone255 = (int8_t)((int16_t)d.aimAngle * 256 / 360);
    if (calcAimAssist(shooterX, shooterY, moveAngle, cone255,
                      targetX, targetY,
                      d.rangeMax, aimLock)) {
      fireDir = aimLock;
      fr.fireDir = fireDir;
    }
  }

  // 始终记录实际发射方向（无论是否自瞄）
  fr.fireDir = fireDir;

  // === 发射 ===
  uint8_t color = CLR_ALLY;
  int16_t rangePx;
  uint8_t splashR;
  int16_t dmg;

  switch (s.wpnId) {
    case WPN_PISTOL: {
      rangePx = (int16_t)d.range * GRID_SIZE;
      splashR = d.splashRadius;
      dmg = d.damage;
      fr.rayResult = Raycast::castParallelRays(
        shooterX, shooterY, fireDir, splashR, rangePx, color);
      break;
    }

    case WPN_GRENADE: {
      // 投掷物：方向=移动方向，距离=摇杆幅度×最大射程
      int16_t maxThrowDist = (int16_t)d.range * GRID_SIZE;
      int16_t throwDist = ((int16_t)maxThrowDist * joyMag) / 255;
      int16_t cosVal = Hardware::sinTable[(fireDir + 64) & 0xFF];
      int16_t sinVal = Hardware::sinTable[fireDir];
      int16_t landX = shooterX + (throwDist * cosVal) / 1000;
      int16_t landY = shooterY + (throwDist * sinVal) / 1000;

      // 边界限制
      if (landX < 0) landX = 0;
      if (landY < 0) landY = 0;
      if (landX >= SCR_W) landX = SCR_W - 1;
      if (landY >= SCR_H) landY = SCR_H - 1;

      // 量化落点以匹配网络传输精度，确保双方爆炸中心一致
      // Y: 网络传输 landY>>1，LSB=2px
      int16_t netLandX = (int16_t)(uint8_t)landX;
      int16_t netLandY = (int16_t)((uint8_t)(landY >> 1) << 1);

      splashR = lerpU8(d.splashMin, d.splashMax, s.chargeLevel);
      if (splashR < 1) splashR = 1;
      dmg = SUB_DMG_GRENADE;
      fr.rayResult = Raycast::castRadialRays(
        netLandX, netLandY, splashR, color, dmg);

      // 发送爆炸物参数：落点 + 溅射半径
      fr.param1 = (uint8_t)netLandX;
      fr.param2 = (uint8_t)(netLandY >> 1);     // LSB=2px
      fr.param3 = splashR;
      break;
    }

    case WPN_SNIPER: {
      rangePx = (int16_t)lerpU8(d.range, d.rangeMax, s.chargeLevel) * GRID_SIZE;
      splashR = lerpU8(d.splashMin, d.splashMax, s.chargeLevel);
      dmg = lerpI16(d.damage, d.damageMax, s.chargeLevel);
      fr.rayResult = Raycast::castParallelRays(
        shooterX, shooterY, fireDir, splashR, rangePx, color);

      // 发送实际射程、溅射半径和伤害（对方无需反推蓄力等级）
      fr.param1 = lerpU8(d.range, d.rangeMax, s.chargeLevel);   // 实际射程（网格）
      fr.param2 = splashR;
      fr.param3 = (uint8_t)dmg;                                   // 实际伤害 (20-60)
      break;
    }

    case WPN_SHOTGUN: {
      // 多个弹丸，散布角 = 自瞄角
      uint8_t pellets = d.pellets;
      int8_t spreadCone = (int8_t)((int16_t)d.aimAngle * 256 / 360);
      splashR = d.splashRadius;
      rangePx = (int16_t)d.range * GRID_SIZE;
      dmg = d.damage;

      for (uint8_t i = 0; i < pellets; i++) {
        // 弹丸均匀分布在 ±spreadCone 范围内
        int16_t offset = spreadCone;
        if (pellets > 1) {
          offset = (int16_t)spreadCone * (2 * (int16_t)i - (pellets - 1)) / (pellets - 1);
        }
        uint8_t pelletAngle = (uint8_t)((int16_t)fireDir + offset) & 0xFF;

        Raycast::Result pelletRes = Raycast::castParallelRays(
          shooterX, shooterY, pelletAngle, splashR, rangePx, color);
        if (pelletRes.hitTarget) {
          fr.rayResult.hitTarget = true;
          fr.rayResult.hitDamage += dmg;
        }
      }
      break;
    }
  }

  // 设置伤害
  if (fr.rayResult.hitTarget && fr.rayResult.hitDamage == 0) {
    fr.rayResult.hitDamage = dmg;
  }

  // 非蓄力武器在这里设置伤害，因为上面 switch 已设置 fr.rayResult
  if (!d.isCharged && s.wpnId != WPN_SHOTGUN) {
    fr.rayResult.hitDamage = dmg;
  }

  // 开火后重置蓄力状态，设置冷却
  s.charging = false;
  s.chargeLevel = 0;
  s.cooldown = d.fireRate;
  fr.fired = true;

  return fr;
}

}  // namespace Weapon
