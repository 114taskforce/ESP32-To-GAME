#include "network.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

namespace Network {

uint8_t peerMac[6] = {0};
uint8_t localFrameId = 0;
uint8_t remoteFireSeq = 0;

FrameData remoteFrame;
bool remoteFrameNew = false;

static volatile bool rxFlag = false;
static NetFrame rxBuf;
static uint32_t lastRxMillis = 0;

void onSend(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac;
  (void)status;
}

void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(NetFrame)) {
    memcpy((void*)&rxBuf, data, sizeof(NetFrame));
    rxFlag = true;
  }
  (void)mac;
}

void init(const uint8_t* peer) {
  memcpy(peerMac, peer, 6);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onSend);
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  memset(&remoteFrame, 0, sizeof(remoteFrame));
  lastRxMillis = millis();
  Serial.printf("ESP-NOW ready, peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                peerMac[0], peerMac[1], peerMac[2],
                peerMac[3], peerMac[4], peerMac[5]);
}

void sendFrame(const FrameData& src) {
  NetFrame nf;
  pack(src, nf);
  esp_now_send(peerMac, (uint8_t*)&nf, sizeof(NetFrame));
}

bool hasNewFrame() {
  if (rxFlag) {
    rxFlag = false;
    unpack(rxBuf, remoteFrame);
    remoteFrameNew = true;
    lastRxMillis = millis();
    return true;
  }
  remoteFrameNew = false;
  return false;
}

uint32_t getLastRxMillis() {
  return lastRxMillis;
}

void pack(const FrameData& src, NetFrame& dst) {
  dst.frameId    = src.frameId;
  dst.posX       = src.posX;
  dst.posY_lo    = src.posY & 0xFF;

  dst.flags = 0;
  if (src.stealth)   dst.flags |= 0x01;
  if (src.skillUsed) dst.flags |= 0x02;
  if (src.charging)  dst.flags |= 0x04;
  if (src.posY & 0x100) dst.flags |= 0x08;  // posY_hi = bit 8

  dst.wpnData1  = src.wpnData1;
  dst.wpnData2  = src.wpnData2;
  dst.wpnData3  = src.wpnData3;

  dst.fireDir   = src.fireDir;
  dst.moveSpeed = src.moveSpeed;
  dst.fireSeq   = src.fireSeq;
  dst.hp        = src.hp;
  dst.shieldHp  = src.shieldHp;
}

void unpack(const NetFrame& src, FrameData& dst) {
  dst.frameId    = src.frameId;
  dst.posX       = src.posX;
  dst.posY       = (uint16_t)src.posY_lo | ((src.flags & 0x08) ? 0x100 : 0);

  dst.stealth    = src.flags & 0x01;
  dst.skillUsed  = src.flags & 0x02;
  dst.charging   = src.flags & 0x04;

  // wpnId/skillId 仅在选人阶段有意义（此时 wpnData1/2 携带选择信息）
  dst.wpnId      = src.wpnData1;
  dst.skillId    = src.wpnData2;

  dst.wpnData1  = src.wpnData1;
  dst.wpnData2  = src.wpnData2;
  dst.wpnData3  = src.wpnData3;

  dst.fireDir   = src.fireDir;
  dst.moveSpeed = src.moveSpeed;
  dst.fireSeq   = src.fireSeq;
  dst.hp        = src.hp;
  dst.shieldHp  = src.shieldHp;
}

}  // namespace Network
