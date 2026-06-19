#include "game.h"
#include "hardware.h"
#include "network.h"
#include "map.h"
#include "raycast.h"
#include "skill.h"
#include "render.h"
#include "audio.h"
#include <cstring>

namespace Game {

State state = STATE_SELECT;
uint8_t selectedWpn   = WPN_PISTOL;
uint8_t selectedSkill = SKILL_DASH;
bool    localReady    = false;
bool    remoteReady   = false;

uint16_t countdownTimer = 0;
uint16_t matchTimer     = 0;

uint16_t allyCells  = 0;
uint16_t enemyCells = 0;

Weapon::State localWpn;

// 输入防抖：选择界面用
static bool selectDebounce = false;

// 本地开火序列号（对方检测变化来涂色）
static uint8_t localFireSeq = 0;
static uint8_t lastFireDir = 0;
static uint8_t  startupIgnoreFrames = 0;  // 开局忽略敌方开火的帧数
static uint16_t deferredPaintCount  = 0;  // 上一帧本地开火捕获的涂色格数
static uint16_t calRawX = 0, calRawY = 0;  // 校准界面原始摇杆值
static uint8_t  calStep = 0;             // 0=推↑校准Y, 1=推→校准X, 2=完成
static bool     calYAxisIsRawX = false;  // Y 轴对应 rawX(1) 还是 rawY(0)
static bool     calYInvertTmp = false;
static bool     calXAxisIsRawY = false;  // X 轴对应 rawY(1) 还是 rawX(0)
static bool     calXInvertTmp = false;
static bool     calBtnDebounce = false;

void restart() {
  Hardware::sprBuf[0].fillSprite(TFT_BLACK);
  Hardware::sprBuf[1].fillSprite(TFT_BLACK);
  Raycast::resetAnims();           // 清空上一局的开火高亮
  Raycast::beginFramePaints();     // 清空涂色缓冲
  deferredPaintCount = 0;          // 清空延迟涂色
  state = STATE_SELECT;
  localReady = false;
  remoteReady = false;
  selectDebounce = false;
}

void init(bool spawnTop) {
  state = STATE_SELECT;
  selectedWpn   = WPN_PISTOL;
  selectedSkill = SKILL_DASH;
  localReady    = false;
  remoteReady   = false;
  countdownTimer = 0;
  matchTimer     = 0;
  allyCells  = 0;
  enemyCells = 0;
  selectDebounce = false;

  Entity::init(spawnTop);
  Map::init();

  memset(&localWpn, 0, sizeof(localWpn));
  localWpn.wpnId = selectedWpn;
}

static void countCells() {
  allyCells = 0;
  enemyCells = 0;
  for (uint16_t i = 0; i < MAP_CELLS; i++) {
    uint8_t sh = (i & 3) << 1;
    uint8_t clr = (Map::data[i >> 2] >> sh) & 0x03;
    if (clr == CLR_ALLY) allyCells++;
    else if (clr == CLR_ENEMY) enemyCells++;
  }
}

// ==================== 每网络帧 ====================
void updateNetwork() {
  // 丢包超时检测：超过 PACKET_TIMEOUT_MS 未收到对方包则回主菜单
  if (state != STATE_SELECT && state != STATE_CALIBRATE &&
      millis() - Network::getLastRxMillis() > PACKET_TIMEOUT_MS) {
    restart();
    return;
  }

  Hardware::Input& in = Hardware::input;

  switch (state) {

  // ---- 赛前选择 ----
  case STATE_SELECT: {
    if (!localReady) {
      // 摇杆选武器（左右）
      if (!selectDebounce) {
        if (in.joyX < JOY_MAX / 3) {
          selectedWpn = (selectedWpn == 0) ? WPN_COUNT - 1 : selectedWpn - 1;
          selectDebounce = true;
        } else if (in.joyX > JOY_MAX * 2 / 3) {
          selectedWpn = (selectedWpn + 1) % WPN_COUNT;
          selectDebounce = true;
        }
        // 摇杆选技能（上下）
        if (in.joyY < JOY_MAX / 3) {
          selectedSkill = (selectedSkill == 0) ? SKILL_COUNT - 1 : selectedSkill - 1;
          selectDebounce = true;
        } else if (in.joyY > JOY_MAX * 2 / 3) {
          selectedSkill = (selectedSkill + 1) % SKILL_COUNT;
          selectDebounce = true;
        }
      }
      if (in.joyX > JOY_MAX / 3 && in.joyX < JOY_MAX * 2 / 3 &&
          in.joyY > JOY_MAX / 3 && in.joyY < JOY_MAX * 2 / 3) {
        selectDebounce = false;
      }

      // 按钮 A 进入摇杆校准
      if (in.btnAPressed) {
        state = STATE_CALIBRATE;
        calStep = 0;
        calBtnDebounce = true;
        break;
      }

      // 按钮 B 确认
      if (in.btnBPressed) {
        localReady = true;
        Audio::play(Audio::SFX_SHIELD_ON);
      }
    }

    // 发送选择状态（含出生位置，避免对方看到位置为(0,0)）
    Network::FrameData fd;
    memset(&fd, 0, sizeof(fd));
    fd.posX    = (uint8_t)Entity::local.posX;
    fd.posY    = (uint16_t)Entity::local.posY;
    fd.wpnData1 = selectedWpn;
    fd.wpnData2 = selectedSkill;
    fd.charging = localReady;
    Network::sendFrame(fd);

    // 接收对方状态（武器/技能选择仅在选人阶段传输）
    if (Network::hasNewFrame()) {
      remoteReady = Network::remoteFrame.charging;
      Entity::remote.currentWpn = Network::remoteFrame.wpnData1;
      Entity::remote.selectedSkill = Network::remoteFrame.wpnData2;
    }

    // 双方确认 → 倒计时
    if (localReady && remoteReady) {
      state = STATE_COUNTDOWN;
      countdownTimer = COUNTDOWN_FRAMES;

      // 应用选择
      localWpn.wpnId = selectedWpn;
      Entity::local.currentWpn = selectedWpn;
      Entity::local.selectedSkill = selectedSkill;
      // 重置双方位置和状态
      Entity::resetLocal();
      Entity::remote.hp = Entity::remote.maxHp;
      Entity::remote.shieldHp = 0;
      Entity::remote.shielded = false;
      Entity::remote.stealth = false;
      Entity::remote.dead = false;
      Entity::remote.deathTimer = 0;
      Entity::remote.invincible = false;
      Entity::remote.invincTimer = 0;
      Map::init();  // 重置地图
    }
    break;
  }

  // ---- 倒计时 ----
  case STATE_COUNTDOWN: {
    // 继续收发网络帧（保持连接，含出生位置）
    Network::FrameData fd;
    memset(&fd, 0, sizeof(fd));
    fd.posX   = (uint8_t)Entity::local.posX;
    fd.posY   = (uint16_t)Entity::local.posY;
    fd.charging = true; // ready
    Network::sendFrame(fd);
    Network::hasNewFrame();

    countdownTimer--;
    if (countdownTimer == 0) {
      state = STATE_PLAYING;
      matchTimer = MATCH_DURATION;
      startupIgnoreFrames = 6;  // 开局 0.5s 忽略敌方开火
      Audio::play(Audio::SFX_STEALTH_ON);
    }
    break;
  }

  // ---- 对战中 ----
  case STATE_PLAYING: {
    Entity::Player& p = Entity::local;
    Entity::Player& r = Entity::remote;

    // 最后一帧（matchTimer==1）：仅接收远端开火、涂色结算，
    // 禁止本地开火和发送，确保双方最后一帧涂色结果一致
    bool lastFrame = (matchTimer == 1);
    if (matchTimer == 0) {
      countCells();
      state = STATE_OVER;
      Audio::play(Audio::SFX_DEATH);
      return;
    }
    matchTimer--;

    // =============================================
    // 第1步：先收远端数据，让远端开火尽早到达
    // =============================================
    Network::hasNewFrame();
    Entity::updateRemoteFromNetwork();

    // 初始化本帧涂色缓冲（用于同帧同格冲突裁决）
    Raycast::beginFramePaints();

    // 回放上一帧捕获的本地开火涂色，与远端开火在同一帧处理，
    // 确保双方设备在同一网络帧内处理相同数据，避免色块不一致
    Raycast::restorePaints(deferredPaintCount);
    deferredPaintCount = 0;

    // =============================================
    // 第2步：处理远端开火/技能（对方上一帧的动作）
    // =============================================

    // 检测对方开火：模拟敌方武器涂色（CLR_ENEMY）
    // 注意：对方死亡时不同步开火，防止丢包导致重生时误触发幻影子弹
    if (Network::remoteFrameNew && startupIgnoreFrames == 0 &&
        Network::remoteFrame.fireSeq != Network::remoteFireSeq) {
      if (r.dead) {
        // 对方已死，只同步序列号，不处理开火
        Network::remoteFireSeq = Network::remoteFrame.fireSeq;
      } else {
        Network::remoteFireSeq = Network::remoteFrame.fireSeq;
      const Weapon::Def& rDef = Weapon::weaponDefs[Entity::remote.currentWpn];
      uint8_t rColor = CLR_ENEMY;
      uint8_t rFireDir = Network::remoteFrame.fireDir;
      int16_t rRange = (int16_t)rDef.range * GRID_SIZE;
      int16_t rx = Entity::remote.posX;
      int16_t ry = Entity::remote.posY;

      // 保存敌方命中目标坐标，设为本地玩家位置
      int16_t saveEx = Raycast::enemyPlayerX;
      int16_t saveEy = Raycast::enemyPlayerY;
      Raycast::enemyPlayerX = p.posX;
      Raycast::enemyPlayerY = p.posY;

      Raycast::Result enemyResult;
      memset(&enemyResult, 0, sizeof(enemyResult));

      switch (Entity::remote.currentWpn) {
        case WPN_PISTOL:
          enemyResult = Raycast::castParallelRays(rx, ry, rFireDir,
                          rDef.splashRadius, rRange, rColor);
          break;
        case WPN_GRENADE: {
          // 直接使用对方发来的爆炸物落点 & 溅射半径
          uint16_t landX = Network::remoteFrame.wpnData1;
          uint16_t landY = (uint16_t)Network::remoteFrame.wpnData2 << 1;
          uint8_t  splashR = Network::remoteFrame.wpnData3;
          if (splashR < 1) splashR = rDef.splashMax;
          enemyResult = Raycast::castRadialRays(
            (int16_t)landX, (int16_t)landY, splashR, rColor, SUB_DMG_GRENADE);
          break;
        }
        case WPN_SNIPER: {
          // 直接使用对方发来的实际射程、溅射半径和伤害
          uint8_t sniperRng = Network::remoteFrame.wpnData1;
          if (sniperRng == 0) sniperRng = rDef.range;
          int16_t rangePx = (int16_t)sniperRng * GRID_SIZE;
          uint8_t splashR = Network::remoteFrame.wpnData2;
          if (splashR == 0) splashR = rDef.splashRadius;
          enemyResult = Raycast::castParallelRays(rx, ry, rFireDir,
                          splashR, rangePx, rColor);
          enemyResult.hitDamage = Network::remoteFrame.wpnData3;
          break;
        }
        case WPN_SHOTGUN: {
          int8_t spreadCone = (int8_t)((int16_t)rDef.aimAngle * 256 / 360);
          uint8_t pellets = rDef.pellets;
          for (uint8_t i = 0; i < pellets; i++) {
            int16_t offset = spreadCone;
            if (pellets > 1)
              offset = (int16_t)spreadCone * (2 * (int16_t)i - (pellets - 1)) / (pellets - 1);
            uint8_t pAngle = (uint8_t)((int16_t)rFireDir + offset) & 0xFF;
            Raycast::Result pr = Raycast::castParallelRays(rx, ry, pAngle,
                                    rDef.splashRadius, rRange, rColor);
            if (pr.hitTarget) {
              enemyResult.hitTarget = true;
              enemyResult.hitDamage += rDef.damage;
            }
          }
          break;
        }
      }

      // 对本地玩家施加伤害
      if (enemyResult.hitTarget && !p.dead) {
        int16_t hitDmg = enemyResult.hitDamage > 0 ? enemyResult.hitDamage : rDef.damage;
        Entity::applyDamage(p, hitDmg);
        Audio::play(Audio::SFX_HIT);
        if (p.hp <= 0) {
          Entity::startDeath(p);
          Audio::play(Audio::SFX_DEATH);
        }
      }

      Raycast::enemyPlayerX = saveEx;
      Raycast::enemyPlayerY = saveEy;
    }
    }

    // 检测对方技能：自爆是唯一需要对方设备模拟的爆炸技能
    if (Network::remoteFrameNew && Network::remoteFrame.skillUsed &&
        Entity::remote.selectedSkill == SKILL_SELFDESTRUCT && !r.dead) {
      int16_t saveEx = Raycast::enemyPlayerX;
      int16_t saveEy = Raycast::enemyPlayerY;
      Raycast::enemyPlayerX = p.posX;
      Raycast::enemyPlayerY = p.posY;

      Raycast::Result skillRes = Raycast::castRadialRays(
        r.posX, r.posY, SELFDESTRUCT_SPLASH, CLR_ENEMY, SUB_DMG_SELFDESTRUCT);

      if (skillRes.hitTarget && !p.dead) {
        Entity::applyDamage(p, skillRes.hitDamage > 0 ? skillRes.hitDamage : SUB_DMG_SELFDESTRUCT);
        Audio::play(Audio::SFX_HIT);
        if (p.hp <= 0) {
          Entity::startDeath(p);
          Audio::play(Audio::SFX_DEATH);
        }
      }

      Raycast::enemyPlayerX = saveEx;
      Raycast::enemyPlayerY = saveEy;
    }

    // =============================================
    // 第3步：本地状态更新
    // =============================================

    // 更新死亡倒计时（本地和远端的复活）
    bool justRespawned = Entity::updateDeath(p);
    Entity::updateDeath(r);

    // 复活的这一帧清除按键/蓄力状态，防止误触发武器/技能
    if (justRespawned) {
      in.btnB = false;
      in.btnAPressed = false;
      in.btnBPressed = false;
      in.btnCPressed = false;
      localWpn.charging = false;
      localWpn.chargeLevel = 0;
      localWpn.cooldown = 5;  // 重生后短暂冷却，防止按键滞留触发开火
    }

    // 开局忽略开火倒计时
    if (startupIgnoreFrames > 0) startupIgnoreFrames--;

    // 武器冷却 & 盾消耗（死亡状态也更新，盾消耗可能导致死亡）
    Entity::updateCooldowns(p);
    localWpn.cooldown = p.wpnCooldown;

    // 远程无敌倒计时（修复：远程复活后 invincible 永不过期）
    Entity::updateInvincible(r);

    // 冷却导致的死亡（盾消耗归零后血量已空）
    if (p.hp <= 0 && !p.dead) {
      Entity::startDeath(p);
      Audio::play(Audio::SFX_DEATH);
    }

    // =============================================
    // 第4步：本地操作（移动、开火、技能）
    // =============================================
    uint8_t sendWpnData1 = 0;
    uint8_t sendWpnData2 = 0;
    uint8_t sendWpnData3 = 0;
    bool    sendSkillUsed = false;

    if (!p.dead) {
      // 移动
      bool wasStealth = p.stealth;
      Entity::updateMovement(p, in.joyX, in.joyY);
      if (wasStealth && !p.stealth) {
        Audio::play(Audio::SFX_STEALTH_OFF);  // 误入敌方色块强制现形
      }

      // 始终同步移动方向到网络帧（远端用 fireDir 推算移动）
      lastFireDir = p.moveAngle;

      // 自瞄检测（每网络帧计算，驱动矩形框指示器）
      if (!r.dead) {
        const Weapon::Def& wdefAim = Weapon::weaponDefs[localWpn.wpnId];
        if (wdefAim.aimAngle > 0) {
          int8_t cone255 = (int8_t)((int16_t)wdefAim.aimAngle * 256 / 360);
          uint8_t aimLock;
          p.aimLocked = Weapon::calcAimAssist(
            p.posX, p.posY, p.moveAngle, cone255,
            r.posX, r.posY,
            wdefAim.rangeMax, aimLock);
        } else {
          p.aimLocked = false;
        }
      } else {
        p.aimLocked = false;
      }

      // 攻击
      bool btnHeld = in.btnB;
      const Weapon::Def& wdef = Weapon::weaponDefs[localWpn.wpnId];
      bool doFire = false;
      bool fireRelease = false;

      // 捕获本地开火/技能的涂色，延迟到下一帧与远端数据合并处理
      Raycast::beginCapturePaint();

      if (wdef.isCharged) {
        if (btnHeld && localWpn.cooldown == 0) {
          if (!localWpn.charging) {
            Weapon::startCharge(localWpn);
            p.charging = true;
          }
          Weapon::updateCharge(localWpn);
        } else if (!btnHeld && localWpn.charging) {
          doFire = true;
          fireRelease = true;
        }
      } else {
        if (btnHeld && localWpn.cooldown == 0) {
          doFire = true;
        }
      }

      // 最后一帧禁止本地开火，确保双方涂色对称
      if (doFire && !lastFrame) {
        // 敌方死亡时不触发命中判定
        Raycast::enemyPlayerX = r.dead ? -100 : r.posX;
        Raycast::enemyPlayerY = r.dead ? -100 : r.posY;

        // 摇杆幅度 0-255（榴弹投掷距离用）
        int16_t jdx = (int16_t)in.joyX - JOY_MAX / 2;
        int16_t jdy = (int16_t)in.joyY - JOY_MAX / 2;
        int32_t magSq = (int32_t)jdx * jdx + (int32_t)jdy * jdy;
        int32_t maxSq = (int32_t)(JOY_MAX / 2) * (JOY_MAX / 2);
        uint8_t joyMag;
        if (magSq >= maxSq) joyMag = 255;
        else joyMag = (uint8_t)(sqrtf((float)magSq) * 255 / (JOY_MAX / 2));

        Weapon::FireResult fr = Weapon::fire(
          localWpn, p.posX, p.posY, p.moveAngle,
          r.posX, r.posY,
          wdef.isCharged ? false : true,   // btnHeld
          wdef.isCharged ? true : false,   // btnReleased
          joyMag);

        if (fr.fired) {
          // 开火自动退出潜入
          if (p.stealth) {
            p.stealth = false;
            Audio::play(Audio::SFX_STEALTH_OFF);
          }
          p.charging = false;
          p.wpnCooldown = localWpn.cooldown;
          localFireSeq++;
          lastFireDir = fr.fireDir;
          sendWpnData1 = fr.param1;
          sendWpnData2 = fr.param2;
          sendWpnData3 = fr.param3;
          switch (fr.wpnId) {
            case WPN_PISTOL:  Audio::play(Audio::SFX_SHOOT_PISTOL); break;
            case WPN_GRENADE: Audio::play(Audio::SFX_GRENADE_LAUNCH); break;
            case WPN_SNIPER:  Audio::play(Audio::SFX_SHOOT_SNIPER); break;
            case WPN_SHOTGUN: Audio::play(Audio::SFX_SHOOT_SHOTGUN); break;
          }
          if (fr.rayResult.hitTarget && !r.dead) {
            Entity::applyDamage(r, fr.rayResult.hitDamage);
            Audio::play(Audio::SFX_HIT);
            if (r.hp <= 0) {
              Entity::startDeath(r);
              Audio::play(Audio::SFX_DEATH);
            }
          }
        }
      }

      // 潜入
      if (in.btnAPressed) {
        Entity::toggleStealth(p);
        Audio::play(p.stealth ? Audio::SFX_STEALTH_ON : Audio::SFX_STEALTH_OFF);
      }

      // 技能
      if (in.btnCPressed && !lastFrame) {
        Raycast::enemyPlayerX = r.dead ? -100 : r.posX;
        Raycast::enemyPlayerY = r.dead ? -100 : r.posY;

        Skill::SkillResult sr = Skill::tryUseSkill(p, selectedSkill);
        if (sr.used) {
          // 自爆自动退出潜入（冲撞和盾在技能内已阻止潜入时使用）
          if (p.stealth) {
            p.stealth = false;
            Audio::play(Audio::SFX_STEALTH_OFF);
          }
          sendSkillUsed = true;
          switch (sr.skillId) {
            case SKILL_SELFDESTRUCT:
              Audio::play(Audio::SFX_SELFDESTRUCT);
              if (sr.rayResult.hitTarget && !r.dead) {
                Entity::applyDamage(r, sr.rayResult.hitDamage);
                if (r.hp <= 0) {
                  Entity::startDeath(r);
                  Audio::play(Audio::SFX_DEATH);
                }
              }
              break;
            case SKILL_DASH:
              Audio::play(Audio::SFX_DASH);
              break;
            case SKILL_SHIELD:
              Audio::play(Audio::SFX_SHIELD_ON);
              break;
          }
        }
      }

      // 潜入回复
      if (p.stealth) Entity::applyRegen(p);

      // 自身行动导致的死亡（如自爆自伤、盾消耗）
      if (p.hp <= 0 && !p.dead) {
        Entity::startDeath(p);
        Audio::play(Audio::SFX_DEATH);
      }

      Raycast::endCapturePaint();
      uint16_t capCount;
      uint16_t* capCells;
      uint8_t* capColors;
      int16_t* capDists;
      Raycast::snapshotPaints(capCount, capCells, capColors, capDists);
      deferredPaintCount = capCount;
    }

    // =============================================
    // 第5步：最后发送（远端在下一帧开头就能收到）
    // =============================================
    Network::FrameData fd;
    fd.frameId    = Network::localFrameId;
    fd.posX       = (uint8_t)p.posX;
    fd.posY       = (uint16_t)p.posY;
    fd.stealth    = p.dead ? false : p.stealth;
    fd.skillUsed  = p.dead ? false : sendSkillUsed;
    fd.charging   = p.dead ? false : localWpn.charging;
    fd.wpnData1   = p.dead ? 0 : sendWpnData1;
    fd.wpnData2   = p.dead ? 0 : sendWpnData2;
    fd.wpnData3   = p.dead ? 0 : sendWpnData3;
    fd.fireDir    = lastFireDir;
    fd.moveSpeed  = p.dead ? 0 : (uint8_t)p.moveSpeed;
    fd.fireSeq    = localFireSeq;
    fd.hp         = (uint8_t)p.hp;
    fd.shieldHp   = p.shielded ? (uint8_t)p.shieldHp : (uint8_t)0;

    Network::sendFrame(fd);
    Network::localFrameId = (Network::localFrameId + 1) % 24;

    // 应用本帧涂色缓冲（冲突格以距离近者为准）
    Raycast::applyFramePaints();
    Raycast::updateAnims();
    break;
  }

  // ---- 结算 ----
  case STATE_OVER: {
    // 收发帧保持连接（重启检测在主循环中 24fps）
    Network::FrameData fd;
    memset(&fd, 0, sizeof(fd));
    Network::sendFrame(fd);
    Network::hasNewFrame();
    break;
  }

  // ---- 摇杆校准 ----
  case STATE_CALIBRATE: {
    Hardware::Input& in = Hardware::input;

    // 读取原始摇杆值（不经过校准变换），直接读取 ADC
    calRawX = analogRead(PIN_JOY_X);
    calRawY = analogRead(PIN_JOY_Y);

    if (!in.btnB && !in.btnA) {
      calBtnDebounce = false;
    }

    if (in.btnB && !calBtnDebounce) {
      calBtnDebounce = true;

      if (calStep == 0) {
        // 第1步：推↑（上方），校准 Y 轴
        // 判断哪个原始轴更接近极端值 → 那就是 Y 轴
        uint16_t extX = (calRawX > JOY_MAX / 2) ? calRawX : (JOY_MAX - calRawX);
        uint16_t extY = (calRawY > JOY_MAX / 2) ? calRawY : (JOY_MAX - calRawY);
        if (extY >= extX) {
          calYAxisIsRawX = false;  // Y 来自 rawY
          calYInvertTmp  = (calRawY > JOY_MAX / 2);  // 上=小值, 若为大值则翻转
        } else {
          calYAxisIsRawX = true;   // Y 来自 rawX
          calYInvertTmp  = (calRawX > JOY_MAX / 2);
        }
        calStep = 1;
      } else if (calStep == 1) {
        // 第2步：推→（右方），校准 X 轴
        // X 轴必须用另一个物理轴
        if (calYAxisIsRawX) {
          // Y 用了 rawX，X 必须用 rawY
          calXAxisIsRawY = true;
          calXInvertTmp  = (calRawY < JOY_MAX / 2);  // 右=大值, 若为小值则翻转
        } else {
          // Y 用了 rawY，X 必须用 rawX
          calXAxisIsRawY = false;
          calXInvertTmp  = (calRawX < JOY_MAX / 2);
        }
        // 计算最终校准参数
        Hardware::calSwapXY = calYAxisIsRawX && calXAxisIsRawY;
        Hardware::calInvertX = calXInvertTmp;
        Hardware::calInvertY = calYInvertTmp;
        Hardware::saveCalibration();  // 写入 NVS 持久化
        calStep = 2;
      }
    }

    // 按 A 返回选择界面（任意步骤均可返回）
    if (in.btnA && !calBtnDebounce) {
      calBtnDebounce = true;
      state = STATE_SELECT;
    }
    break;
  }
  } // switch
}

// ==================== Core 0 渲染（24fps）====================
void doRender() {
  Hardware::beginDraw();

  switch (state) {

  case STATE_SELECT:
    Render::drawSelectScreen(selectedWpn, selectedSkill, localReady, remoteReady);
    break;

  case STATE_COUNTDOWN:
    Render::drawMap(Map::data, Map::prev);
    Render::drawCountdown((countdownTimer + 11) / FPS_NETWORK); // 3,2,1,0=GO
    break;

  case STATE_PLAYING: {
    Entity::Player& p = Entity::local;
    Entity::Player& r = Entity::remote;

    Render::drawMap(Map::data, Map::prev);

    // 死亡倒计时
    if (p.dead) {
      Render::drawDeathTimer((p.deathTimer + FPS_NETWORK - 1) / FPS_NETWORK);
    }

    // 指示线（本地存活时始终显示）
    if (!p.dead) {
      const Weapon::Def& wdef = Weapon::weaponDefs[localWpn.wpnId];
      uint16_t rangePx;
      if (wdef.isThrowable) {
        // 投掷物：指示线终点 = 摇杆幅度决定的实际落点
        int16_t jdx = (int16_t)Hardware::input.joyX - JOY_MAX / 2;
        int16_t jdy = (int16_t)Hardware::input.joyY - JOY_MAX / 2;
        int32_t magSq = (int32_t)jdx * jdx + (int32_t)jdy * jdy;
        int32_t maxSq = (int32_t)(JOY_MAX / 2) * (JOY_MAX / 2);
        uint8_t joyMag;
        if (magSq >= maxSq) joyMag = 255;
        else joyMag = (uint8_t)(sqrtf((float)magSq) * 255 / (JOY_MAX / 2));
        int16_t maxThrowDist = (int16_t)wdef.range * GRID_SIZE;
        rangePx = (uint16_t)(((int16_t)maxThrowDist * joyMag) / 255);
        if (rangePx < GRID_SIZE) rangePx = GRID_SIZE;  // 最小1格，避免零长度
      } else if (wdef.isCharged && localWpn.charging) {
        // 蓄力型（狙击枪）：指示线随蓄力等级伸长
        uint8_t dispRange = wdef.range +
          ((wdef.rangeMax - wdef.range) * localWpn.chargeLevel) / 255;
        rangePx = (uint16_t)dispRange * GRID_SIZE;
      } else {
        rangePx = (uint16_t)wdef.range * GRID_SIZE;
      }
      bool showInd = !r.stealth && !r.dead;
      Render::drawIndicator(p.posX, p.posY, p.moveAngle,
                            wdef.aimAngle, rangePx, showInd);
    }

    // 远程玩家位置预测（24fps 航位推算，平滑移动）
    Entity::predictRemotePosition();

    // 玩家（死亡时显示红色 X 标记）
    bool aimLocked = p.aimLocked && localWpn.wpnId != WPN_GRENADE;
    if (!p.dead) {
      Render::drawPlayer(p.posX, p.posY, p.stealth, false, false);
    } else {
      Render::drawDeathMarker(p.deathX, p.deathY);
    }
    if (!r.dead) {
      Render::drawPlayer(r.rposX, r.rposY, r.stealth, aimLocked, true);
    } else {
      Render::drawDeathMarker(r.deathX, r.deathY);
    }

    Render::drawEffects();
    uint8_t skillCdVal  = p.skillCd[selectedSkill];
    uint8_t skillCdMax  = (selectedSkill == SKILL_SELFDESTRUCT) ? CD_SELFDESTRUCT
                        : (selectedSkill == SKILL_DASH) ? CD_DASH
                        : CD_SHIELD;
    uint8_t wpnCdMax    = Weapon::weaponDefs[localWpn.wpnId].fireRate;
    Render::drawUI(p.hp, p.maxHp, p.shielded ? p.shieldHp : 0,
                   selectedWpn, selectedSkill,
                   skillCdVal, skillCdMax,
                   localWpn.cooldown, wpnCdMax,
                   matchTimer / FPS_NETWORK, p.stealth);
    break;
  }

  case STATE_CALIBRATE:
    Render::drawCalibrate(calRawX, calRawY, calStep,
                          calYAxisIsRawX, calYInvertTmp,
                          calXAxisIsRawY, calXInvertTmp,
                          Hardware::calSwapXY,
                          Hardware::calInvertX, Hardware::calInvertY);
    break;

  case STATE_OVER:
    Render::drawMap(Map::data, Map::prev);
    Render::drawResult(allyCells, enemyCells);
    break;
  }
}

// ==================== Core 1 渲染帧收尾（24fps）====================
void finishRender() {
  Hardware::submitDraw();  // DMA 推送 + 交换双缓冲
  Map::snapshot();         // 必须在渲染完成后
}

}  // namespace Game
