#pragma once
#include "config.h"
#include <cstdint>

namespace Render {

extern uint8_t frameCount;

// 地图
void drawMap(const uint8_t* mapData, const uint8_t* prevMap);

// 指示线（移动方向 + 自瞄锥形）
void drawIndicator(int16_t cx, int16_t cy, uint8_t dirAngle,
                   uint8_t aimAngleDeg, uint16_t rangePx, bool enemyVisible);

// 玩家
void drawPlayer(int16_t px, int16_t py, bool stealth,
                bool aimLocked, bool isEnemy);

// 死亡标记：红色 X
void drawDeathMarker(int16_t px, int16_t py);

// 涂色动画
void drawEffects();

// UI（对战时）
void drawUI(int16_t hp, int16_t maxHp, int16_t shieldHp, uint8_t wpnId, uint8_t skillId,
            uint8_t skillCd, uint8_t skillCdMax,
            uint8_t wpnCd, uint8_t wpnCdMax,
            uint16_t remainSec, bool stealth);

// 赛前选择界面
void drawSelectScreen(uint8_t wpnSel, uint8_t skillSel,
                      bool localReady, bool remoteReady);

// 倒计时
void drawCountdown(uint8_t sec);

// 死亡复活倒计时
void drawDeathTimer(uint8_t remainSec);

// 摇杆校准界面
void drawCalibrate(uint16_t rawX, uint16_t rawY, uint8_t step,
                   bool yFromRawX, bool yInv,
                   bool xFromRawY, bool xInv,
                   bool swapXY, bool invX, bool invY);

// 结算界面
void drawResult(uint16_t allyCells, uint16_t enemyCells);

// 颜色映射
uint16_t gridColor(uint8_t clr);
uint16_t gridColorAnim(uint8_t clr, uint8_t animFrame);

}  // namespace Render
