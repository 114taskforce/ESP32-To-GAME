#include "render.h"
#include "hardware.h"
#include "map.h"
#include "raycast.h"

namespace Render {

uint8_t frameCount = 0;

// ==================== 颜色 ====================
uint16_t gridColor(uint8_t clr) {
  switch (clr) {
    case CLR_ALLY:  return 0x07E0;   // 绿色
    case CLR_ENEMY: return 0xF800;   // 红色
    case CLR_WALL:  return TFT_WHITE; // 白色墙壁
    default:        return TFT_BLACK; // 黑色中立
  }
}

uint16_t gridColorAnim(uint8_t clr, uint8_t animFrame) {
  if (animFrame & 1) return TFT_WHITE;
  return gridColor(clr);
}

// ==================== 地图（全量重绘）====================
void drawMap(const uint8_t* mapData, const uint8_t* prevMap) {
  (void)prevMap;  // 双缓冲下快照对比无效，全量重绘
  for (uint16_t r = 0; r < MAP_ROWS; r++) {
    for (uint16_t c = 0; c < MAP_COLS; c++) {
      uint16_t idx = r * MAP_COLS + c;
      uint8_t sh = (idx & 3) << 1;
      uint8_t clr = (mapData[idx >> 2] >> sh) & 0x03;

      int8_t animF = Raycast::getAnimFrame(idx);
      uint16_t color;
      if (animF > 0) {
        color = gridColorAnim(clr, (uint8_t)animF);
      } else {
        color = gridColor(clr);
      }

      int16_t px = (int16_t)c * GRID_SIZE;
      int16_t py = (int16_t)r * GRID_SIZE;
      Hardware::sprDraw->fillRect(px, py, GRID_SIZE, GRID_SIZE, color);
    }
  }
}

// ==================== 指示线 ====================
void drawIndicator(int16_t cx, int16_t cy, uint8_t dirAngle,
                   uint8_t aimAngleDeg, uint16_t rangePx, bool enemyVisible) {
  if (!enemyVisible) return;

  int16_t cosMain = Hardware::sinTable[(dirAngle + 64) & 0xFF];
  int16_t sinMain = Hardware::sinTable[dirAngle];
  int16_t ex = cx + (rangePx * cosMain) / 1000;
  int16_t ey = cy + (rangePx * sinMain) / 1000;
  Hardware::sprDraw->drawLine(cx, cy, ex, ey, 0x8410);   // 灰色指示线

  if (aimAngleDeg > 0) {
    int16_t cone255 = (int16_t)aimAngleDeg * 256 / 360;
    uint8_t aimL = (uint8_t)((int16_t)dirAngle - cone255) & 0xFF;
    uint8_t aimR = (uint8_t)((int16_t)dirAngle + cone255) & 0xFF;
    int16_t cosL = Hardware::sinTable[(aimL + 64) & 0xFF];
    int16_t sinL = Hardware::sinTable[aimL];
    int16_t cosR = Hardware::sinTable[(aimR + 64) & 0xFF];
    int16_t sinR = Hardware::sinTable[aimR];
    int16_t coneLen = rangePx * 3 / 4;
    Hardware::sprDraw->drawLine(cx, cy,
      cx + (coneLen * cosL) / 1000, cy + (coneLen * sinL) / 1000, 0x8410);  // 灰色自瞄角度线
    Hardware::sprDraw->drawLine(cx, cy,
      cx + (coneLen * cosR) / 1000, cy + (coneLen * sinR) / 1000, 0x8410);
  }
}

// ==================== 玩家 ====================
void drawPlayer(int16_t px, int16_t py, bool stealth,
                bool aimLocked, bool isEnemy) {
  // 潜入的敌方：显示微弱位置提示（脉冲圆点 + 周围色块微亮）
  // 对手可以看到潜入者所在的己方色块轻微高亮，便于追踪
  if (stealth && isEnemy) {
    uint8_t pulse = (frameCount & 0x08) ? 2 : 1;
    uint16_t hintColor = (frameCount & 0x04) ? 0x4208 : 0x2104;
    Hardware::sprDraw->fillCircle(px, py, PLAYER_SIZE / 2 + pulse, hintColor);
    Hardware::sprDraw->drawCircle(px, py, PLAYER_SIZE / 2 + 3, 0x18C3);

    // 潜入者脚下的色块微高亮（3x3 网格范围），形成追踪轨迹
    int16_t gx = px / GRID_SIZE;
    int16_t gy = py / GRID_SIZE;
    for (int16_t dr = -1; dr <= 1; dr++) {
      for (int16_t dc = -1; dc <= 1; dc++) {
        int16_t cx = gx + dc;
        int16_t cy = gy + dr;
        if (cx >= 0 && cx < MAP_COLS && cy >= 0 && cy < MAP_ROWS) {
          Hardware::sprDraw->drawRect(cx * GRID_SIZE, cy * GRID_SIZE,
                                      GRID_SIZE, GRID_SIZE, hintColor);
        }
      }
    }
    return;
  }

  uint16_t color = isEnemy ? TFT_RED : TFT_GREEN;
  int16_t r = PLAYER_SIZE / 2 + 3;

  // 潜入时敌方看不到光环，自己始终可见（且潜入时自身显示方框）
  if (!stealth || !isEnemy) {
    if (aimLocked || stealth) {
      Hardware::sprDraw->drawRect(px - r, py - r, r * 2, r * 2, 0x8410);  // 灰色/潜入方框
    } else {
      Hardware::sprDraw->drawCircle(px, py, r, TFT_BLUE);
    }
  }

  int16_t half = PLAYER_SIZE / 2;
  Hardware::sprDraw->fillRect(px - half, py - half, PLAYER_SIZE, PLAYER_SIZE, color);
}

// ==================== 死亡标记 ====================
void drawDeathMarker(int16_t px, int16_t py) {
  int16_t s = PLAYER_SIZE / 2 + 1;  // 半对角线
  // 红色 X：两条对角线 + 边框
  Hardware::sprDraw->drawLine(px - s, py - s, px + s, py + s, TFT_RED);
  Hardware::sprDraw->drawLine(px - s, py + s, px + s, py - s, TFT_RED);
  // 加粗（偏移 1px）
  Hardware::sprDraw->drawLine(px - s + 1, py - s, px + s + 1, py + s, TFT_RED);
  Hardware::sprDraw->drawLine(px - s + 1, py + s, px + s + 1, py - s, TFT_RED);
}

// ==================== 特效 ====================
void drawEffects() {
  for (uint8_t i = 0; i < Raycast::animCount; i++) {
    const Raycast::AnimCell& ac = Raycast::animCells[i];
    uint8_t row = ac.cellIndex / MAP_COLS;
    uint8_t col = ac.cellIndex % MAP_COLS;
    uint8_t clr = Map::getColor(col, row);
    uint16_t color = gridColorAnim(clr, ac.remainFrames);
    int16_t px = (int16_t)col * GRID_SIZE;
    int16_t py = (int16_t)row * GRID_SIZE;
    Hardware::sprDraw->fillRect(px, py, GRID_SIZE, GRID_SIZE, color);
  }
}

// ==================== UI ====================
void drawUI(int16_t hp, int16_t maxHp, int16_t shieldHp, uint8_t wpnId, uint8_t skillId,
            uint8_t skillCd, uint8_t skillCdMax,
            uint8_t wpnCd, uint8_t wpnCdMax,
            uint16_t remainSec, bool stealth) {
  // 血条（右上）
  int16_t barW = 100, barH = 8;
  int16_t barX = SCR_W - barW - 4, barY = 18;

  // 盾条（血条下方，仅盾 > 0 时显示）
  if (shieldHp > 0) {
    int16_t shieldBarY = barY + barH + 2;
    Hardware::sprDraw->drawRect(barX, shieldBarY, barW, barH, TFT_WHITE);
    int16_t sFill = (int32_t)shieldHp * barW / SHIELD_HP;
    if (sFill > 0) {
      Hardware::sprDraw->fillRect(barX + 1, shieldBarY + 1, sFill - 1, barH - 2, 0x7E0C);
    }
    barY = shieldBarY + barH + 1;  // 盾条占位
  }

  Hardware::sprDraw->drawRect(barX, barY, barW, barH, TFT_WHITE);
  int16_t fill = (int32_t)hp * barW / maxHp;
  if (fill > 0) {
    uint16_t hpColor = TFT_BLUE;
    Hardware::sprDraw->fillRect(barX + 1, barY + 1, fill - 1, barH - 2, hpColor);
  }

  // 比赛倒计时（血条左侧，小字体同高度）
  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  Hardware::sprDraw->setTextDatum(TR_DATUM);
  char buf[8];
  snprintf(buf, 8, "%u", remainSec);
  Hardware::sprDraw->drawString(buf, barX - 4, barY, 1);
  Hardware::sprDraw->setTextDatum(TL_DATUM);

  // 武器 & 技能名
  const char* wpnNames[] = {"PST", "GRN", "SNP", "SHT"};
  const char* sklNames[] = {"BOMB", "DASH", "SHLD"};
  Hardware::sprDraw->drawString(wpnNames[wpnId], 4, 4, 1);
  Hardware::sprDraw->drawString(sklNames[skillId], 4, 18, 1);

  // 潜入状态指示
  if (stealth) {
    Hardware::sprDraw->setTextColor(0x0410, TFT_BLACK);  // 暗绿色 STL
    Hardware::sprDraw->drawString("STL", 4, 32, 1);
  }

  // 冷却指示器
  auto drawCD = [](int16_t x, int16_t y, uint8_t cd, uint8_t cdMax, uint16_t clr) {
    int16_t sz = 12;
    Hardware::sprDraw->drawRect(x, y, sz, sz, clr);
    if (cd > 0) {
      int16_t fillH = (int16_t)cd * sz / cdMax;
      Hardware::sprDraw->fillRect(x + 1, y + sz - fillH - 1, sz - 2, fillH, 0x4208);
    }
  };

  uint16_t skillClrs[] = {TFT_YELLOW, TFT_CYAN, TFT_MAGENTA};
  uint16_t wpnBoxClrs[] = {TFT_GREEN, TFT_CYAN, TFT_RED, TFT_YELLOW};
  drawCD(4, 34, skillCd, skillCdMax, skillClrs[skillId]);   // 技能冷却
  drawCD(18, 34, wpnCd, wpnCdMax, wpnBoxClrs[wpnId]);       // 武器冷却
}

// ==================== 选择界面 ====================
void drawSelectScreen(uint8_t wpnSel, uint8_t skillSel,
                      bool localReady, bool remoteReady) {
  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  Hardware::sprDraw->setTextDatum(MC_DATUM);

  // 标题
  Hardware::sprDraw->drawString("SELECT WEAPON & SKILL", SCR_W / 2, 30, 2);

  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  Hardware::sprDraw->drawString("WEAPON (LEFT/RIGHT)", SCR_W / 2, 48, 1);

  // 武器选项
  const char* wpnNames[] = {"PISTOL", "GRENADE", "SNIPER", "SHOTGUN"};
  const char* wpnDesc[] = {
    "DMG:20 SPD:fast RNG:40",
    "DMG:2spl CHG:splash",
    "DMG:20-60 CHG:range",
    "DMG:25x3 SPD:slow"
  };

  for (uint8_t i = 0; i < WPN_COUNT; i++) {
    uint16_t clr = (i == wpnSel) ? TFT_CYAN : TFT_DARKGREY;
    int16_t y = 60 + i * 25;
    Hardware::sprDraw->setTextColor(clr, TFT_BLACK);
    Hardware::sprDraw->drawString(wpnNames[i], SCR_W / 2, y, 1);
    Hardware::sprDraw->setTextColor(TFT_DARKGREY, TFT_BLACK);
    Hardware::sprDraw->drawString(wpnDesc[i], SCR_W / 2, y + 10, 1);
  }

  // 技能选项
  const char* sklNames[] = {"SELF-DESTRUCT", "DASH", "SHIELD"};
  const char* sklDesc[] = {
    "CD:8s 360 splash R=8",
    "CD:3s dash 20 tiles",
    "CD:10s +100HP +60%SPD"
  };

  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  Hardware::sprDraw->drawString("SKILL (UP/DOWN)", SCR_W / 2, 195, 1);

  for (uint8_t i = 0; i < SKILL_COUNT; i++) {
    uint16_t clr = (i == skillSel) ? TFT_CYAN : TFT_DARKGREY;
    int16_t y = 210 + i * 25;
    Hardware::sprDraw->setTextColor(clr, TFT_BLACK);
    Hardware::sprDraw->drawString(sklNames[i], SCR_W / 2, y, 1);
    Hardware::sprDraw->setTextColor(TFT_DARKGREY, TFT_BLACK);
    Hardware::sprDraw->drawString(sklDesc[i], SCR_W / 2, y + 10, 1);
  }

  // 确认状态
  Hardware::sprDraw->setTextColor(TFT_YELLOW, TFT_BLACK);
  Hardware::sprDraw->drawString(
    localReady ? "YOU: READY - Waiting..." : "YOU: Press B to confirm",
    SCR_W / 2, 290, 1);

  Hardware::sprDraw->setTextColor(remoteReady ? TFT_GREEN : TFT_RED, TFT_BLACK);
  Hardware::sprDraw->drawString(
    remoteReady ? "OPPONENT: READY" : "OPPONENT: selecting...",
    SCR_W / 2, 304, 1);

  Hardware::sprDraw->setTextDatum(TL_DATUM);
}

// ==================== 倒计时 ====================
void drawCountdown(uint8_t sec) {
  Hardware::sprDraw->setTextColor(TFT_RED, TFT_BLACK);
  Hardware::sprDraw->setTextDatum(MC_DATUM);
  Hardware::sprDraw->setTextSize(3);  // font 1 ×3 倍放大

  if (sec > 0) {
    char buf[2];
    snprintf(buf, 2, "%u", sec);
    Hardware::sprDraw->drawString(buf, SCR_W / 2, SCR_H / 2);
  } else {
    Hardware::sprDraw->drawString("GO!", SCR_W / 2, SCR_H / 2);
  }

  Hardware::sprDraw->setTextSize(1);
  Hardware::sprDraw->setTextDatum(TL_DATUM);
}

// ==================== 死亡倒计时 ====================
void drawDeathTimer(uint8_t remainSec) {
  Hardware::sprDraw->setTextColor(TFT_RED, TFT_BLACK);
  Hardware::sprDraw->setTextDatum(MC_DATUM);
  Hardware::sprDraw->setTextSize(2);
  char buf[16];
  snprintf(buf, 16, "RESPAWN %u", remainSec);
  Hardware::sprDraw->drawString(buf, SCR_W / 2, SCR_H / 2);
  Hardware::sprDraw->setTextSize(1);
  Hardware::sprDraw->setTextDatum(TL_DATUM);
}

// ==================== 摇杆校准 ====================
void drawCalibrate(uint16_t rawX, uint16_t rawY, uint8_t step,
                   bool yFromRawX, bool yInv,
                   bool xFromRawY, bool xInv,
                   bool swapXY, bool invX, bool invY) {
  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  Hardware::sprDraw->setTextDatum(MC_DATUM);

  Hardware::sprDraw->drawString("JOYSTICK CAL", SCR_W / 2, 12, 2);

  char buf[32];

  if (step < 2) {
    // 步骤提示
    Hardware::sprDraw->setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, 32, "Step %u/2", step + 1);
    Hardware::sprDraw->drawString(buf, SCR_W / 2, 30, 1);

    Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
    if (step == 0) {
      Hardware::sprDraw->drawString("Push joystick UP ^", SCR_W / 2, 44, 1);
      Hardware::sprDraw->drawString("then press B", SCR_W / 2, 58, 1);
    } else {
      Hardware::sprDraw->drawString("Push joystick RIGHT >", SCR_W / 2, 44, 1);
      Hardware::sprDraw->drawString("then press B", SCR_W / 2, 58, 1);
    }
  } else {
    Hardware::sprDraw->setTextColor(TFT_GREEN, TFT_BLACK);
    Hardware::sprDraw->drawString("CALIBRATION DONE", SCR_W / 2, 30, 1);

    Hardware::sprDraw->setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(buf, 32, "SwapXY: %s  Xinv: %s  Yinv: %s",
             swapXY ? "YES" : "NO", invX ? "YES" : "NO", invY ? "YES" : "NO");
    Hardware::sprDraw->drawString(buf, SCR_W / 2, 48, 1);
  }

