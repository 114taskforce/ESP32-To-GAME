#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "config.h"
#include "hardware.h"
#include "network.h"
#include "map.h"
#include "game.h"
#include "render.h"
#include "audio.h"

// 对方 MAC 地址（每台设备需修改为对方的 MAC）
static uint8_t peerMac[6] = {0x28, 0x84, 0x85, 0x49, 0xF2, 0x40};
//static uint8_t peerMac[6] = {0x28, 0x84, 0x85, 0x49, 0xFE, 0xE0};
//static uint8_t peerMac[6] = {0x3C, 0x0F, 0x02, 0xDB, 0x7D, 0x0C};
//28:84:85:49:FE:E0
//3C:0F:02:DB:7D:0C
//28:84:85:49:F2:40
void setup() {
  Serial.begin(115200);

  Hardware::init();

  // MAC 地址小的在上方出生，大的在下方
  uint8_t localMac[6];
  WiFi.macAddress(localMac);
  bool spawnTop = (memcmp(localMac, peerMac, 6) < 0);

  Audio::init();
  Network::init(peerMac);
  Game::init(spawnTop);

  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X, spawn: %s\n",
                localMac[0], localMac[1], localMac[2],
                localMac[3], localMac[4], localMac[5],
                spawnTop ? "TOP" : "BOTTOM");

  // 启动第一帧渲染（后续由主循环驱动流水线）
  xSemaphoreGive(Hardware::renderStartSem);
}

void loop() {
  uint32_t frameStart = micros();

  Hardware::readInput();

  // 结算界面按 D 重启（24fps，不受网络帧限制）
  if (Game::state == Game::STATE_OVER && Hardware::input.btnDPressed) {
    Game::restart();
  }

  // 网络帧（12fps：偶数渲染帧执行）
  if (Render::frameCount % 2 == 0) {
    Game::updateNetwork();
  }

  // 等待 Core 0 完成上一帧渲染
  // （Core 0 大约耗时 ~30ms，此时应该已完成）
  xSemaphoreTake(Hardware::renderDoneSem, pdMS_TO_TICKS(50));

  // 推送上一帧到屏幕 + 交换双缓冲 + 地图快照
  Game::finishRender();

  // 通知 Core 0 开始渲染当前帧
  xSemaphoreGive(Hardware::renderStartSem);

  Render::frameCount = (Render::frameCount + 1) % 24;

  // 帧率同步
  uint32_t elapsed = micros() - frameStart;
  if (elapsed < FRAME_US) {
    delayMicroseconds(FRAME_US - elapsed);
  }
}
