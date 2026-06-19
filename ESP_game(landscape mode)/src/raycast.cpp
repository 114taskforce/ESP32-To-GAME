#include "raycast.h"
#include "hardware.h"
#include "map.h"
#include <cstring>

namespace Raycast {

AnimCell animCells[128];
uint8_t  animCount = 0;

int16_t enemyPlayerX = -100;
int16_t enemyPlayerY = -100;

// ========== 帧内涂色缓冲（带距离，解决双方同格冲突）==========
static constexpr uint16_t MAX_FRAME_PAINTS = 1536;  // 双方各一把榴弹+自爆 ≈ 1136 格
static uint16_t framePaintCells[MAX_FRAME_PAINTS];
static uint8_t  framePaintColors[MAX_FRAME_PAINTS];
static int16_t  framePaintDists[MAX_FRAME_PAINTS];
static uint16_t framePaintCount = 0;

void beginFramePaints() {
  framePaintCount = 0;
}

void applyFramePaints() {
  for (uint16_t i = 0; i < framePaintCount; i++) {
    uint8_t clr = framePaintColors[i];
    if (clr == 0) continue;  // 等距冲突，保留原色
    uint16_t idx = framePaintCells[i];
    Map::setColor(idx % MAP_COLS, idx / MAP_COLS, clr);
  }
  framePaintCount = 0;
}

// ========== 延迟涂色：开火捕获 → 下一帧回放 ==========
// 目的：本地开火涂色延迟1帧，与远端收到的同一帧数据一起处理，
//       确保双方设备在同一网络帧内处理相同开火，避免色块不一致
static uint16_t pendingPaintCells[MAX_FRAME_PAINTS];
static uint8_t  pendingPaintColors[MAX_FRAME_PAINTS];
static int16_t  pendingPaintDists[MAX_FRAME_PAINTS];
static uint16_t pendingPaintCount = 0;

static uint16_t pendingAnimIndices[128];
static uint8_t  pendingAnimFrames[128];
static uint8_t  pendingAnimCount = 0;

static bool captureActive = false;

void beginCapturePaint() {
  captureActive = true;
  pendingPaintCount = 0;
  pendingAnimCount = 0;
}

void endCapturePaint() {
  captureActive = false;
}

void snapshotPaints(uint16_t& count, uint16_t*& cells, uint8_t*& colors, int16_t*& dists) {
  count  = pendingPaintCount;
  cells  = pendingPaintCells;
  colors = pendingPaintColors;
  dists  = pendingPaintDists;
}

void restorePaints(uint16_t count) {
  // 将延迟涂色合并到当前帧缓冲（冲突格仍按距离裁决）
  for (uint16_t i = 0; i < count; i++) {
    uint16_t cellIdx = pendingPaintCells[i];
    uint8_t  color   = pendingPaintColors[i];
    int16_t  dist    = pendingPaintDists[i];
    // 直接写入 framePaint 缓冲，复用冲突裁决逻辑
    bool found = false;
    for (uint16_t j = 0; j < framePaintCount; j++) {
      if (framePaintCells[j] == cellIdx) {
        found = true;
        if (framePaintColors[j] != color) {
          if (dist < framePaintDists[j]) {
            framePaintColors[j] = color;
            framePaintDists[j] = dist;
          } else if (dist == framePaintDists[j]) {
            framePaintColors[j] = 0;
          }
        } else if (dist < framePaintDists[j]) {
          framePaintDists[j] = dist;
        }
        break;
      }
    }
    if (!found && framePaintCount < MAX_FRAME_PAINTS) {
      framePaintCells[framePaintCount] = cellIdx;
      framePaintColors[framePaintCount] = color;
      framePaintDists[framePaintCount] = dist;
      framePaintCount++;
    }
  }
  // 回放动画格子
  for (uint8_t i = 0; i < pendingAnimCount; i++) {
    addAnimCell(pendingAnimIndices[i], pendingAnimFrames[i]);
  }
}

// 记录涂色，同格冲突时距离更近的覆盖，等距则不涂（保留原色）
static void logPaint(uint16_t cellIdx, uint8_t color, int16_t dist) {
  uint16_t* cells = captureActive ? pendingPaintCells : framePaintCells;
  uint8_t*  clrs  = captureActive ? pendingPaintColors : framePaintColors;
  int16_t*  dsts  = captureActive ? pendingPaintDists : framePaintDists;
  uint16_t& count = captureActive ? pendingPaintCount : framePaintCount;

  for (uint16_t i = 0; i < count; i++) {
    if (cells[i] == cellIdx) {
      if (clrs[i] != color) {
        if (dist < dsts[i]) {
          clrs[i] = color;
          dsts[i] = dist;
        } else if (dist == dsts[i]) {
          clrs[i] = 0;
        }
      } else if (dist < dsts[i]) {
        // 同色但更近：保留更近距离，优化后续跨色冲突裁决
        dsts[i] = dist;
      }
      return;
    }
  }
  if (count < MAX_FRAME_PAINTS) {
    cells[count] = cellIdx;
    clrs[count] = color;
    dsts[count] = dist;
    count++;
  }
}

void resetAnims() {
  animCount = 0;
  pendingAnimCount = 0;
}

void addAnimCell(uint16_t cellIndex, uint8_t frames) {
  if (captureActive) {
    if (pendingAnimCount >= 128) return;
    for (uint8_t i = 0; i < pendingAnimCount; i++) {
      if (pendingAnimIndices[i] == cellIndex) {
        pendingAnimFrames[i] = frames;
        return;
      }
    }
    pendingAnimIndices[pendingAnimCount] = cellIndex;
    pendingAnimFrames[pendingAnimCount] = frames;
    pendingAnimCount++;
    return;
  }

  if (animCount >= 128) return;
  for (uint8_t i = 0; i < animCount; i++) {
    if (animCells[i].cellIndex == cellIndex) {
      animCells[i].remainFrames = frames;
      return;
    }
  }
  animCells[animCount].cellIndex = cellIndex;
  animCells[animCount].remainFrames = frames;
  animCount++;
}

void updateAnims() {
  uint8_t w = 0;
  for (uint8_t i = 0; i < animCount; i++) {
    if (animCells[i].remainFrames > 1) {
      animCells[i].remainFrames--;
      if (w != i) animCells[w] = animCells[i];
      w++;
    }
  }
  animCount = w;
}

int8_t getAnimFrame(uint16_t cellIndex) {
  for (uint8_t i = 0; i < animCount; i++) {
    if (animCells[i].cellIndex == cellIndex) return animCells[i].remainFrames;
  }
  return 0;
}

// ---- 单条射线 DDA 遍历 ----
void castSingleRay(int16_t sx, int16_t sy, uint8_t angle,
                   int16_t rangePx, uint8_t color,
                   Result& result) {
  // 方向向量（sinTable: ±1000）
  int16_t cosVal = Hardware::sinTable[(angle + 64) & 0xFF]; // cos = sin(angle+90°)
  int16_t sinVal = Hardware::sinTable[angle];

  // 当前网格坐标
  int16_t gx = Map::pxToGrid(sx);
  int16_t gy = Map::pxToGrid(sy);

  if (gx < 0 || gx >= MAP_COLS || gy < 0 || gy >= MAP_ROWS) return;

  // 步进方向
  int8_t stepX = (cosVal > 0) ? 1 : -1;
  int8_t stepY = (sinVal > 0) ? 1 : -1;

  // 到下一个网格边界的距离（像素 * 1000）
  int32_t tDeltaX = (cosVal != 0) ? abs((int32_t)GRID_SIZE * 1000 / cosVal) : 0x7FFFFFFF;
  int32_t tDeltaY = (sinVal != 0) ? abs((int32_t)GRID_SIZE * 1000 / sinVal) : 0x7FFFFFFF;

  // 初始 tMax
  int32_t tMaxX, tMaxY;
  if (cosVal > 0) {
    tMaxX = ((gx + 1) * GRID_SIZE - sx) * 1000 / cosVal;
  } else if (cosVal < 0) {
    tMaxX = (sx - gx * GRID_SIZE) * 1000 / (-cosVal);
  } else {
    tMaxX = 0x7FFFFFFF;
  }
  if (sinVal > 0) {
    tMaxY = ((gy + 1) * GRID_SIZE - sy) * 1000 / sinVal;
  } else if (sinVal < 0) {
    tMaxY = (sy - gy * GRID_SIZE) * 1000 / (-sinVal);
  } else {
    tMaxY = 0x7FFFFFFF;
  }

  // 格数按角度补偿：斜向需要更多格达到相同欧氏距离
  int16_t aCos = (cosVal > 0) ? cosVal : -cosVal;
  int16_t aSin = (sinVal > 0) ? sinVal : -sinVal;
  int16_t maxCells = (int16_t)(((int32_t)rangePx / GRID_SIZE) * (aCos + aSin) / 1000);
  if (maxCells < rangePx / GRID_SIZE) maxCells = rangePx / GRID_SIZE;  // 水平竖直时不减少
  int16_t cellN = 0;

  while (cellN < maxCells) {
    // 步进到下一个格子
    if (tMaxX < tMaxY) {
      tMaxX += tDeltaX;
      gx += stepX;
    } else {
      tMaxY += tDeltaY;
      gy += stepY;
    }
    cellN++;

    // 边界检查
    if (gx < 0 || gx >= MAP_COLS || gy < 0 || gy >= MAP_ROWS) break;

    // 遇墙停止
    if (Map::isWall((uint8_t)gx, (uint8_t)gy)) break;

    // 涂色（写入缓冲，同帧内双方冲突时起点更近者胜）
    uint16_t cellIdx = (uint16_t)gy * MAP_COLS + (uint16_t)gx;
    int16_t distPx = cellN * GRID_SIZE;
    logPaint(cellIdx, color, distPx);

    // 记录涂色格子
    if (result.cellCount < 64) {
      result.paintedCells[result.cellCount++] = cellIdx;
    }

    // 命中检测：检查该格子是否覆盖敌方玩家中心像素
    if (!result.hitTarget && enemyPlayerX >= 0 && enemyPlayerY >= 0) {
      uint8_t egx = Map::pxToGrid(enemyPlayerX);
      uint8_t egy = Map::pxToGrid(enemyPlayerY);
      if ((uint8_t)gx == egx && (uint8_t)gy == egy) {
        result.hitTarget = true;
      }
    }
  }
}

// ---- 平行射线组 ----
Result castParallelRays(int16_t sx, int16_t sy, uint8_t dirAngle,
                        uint8_t splashRadius, int16_t rangePx,
                        uint8_t color) {
  Result result;
  memset(&result, 0, sizeof(Result));

  uint8_t numRays = 2 * splashRadius + 1;
  uint8_t perpAngle = (dirAngle + 64) & 0xFF; // +90°

  int16_t perpCos = Hardware::sinTable[(perpAngle + 64) & 0xFF];
  int16_t perpSin = Hardware::sinTable[perpAngle];

  for (uint8_t i = 0; i < numRays; i++) {
    int16_t offset = ((int16_t)i - splashRadius) * GRID_SIZE;
    int16_t rx = sx + (offset * perpCos) / 1000;
    int16_t ry = sy + (offset * perpSin) / 1000;

    // 每条射线独立追踪
    Result rayRes;
    memset(&rayRes, 0, sizeof(rayRes));
    castSingleRay(rx, ry, dirAngle, rangePx, color, rayRes);

    // 合并结果
    if (rayRes.hitTarget) {
      result.hitTarget = true;
      result.hitDamage += rayRes.hitDamage;
    }
    // paintedCells 由 castSingleRay 已写入 Map，我们只标记动画
    for (uint8_t j = 0; j < rayRes.cellCount; j++) {
      addAnimCell(rayRes.paintedCells[j], 4);
    }
  }

  return result;
}

// ---- 径向射线组 ----
Result castRadialRays(int16_t cx, int16_t cy, uint8_t splashRadius,
                      uint8_t color, int16_t subDmg) {
  Result result;
  memset(&result, 0, sizeof(Result));

  uint8_t numRays = 3 * splashRadius;
  if (numRays < 8) numRays = 8;
  if (numRays > 64) numRays = 64;

  int16_t rangePx = (int16_t)splashRadius * GRID_SIZE;

  for (uint8_t i = 0; i < numRays; i++) {
    uint8_t angle = (uint8_t)((uint16_t)i * 256 / numRays);

    // 每条径向射线是一个方向上的小子弹（溅射半径=6的平行射线组）
    Result rayRes = castParallelRays(cx, cy, angle,
                                     SUB_BULLET_SPLASH, rangePx, color);
    if (rayRes.hitTarget) {
      result.hitTarget = true;
      result.hitDamage += subDmg;
    }
  }

  return result;
}

}  // namespace Raycast
