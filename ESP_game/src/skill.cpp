#include "skill.h"
#include "hardware.h"
#include "map.h"
#include <cstring>

namespace Skill {

SkillResult useSelfDestruct(Entity::Player& p) {
  SkillResult sr;
  memset(&sr, 0, sizeof(sr));
  sr.skillId = SKILL_SELFDESTRUCT;

  if (p.skillCd[SKILL_SELFDESTRUCT] > 0) return sr;

  sr.rayResult = Raycast::castRadialRays(
    p.posX, p.posY,
    SELFDESTRUCT_SPLASH,
    CLR_ALLY,
    SUB_DMG_SELFDESTRUCT
  );

  p.skillCd[SKILL_SELFDESTRUCT] = CD_SELFDESTRUCT;
  sr.used = true;
  return sr;
}

SkillResult useDash(Entity::Player& p) {
  SkillResult sr;
  memset(&sr, 0, sizeof(sr));
  sr.skillId = SKILL_DASH;

  if (p.skillCd[SKILL_DASH] > 0) return sr;
  if (p.stealth) return sr;  // 潜入时不可用

  // 向移动方向位移 20 网格
  constexpr uint8_t dashGrids = DASH_GRIDS;
  int16_t cosVal = Hardware::sinTable[(p.moveAngle + 64) & 0xFF];
  int16_t sinVal = Hardware::sinTable[p.moveAngle];
  int16_t dist = (int16_t)dashGrids * GRID_SIZE;

  int16_t targetX = p.posX + (dist * cosVal) / 1000;
  int16_t targetY = p.posY + (dist * sinVal) / 1000;

  // 边界限制
  int16_t half = PLAYER_SIZE / 2;
  if (targetX - half < 0) targetX = half;
  if (targetX + half >= SCR_W) targetX = SCR_W - 1 - half;
  if (targetY - half < 0) targetY = half;
  if (targetY + half >= SCR_H) targetY = SCR_H - 1 - half;

  // 沿途墙壁检测：沿路径逐步检查
  int16_t steps = dashGrids;
  int16_t stepDx = (targetX - p.posX) / steps;
  int16_t stepDy = (targetY - p.posY) / steps;

  int16_t curX = p.posX;
  int16_t curY = p.posY;
  int16_t lastValidX = curX;
  int16_t lastValidY = curY;

  for (int16_t i = 0; i < steps; i++) {
    curX += stepDx;
    curY += stepDy;
    if (Entity::collidesWithWall(curX, curY)) break;
    lastValidX = curX;
    lastValidY = curY;
  }

  p.posX = lastValidX;
  p.posY = lastValidY;
  p.skillCd[SKILL_DASH] = CD_DASH;
  sr.used = true;
  return sr;
}

SkillResult useShield(Entity::Player& p) {
  SkillResult sr;
  memset(&sr, 0, sizeof(sr));
  sr.skillId = SKILL_SHIELD;

  if (p.skillCd[SKILL_SHIELD] > 0) return sr;
  if (p.stealth) return sr;  // 潜入时不可开盾

  p.shieldHp = SHIELD_HP;
  p.shielded = true;
  p.skillCd[SKILL_SHIELD] = CD_SHIELD;
  sr.used = true;
  return sr;
}

SkillResult tryUseSkill(Entity::Player& p, uint8_t skillId) {
  switch (skillId) {
    case SKILL_SELFDESTRUCT: return useSelfDestruct(p);
    case SKILL_DASH:         return useDash(p);
    case SKILL_SHIELD:       return useShield(p);
    default: {
      SkillResult sr;
      memset(&sr, 0, sizeof(sr));
      return sr;
    }
  }
}

}  // namespace Skill
