#pragma once
#include "config.h"
#include "raycast.h"
#include "entity.h"
#include <cstdint>

namespace Skill {

struct SkillResult {
  bool     used;
  Raycast::Result rayResult;
  uint8_t  skillId;
};

// 自爆：脚下360°溅射
SkillResult useSelfDestruct(Entity::Player& p);

// 冲撞：向移动方向位移
SkillResult useDash(Entity::Player& p);

// 盾：临时血量+移速
SkillResult useShield(Entity::Player& p);

// 尝试使用技能（检查冷却和状态）
SkillResult tryUseSkill(Entity::Player& p, uint8_t skillId);

}  // namespace Skill
