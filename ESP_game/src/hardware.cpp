#include "hardware.h"
#include "game.h"
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace Hardware {

TFT_eSPI tft;

// 双缓冲
TFT_eSprite sprBuf[2] = { TFT_eSprite(&tft), TFT_eSprite(&tft) };
TFT_eSprite* sprDraw = &sprBuf[0];
static uint8_t drawIdx = 0;  // 当前绘制目标索引

// 双核同步
SemaphoreHandle_t renderStartSem = nullptr;
SemaphoreHandle_t renderDoneSem = nullptr;
TaskHandle_t renderTaskHandle = nullptr;

Input input;
uint8_t prevBtns = 0;

bool calSwapXY = false;
bool calInvertX = false;
bool calInvertY = false;

int16_t sinTable[TRIG_TABLE_SIZE];

// ---- Core 0 渲染任务 ----
static void renderTask(void* param) {
  while (1) {
    // 等待 Core 1 发出渲染开始信号
    xSemaphoreTake(renderStartSem, portMAX_DELAY);

    // 读取当前绘制缓冲索引并执行渲染
    Game::doRender();

    // 通知 Core 1 渲染完成
    xSemaphoreGive(renderDoneSem);
  }
  (void)param;
}

void initRenderTask() {
  renderStartSem = xSemaphoreCreateBinary();
  renderDoneSem = xSemaphoreCreateBinary();

  xTaskCreatePinnedToCore(
    renderTask, "render", RENDER_STACK_SIZE, nullptr,
    1, &renderTaskHandle, 0);  // 绑定 Core 0
}

void init() {
  // 显示屏
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);  // ST7789 需要字节交换

  // PSRAM 双缓冲精灵
  for (uint8_t i = 0; i < 2; i++) {
    sprBuf[i].setColorDepth(16);
    sprBuf[i].createSprite(SCR_W, SCR_H);
  }
  sprDraw = &sprBuf[0];

  // 按钮引脚
  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_BTN_D, INPUT_PULLUP);

  // 音频
  pinMode(PIN_AUDIO, OUTPUT);

  // 正弦查表 (sin(0°~360°) * 1000)
  for (uint16_t i = 0; i < TRIG_TABLE_SIZE; i++) {
    float rad = i * 2.0f * PI / TRIG_TABLE_SIZE;
    sinTable[i] = (int16_t)(sinf(rad) * 1000.0f);
  }

  // 启动 Core 0 渲染任务
  initRenderTask();

  // 从 NVS 加载摇杆校准设置
  loadCalibration();
}

void readInput() {
  input.joyX = analogRead(PIN_JOY_X);
  input.joyY = analogRead(PIN_JOY_Y);

  // 应用摇杆校准（处理轴交换和翻转）
  if (calSwapXY) {
    uint16_t tmp = input.joyX;
    input.joyX = input.joyY;
    input.joyY = tmp;
  }
  if (calInvertX) input.joyX = JOY_MAX - input.joyX;
  if (calInvertY) input.joyY = JOY_MAX - input.joyY;

  uint8_t btns = 0;
  if (!digitalRead(PIN_BTN_A)) btns |= 0x01;
  if (!digitalRead(PIN_BTN_B)) btns |= 0x02;
  if (!digitalRead(PIN_BTN_C)) btns |= 0x04;
  if (!digitalRead(PIN_BTN_D)) btns |= 0x08;

  input.btnA = btns & 0x01;
  input.btnB = btns & 0x02;
  input.btnC = btns & 0x04;
  input.btnD = btns & 0x08;

  uint8_t rising = btns & ~prevBtns;
  input.btnAPressed = rising & 0x01;
  input.btnBPressed = rising & 0x02;
  input.btnCPressed = rising & 0x04;
  input.btnDPressed = rising & 0x08;

  prevBtns = btns;
}

void beginDraw() {
  sprDraw = &sprBuf[drawIdx];
  sprDraw->fillSprite(TFT_BLACK);
}

void submitDraw() {
  // 推送刚绘制完的缓冲到屏幕
  sprBuf[drawIdx].pushSprite(0, 0);

  // 交换：下次绘制到另一个缓冲
  drawIdx = 1 - drawIdx;
}

uint8_t vecToAngle(int16_t dx, int16_t dy) {
  // atan2 结果转 0-255 角度制
  float rad = atan2f((float)dy, (float)dx);
  if (rad < 0) rad += 2.0f * PI;
  return (uint8_t)(rad * TRIG_TABLE_SIZE / (2.0f * PI)) & 0xFF;
}

int16_t angleDiff(uint8_t a, uint8_t b) {
  int16_t d = (int16_t)a - (int16_t)b;
  if (d > 128) d -= 256;
  if (d < -128) d += 256;
  return d;
}

void saveCalibration() {
  Preferences prefs;
  prefs.begin("calib", false);
  prefs.putBool("swapXY", calSwapXY);
  prefs.putBool("invX", calInvertX);
  prefs.putBool("invY", calInvertY);
  prefs.end();
}

void loadCalibration() {
  Preferences prefs;
  prefs.begin("calib", true);
  calSwapXY = prefs.getBool("swapXY", false);
  calInvertX = prefs.getBool("invX", false);
  calInvertY = prefs.getBool("invY", false);
  prefs.end();
}

}  // namespace Hardware