  // 原始值
  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, 32, "Raw X: %u  Y: %u", rawX, rawY);
  Hardware::sprDraw->drawString(buf, SCR_W / 2, 70, 1);

  // 可视化：摇杆位置圆点
  int16_t dotX = (int16_t)rawX * 200 / JOY_MAX + 20;
  int16_t dotY = (int16_t)rawY * 200 / JOY_MAX + 140;
  Hardware::sprDraw->drawRect(20, 140, 200, 200, TFT_WHITE);
  Hardware::sprDraw->fillCircle(dotX, dotY, 4, TFT_RED);

  // 标记四个方向
  Hardware::sprDraw->drawRect(20, 140, 10, 10, TFT_CYAN);            // 左上 (0,0)
  Hardware::sprDraw->drawRect(210, 140, 10, 10, TFT_CYAN);           // 右上 (max,0)
  Hardware::sprDraw->drawRect(20, 330, 10, 10, TFT_CYAN);            // 左下 (0,max)
  Hardware::sprDraw->drawRect(210, 330, 10, 10, TFT_CYAN);           // 右下 (max,max)

  // 上 (Y=0) 和 右 (X=max) 的指示
  if (step == 0) {
    Hardware::sprDraw->drawRect(110, 130, 20, 20, TFT_YELLOW);       // 上方高亮
    Hardware::sprDraw->setTextColor(TFT_YELLOW, TFT_BLACK);
    Hardware::sprDraw->drawString("UP", SCR_W / 2, 120, 1);
  } else if (step == 1) {
    Hardware::sprDraw->drawRect(210, 230, 20, 20, TFT_YELLOW);       // 右方高亮
    Hardware::sprDraw->setTextColor(TFT_YELLOW, TFT_BLACK);
    Hardware::sprDraw->drawString("RIGHT", 225, 230, 1);
  }

  // 操作提示
  Hardware::sprDraw->setTextColor(TFT_WHITE, TFT_BLACK);
  Hardware::sprDraw->drawString("Press A to return", SCR_W / 2, 305, 1);

  Hardware::sprDraw->setTextDatum(TL_DATUM);
}

