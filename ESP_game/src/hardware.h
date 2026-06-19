#pragma once
#include "config.h"
#include <TFT_eSPI.h>
#include <cstdint>

namespace Hardware {

extern TFT_eSPI tft;

// 双缓冲渲染精灵（PSRAM）
extern TFT_eSprite sprBuf[2];  // 两个全屏缓冲
extern TFT_eSprite* sprDraw;   // 当前绘制目标（指向 sprBuf[drawIdx]）

// 双核同步
extern SemaphoreHandle_t renderStartSem;  // Core 1 → Core 0: 开始渲染
extern SemaphoreHandle_t renderDoneSem;   // Core 0 → Core 1: 渲染完成
extern TaskHandle_t renderTaskHandle;     // Core 0 渲染任务句柄

// 输入状态
struct Input {
  uint16_t joyX;    // 0-4095
  uint16_t joyY;
  bool     btnA;    // 潜入
  bool     btnB;    // 攻击
  bool     btnC;    // 技能
  bool     btnD;    // 切换武器
  bool     btnAPressed;  // 上升沿
  bool     btnBPressed;
  bool     btnCPressed;
  bool     btnDPressed;
};

extern Input input;

// 上一帧按钮状态（用于检测上升沿）
extern uint8_t prevBtns;

// 摇杆校准（防止安装方向反了或XY轴交换）
extern bool calSwapXY;   // X/Y 轴交换
extern bool calInvertX;  // X 轴翻转
extern bool calInvertY;  // Y 轴翻转

void init();
void readInput();
void saveCalibration();   // 保存摇杆校准到 NVS
void loadCalibration();   // 从 NVS 读取摇杆校准

// 双缓冲操作
void beginDraw();          // 设置 sprDraw 指向当前绘制缓冲，清屏
void submitDraw();         // DMA 推送当前绘制缓冲到屏幕，交换缓冲

// 初始化 Core 0 渲染任务
void initRenderTask();

// 正弦查表 (0-255 → sin * 1000, 范围 -1000~1000)
extern int16_t sinTable[TRIG_TABLE_SIZE];

// 获取方向向量 (angle 0-255 => 0-360°, 返回 x,y * 1000)
inline void angleToVec(uint8_t angle, int16_t& dx, int16_t& dy) {
  dx = sinTable[(angle + 64) & 0xFF];  // cos = sin(angle+90°)
  dy = sinTable[angle];
}

// 两点间角度 (返回 0-255)
uint8_t vecToAngle(int16_t dx, int16_t dy);

// 两个角度差 (0-255 制)
int16_t angleDiff(uint8_t a, uint8_t b);

}  // namespace Hardware
