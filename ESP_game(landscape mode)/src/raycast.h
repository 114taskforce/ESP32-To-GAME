#pragma once
#include "config.h"
#include <cstdint>

namespace Raycast {

struct Result {
  uint16_t paintedCells[64];  // 涂色格子索引列表 (y*60+x)
  uint8_t  cellCount;
  bool     hitTarget;
  uint8_t  hitDamage;
};

// 动画格子
struct AnimCell {
  uint16_t cellIndex;   // y*60+x
  uint8_t  remainFrames; // 1-4
};

extern AnimCell animCells[128];
extern uint8_t  animCount;

// 每帧涂色缓冲：同一帧内双方涂到同一格子时，起点更近的生效
void beginFramePaints();
void applyFramePaints();

// 延迟涂色：开火时拍摄涂色快照，下一帧回放，确保双方设备在同一帧处理相同数据
void beginCapturePaint();
void endCapturePaint();
void snapshotPaints(uint16_t& count, uint16_t*& cells, uint8_t*& colors, int16_t*& dists);
void restorePaints(uint16_t count);

// 单条射线：从 (sx,sy) 沿角度 angle 前进，逐格涂色（写入缓冲），遇墙停止
// 返回涂色格子列表和命中信息
void castSingleRay(int16_t sx, int16_t sy, uint8_t angle,
                   int16_t rangePx, uint8_t color,
                   Result& result);

// 平行射线组：子弹轨迹矩形涂色
// numRays = 2*splashRadius + 1，每条射线间距 = 1网格
Result castParallelRays(int16_t sx, int16_t sy, uint8_t dirAngle,
                        uint8_t splashRadius, int16_t rangePx,
                        uint8_t color);

// 径向射线组：圆形溅射（投掷物/自爆）
// numRays = 3*splashRadius，每条径向射线发射溅射半径=6的小子弹
Result castRadialRays(int16_t cx, int16_t cy, uint8_t splashRadius,
                      uint8_t color, int16_t subDmg);

// 清空动画（游戏重启时调用）
void resetAnims();

// 添加动画格子
void addAnimCell(uint16_t cellIndex, uint8_t frames);

// 每帧更新动画（减少 remainFrames）
void updateAnims();

// 获取格子是否处于动画中
int8_t getAnimFrame(uint16_t cellIndex);  // 返回 0=无动画, 1-4=动画帧

// 敌方玩家位置（由 game 层设置，用于命中检测）
extern int16_t enemyPlayerX;
extern int16_t enemyPlayerY;

}  // namespace Raycast