// ==================== 结算 ====================
void drawResult(uint16_t allyCells, uint16_t enemyCells) {
  Hardware::sprDraw->setTextDatum(MC_DATUM);

  bool won = allyCells > enemyCells;

  Hardware::sprDraw->setTextColor(won ? TFT_GREEN : TFT_RED, TFT_BLACK);
  Hardware::sprDraw->drawString(won ? "YOU WIN!" : "YOU LOSE!",
                                SCR_W / 2, 50, 1);

  char buf[32];
  Hardware::sprDraw->setTextColor(TFT_GREEN, TFT_BLACK);
  snprintf(buf, 32, "YOU: %u/%u", allyCells, (uint16_t)MAP_CELLS);
  Hardware::sprDraw->drawString(buf, SCR_W / 2, 80, 1);

  Hardware::sprDraw->setTextColor(TFT_RED, TFT_BLACK);
  snprintf(buf, 32, "ENEMY: %u/%u", enemyCells, (uint16_t)MAP_CELLS);
  Hardware::sprDraw->drawString(buf, SCR_W / 2, 100, 1);

  Hardware::sprDraw->setTextColor(TFT_YELLOW, TFT_BLACK);
  Hardware::sprDraw->drawString("Press D to restart", SCR_W / 2, 130, 1);

  Hardware::sprDraw->setTextDatum(TL_DATUM);
}

}  // namespace Render
