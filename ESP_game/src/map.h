#pragma once
#include "config.h"
#include <cstdint>

namespace Map {

// 地图数据（PSRAM 分配）
extern uint8_t* data;
extern uint8_t* prev;  // 上一帧快照，用于差分渲染

// 初始化（分配内存 + 设置墙壁/初始布局）
void init();

// 格子操作
inline uint8_t getColor(uint8_t col, uint8_t row) {
  uint16_t idx = (uint16_t)row * MAP_COLS + col;
  uint8_t sh = (idx & 3) << 1;
  return (data[idx >> 2] >> sh) & 0x03;
}

inline void setColor(uint8_t col, uint8_t row, uint8_t clr) {
  uint16_t idx = (uint16_t)row * MAP_COLS + col;
  uint8_t sh = (idx & 3) << 1;
  uint8_t& b = data[idx >> 2];
  b = (b & ~(0x03 << sh)) | ((clr & 0x03) << sh);
}

inline bool isWall(uint8_t col, uint8_t row) {
  return getColor(col, row) == CLR_WALL;
}

// 快照（复制到 prev，用于差分渲染）
void snapshot();

// 坐标转换
inline uint8_t pxToGrid(int16_t px) {
  if (px < 0) return 0;
  return (uint8_t)(px / GRID_SIZE);
}

// 检查像素坐标是否在墙内
bool isPixelWall(int16_t px, int16_t py);

// 检查像素坐标所在格子的颜色
inline uint8_t pixelColor(int16_t px, int16_t py) {
  return getColor(pxToGrid(px), pxToGrid(py));
}

}  // namespace Map
