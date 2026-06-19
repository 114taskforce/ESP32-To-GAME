#include "map.h"
#include <cstdlib>
#include <cstring>

namespace Map {

uint8_t* data = nullptr;
uint8_t* prev = nullptr;

void init() {
  // PSRAM 分配
  data = (uint8_t*)malloc(MAP_BYTES);
  prev = (uint8_t*)malloc(MAP_BYTES);
  memset(data, 0, MAP_BYTES);
  memset(prev, 0xAA, MAP_BYTES);  // 0b10=CLR_ALLY, 不与中立/敌/墙匹配，强制首帧全量绘制

  // 绘制边界墙
  for (uint8_t c = 0; c < MAP_COLS; c++) {
    setColor(c, 0, CLR_WALL);
    setColor(c, 1, CLR_WALL);
    setColor(c, MAP_ROWS - 2, CLR_WALL);
    setColor(c, MAP_ROWS - 1, CLR_WALL);
  }
  for (uint8_t r = 0; r < MAP_ROWS; r++) {
    setColor(0, r, CLR_WALL);
    setColor(1, r, CLR_WALL);
    setColor(MAP_COLS - 2, r, CLR_WALL);
    setColor(MAP_COLS - 1, r, CLR_WALL);
  }

  // 中心障碍物（对称布局，适配 80×60 横屏网格）
  auto fillRectMap = [](uint8_t cx, uint8_t cy, uint8_t w, uint8_t h, uint8_t clr) {
    for (uint8_t r = cy; r < cy + h && r < MAP_ROWS; r++)
      for (uint8_t c = cx; c < cx + w && c < MAP_COLS; c++)
        setColor(c, r, clr);
  };

  // 上方平台
  fillRectMap(20, 16, 12, 3, CLR_WALL);
  fillRectMap(52, 16, 12, 3, CLR_WALL);

  // 中央掩体
  fillRectMap(17, 27, 5, 9, CLR_WALL);
  fillRectMap(58, 27, 5, 9, CLR_WALL);
  fillRectMap(37, 29, 7, 5, CLR_WALL);

  // 下方对称
  fillRectMap(20, 42, 12, 3, CLR_WALL);
  fillRectMap(52, 42, 12, 3, CLR_WALL);

}

void snapshot() {
  memcpy(prev, data, MAP_BYTES);
}

bool isPixelWall(int16_t px, int16_t py) {
  if (px < 0 || py < 0 || px >= SCR_W || py >= SCR_H) return true;
  return isWall(pxToGrid(px), pxToGrid(py));
}

}  // namespace Map
