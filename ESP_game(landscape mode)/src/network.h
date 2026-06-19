#pragma once
#include "config.h"
#include <cstdint>
#include <esp_now.h>

namespace Network {

// 网络帧（12 字节，严格打包）
// 武器/技能类型仅在选人阶段通过 wpnData1/2 传输一次，对战期间不再发送
// Y 坐标：posY_lo(byte2) + flags.bit3(posY_hi) = 9bit 全精度 (0-319)
#pragma pack(push, 1)
struct NetFrame {
  uint8_t  frameId;       // 0-23 循环
  uint8_t  posX;          // 像素 X (0-239)
  uint8_t  posY_lo;       // Y 低8位 (0-255)
  uint8_t  flags;         // bit0:stealth bit1:skillUsed bit2:charging bit3:posY_hi
                           // bit4-7: reserved
  uint8_t  wpnData1;      // 选人阶段=wpnId, 对战=武器参数1
  uint8_t  wpnData2;      // 选人阶段=skillId, 对战=武器参数2
  uint8_t  wpnData3;      // 武器参数3
  uint8_t  fireDir;       // 移动/发射方向 0-255
  uint8_t  moveSpeed;     // 移动速度 px/s
  uint8_t  fireSeq;       // 开火计数（对方检测变化来涂色）
  uint8_t  hp;            // 当前血量 0-100
  uint8_t  shieldHp;      // 盾血量 0-100 (0=无盾)
};
#pragma pack(pop)

// 解包后的友好结构
struct FrameData {
  uint8_t  frameId;
  uint8_t  posX;
  uint16_t posY;          // 完整 Y (0-319), posY_lo + flags.posY_hi 重建
  bool     stealth;
  bool     skillUsed;
  bool     charging;
  uint8_t  wpnId;         // 选人阶段=wpnData1，对战期间不使用
  uint8_t  skillId;       // 选人阶段=wpnData2，对战期间不使用
  uint8_t  fireSeq;
  uint8_t  wpnData1;      // 武器参数1
  uint8_t  wpnData2;      // 武器参数2
  uint8_t  wpnData3;      // 武器参数3
  uint8_t  fireDir;
  uint8_t  moveSpeed;
  uint8_t  hp;            // 当前血量 0-100
  uint8_t  shieldHp;      // 盾血量 0-100
};

// 敌方开火序列号（用于检测对方是否开火）
extern uint8_t remoteFireSeq;

extern uint8_t peerMac[6];
extern uint8_t localFrameId;

// 收到的对方帧数据
extern FrameData remoteFrame;
extern bool remoteFrameNew;

void init(const uint8_t* peer);
void sendFrame(const FrameData& data);
bool hasNewFrame();

// 获取上次收包时间（ms），用于断线检测
uint32_t getLastRxMillis();

// 打包/解包
void pack(const FrameData& src, NetFrame& dst);
void unpack(const NetFrame& src, FrameData& dst);

}  // namespace Network
