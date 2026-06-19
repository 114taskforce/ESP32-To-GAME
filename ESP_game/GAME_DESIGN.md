# ESP-NOW 双人对战射击游戏 — 完整设计文档

## 目录

1. [概述](#1-概述)
2. [硬件与控制](#2-硬件与控制)
3. [游戏流程](#3-游戏流程)
4. [地图系统](#4-地图系统)
5. [移动与碰撞](#5-移动与碰撞)
6. [角度系统](#6-角度系统)
7. [武器系统](#7-武器系统)
8. [自瞄系统](#8-自瞄系统)
9. [探测射线系统](#9-探测射线系统)
10. [技能系统](#10-技能系统)
11. [潜入系统](#11-潜入系统)
12. [网络协议](#12-网络协议)
13. [音频系统](#13-音频系统)
14. [渲染系统](#14-渲染系统)
15. [性能优化](#15-性能优化)
16. [常量速查](#16-常量速查)
17. [搭建指南](#17-搭建指南)

---

## 1. 概述

这是一款基于 ESP32-S3 + ST7789 显示屏的双人无线对战射击游戏。两名玩家各持一台设备，通过 ESP-NOW 协议进行无线通信（12fps），在 60×80 的网格地图上进行射击、涂色、潜入和技能对抗。游戏以涂色面积决定胜负。

- **平台**：ESP32-S3 N16R8（16MB Flash + 8MB PSRAM）
- **屏幕**：ST7789，240×320 像素
- **通信**：ESP-NOW 点对点，12fps 网络帧率
- **渲染**：TFT_eSPI 库，24fps 渲染帧率
- **帧预算**：每帧约 41.7ms（24fps）

## 2. 硬件与控制

### 2.1 输入设备

| 组件 | GPIO | 功能 |
|------|------|------|
| 摇杆 X 轴 | GPIO1 | 模拟量 0-4095，控制左右移动 |
| 摇杆 Y 轴 | GPIO2 | 模拟量 0-4095，控制上下移动 |
| 按钮 A | GPIO4 | 潜入（Stealth）切换 |
| 按钮 B | GPIO5 | 攻击（开火/蓄力） |
| 按钮 C | GPIO6 | 技能释放 |
| 按钮 D | GPIO7 | 预留 |
| 音频输出 | GPIO17 | Sigma-Delta 调制音频 |

### 2.2 按钮边缘检测

所有按钮支持两种读取模式：
- **持续按下**：`input.btnA / btnB / btnC / btnD`（当前状态）
- **按下瞬间**：`input.btnAPressed / btnBPressed / btnCPressed / btnDPressed`（上升沿，单帧有效）

摇杆设有死区（`JOY_DEADZONE = 200`），避免漂移。

### 2.3 操作说明

| 阶段 | 摇杆 | 按钮 B | 按钮 A | 按钮 C |
|------|------|--------|--------|--------|
| 选择界面 | 上下选技能，左右选武器 | 确认选择 | — | — |
| 对战中 | 移动 | 开火/蓄力 | 潜入 | 技能 |
| 结算 | — | 重新开始 | — | — |

### 2.4 摇杆校准

防止摇杆模块安装方向错误（XY 轴交换、方向反转）导致操控异常。校准参数在完成校准后**自动保存到 NVS（非易失性存储）**，断电重启后依然有效，无需重复校准。

**进入与退出：**
- 在选择界面按 **按钮 A** 进入校准界面
- 校准完成后或校准过程中按 **按钮 A** 返回选择界面

**两步校准流程：**

| 步骤 | 操作 | 说明 |
|------|------|------|
| Step 1 — 校准 Y 轴 | 将摇杆推向**正上方**，按 **按钮 B** | 系统检测摇杆哪个物理轴偏离中心最远，自动判断 Y 轴对应 rawX 还是 rawY，并确定是否需要翻转 |
| Step 2 — 校准 X 轴 | 将摇杆推向**最右边**，按 **按钮 B** | 用剩余的物理轴校准 X 轴，同样自动判断是否需要翻转；完成后写入 NVS |

**校准逻辑：**
1. **Y 轴判定**：比较 rawX 和 rawY 的偏离程度（与中心值 2048 的距离），偏离更大的那个物理轴被识别为 Y 轴；若原始值大于中心说明方向需翻转
2. **X 轴判定**：用另一个物理轴（未被 Y 占用的）作为 X 轴；若推右时原始值小于中心说明方向需翻转
3. **最终参数**：
   - `calSwapXY`：当 Y 来自 rawX 且 X 来自 rawY 时设为 true，即两轴交换
   - `calInvertX`：X 轴是否需要翻转
   - `calInvertY`：Y 轴是否需要翻转

**NVS 持久化：**
- 使用 ESP32 `Preferences` 库，存储在 `"calib"` 命名空间下
- 保存三个 bool 值：`swapXY`、`invX`、`invY`
- 启动时 `Hardware::init()` 自动调用 `loadCalibration()` 恢复上次校准
- 校准第 2 步完成时自动调用 `saveCalibration()` 写入 NVS

**校准界面显示：**
- 上方显示当前步骤提示（"Push UP + B" / "Push RIGHT + B"）
- 中间显示 rawX 和 rawY 原始 ADC 值
- 下方实时可视化：摇杆位置用白色圆点表示，四个角标标记方向，UP 和 RIGHT 方向在对应步骤高亮
- 底部显示最终校准参数状态（swapXY, invertX, invertY）

## 3. 游戏流程

游戏使用四状态状态机：

```
STATE_SELECT → STATE_COUNTDOWN → STATE_PLAYING → STATE_OVER
     ↑                                                  |
     └──────────────────── 重新开始 ←────────────────────┘
```

### 3.1 选择阶段 (STATE_SELECT)

- 每位玩家使用摇杆选择武器（左右）和技能（上下）
- 摇杆带防抖，回到中心区域后重新激活
- 按 **按钮 B** 确认选择，本地播放确认音效
- 通过 ESP-NOW 同步双方的选择状态：`wpnId`、`skillId`、`firing`（作为 ready 标志）
- **双方都确认后**，进入倒计时阶段
- 双方位置重置到出生点，地图重置

### 3.2 倒计时阶段 (STATE_COUNTDOWN)

- 持续 3 秒（36 网络帧）
- 显示 3 → 2 → 1 → GO!
- 公式：`(countdownTimer + 11) / 12`，保证最后 1 帧显示 "GO!"
- 持续维持网络连接

### 3.3 对战阶段 (STATE_PLAYING)

- 持续 90 秒（1080 网络帧）
- 每帧执行：移动、武器攻击、技能、潜入、血量回复、死亡检测
- 网络同步在偶数渲染帧执行（12fps）
- 涂色动画持续更新
- 计时归零 → 进入结算

#### 死亡与复活

- **死亡触发**：HP ≤ 0 时进入死亡状态
- **死亡期间**：无法移动、攻击、使用技能或潜入；玩家本体不渲染
- **复活时间**：`RESPAWN_FRAMES = 12` 网络帧（1 秒）
- **复活**：传送回出生点，满血满状态，所有冷却清零
- **复活无敌**：复活后 `INVINCIBILITY_FRAMES = 12` 网络帧（1 秒）内不受任何伤害（伤害判定直接跳过，不现形、不扣盾、不扣血）

### 3.4 结算阶段 (STATE_OVER)

- 遍历地图所有格子（4800 个），统计 `CLR_ALLY` 和 `CLR_ENEMY` 数量
- 比较双方涂色面积：多者获胜
- 显示：
  - WIN/LOSE 标题
  - 己方格数（绿色）、敌方格数（红色）
  - 覆盖率百分比
  - "Press B to restart" 提示
- 按 **按钮 B** → 回到选择界面

## 4. 地图系统

### 4.1 网格规格

| 参数 | 值 |
|------|-----|
| 网格尺寸 | 4×4 像素/格 |
| 总列数 | 60（240 / 4） |
| 总行数 | 80（320 / 4） |
| 总格子数 | 4800 |
| 存储空间 | 1200 bytes |

### 4.2 2bit 紧凑编码

每个格子用 2bit 存储颜色，4 个格子打包进 1 个 `uint8_t`：

```
byte N:  [bb aa] [dd cc] [ff ee] [hh gg]
         格4N+3  格4N+2  格4N+1  格4N+0
```

位移计算：
- 格子索引 `idx = row * 60 + col`
- 字节索引 `byteIdx = idx / 4`（即 `idx >> 2`）
- 位移量 `shift = (idx % 4) * 2`（即 `(idx & 3) << 1`）

### 4.3 颜色编码

| 编码 | 宏定义 | 含义 | 显示颜色 |
|------|--------|------|----------|
| 0b00 | `CLR_NEUTRAL` | 中立（未涂色） | 深灰 (#2124) |
| 0b01 | `CLR_ENEMY` | 敌方色块 | 红色 (#F800) |
| 0b10 | `CLR_ALLY` | 己方色块 | 绿色 (#07E0) |
| 0b11 | `CLR_WALL` | 墙壁（不可通行/不可涂色） | 灰色 (#8410) |

### 4.4 地图初始布局

```
壁                 壁
壁   ▄▄       ▄▄   壁       上方平台: (15,20) 10×3 和 (38,20) 10×3
壁                 壁
壁                 壁       中央掩体:
壁   ██        ██  壁         (12,36) 4×8, (44,36) 4×8
壁       ████      壁         (27,38) 6×4
壁                 壁
壁                 壁       下方对称:
壁   ▄▄       ▄▄   壁        (15,57) 10×3, (38,57) 10×3
壁                 壁
```

- 最外层 2 格宽的边界墙（上下左右各 2 行/列）
- 完全对称布局，确保双方公平

### 4.5 坐标转换

```cpp
// 像素坐标 → 网格列号
uint8_t pxToGrid(int16_t px) { return px / GRID_SIZE; }

// 网格坐标 → 颜色查询
uint8_t pixelColor(int16_t px, int16_t py) {
    return getColor(pxToGrid(px), pxToGrid(py));
}
```

### 4.6 墙壁检测

`isPixelWall(px, py)` 用于碰撞检测，检查像素坐标所在的网格是否为墙。屏幕边界外始终视为墙壁。

## 5. 移动与碰撞

### 5.1 速度系统

根据玩家当前状态，速度按以下优先级计算：

| 优先级 | 状态 | 速度 (px/s) | 说明 |
|--------|------|-------------|------|
| 1 | 盾激活 | 96 | SPD_SHIELD，+60% |
| 2 | 蓄力中（榴弹） | 48 | SPD_CHARGE_B，×0.8 |
| 3 | 蓄力中（狙击） | 36 | SPD_CHARGE_C，×0.6 |
| 4 | 潜入中 | 120 | SPD_STEALTH，×2.0 |
| 5 | 站在敌方色块上 | 54 | SPD_ENEMY_TILE，×0.9 |
| 6 | 默认 | 60 | SPD_BASE |

### 5.2 移动计算

1. 读取摇杆 X/Y 值（0-4095），转换为以中心（2047）为原点的偏移量
2. 死区过滤：|dx| < 200 或 |dy| < 200 时不移动
3. 使用 `atan2f` 计算移动方向角度（转为 0-255 制存储于 `moveAngle`）
4. 摇杆幅度 = 偏移量 / 2047（钳位到 0.0-1.0），作为速度倍率
5. 本帧移动距离 = `moveSpeed / 24 * 幅度`（每渲染帧）
6. 使用 `cosf/sinf` 分解为 XY 分量

### 5.3 墙壁碰撞（分轴滑墙）

检测 7×7 碰撞盒的 8 个检测点：4 个角 + 4 条边的中点。

分轴检测：
1. 先尝试 X 轴移动：如果 `collidesWithWall(newX, currentY)` 为假，则接受 newX
2. 再尝试 Y 轴移动：如果 `collidesWithWall(currentX, newY)` 为假，则接受 newY

分轴检测允许玩家沿墙壁滑动，而不是被墙角卡住。

### 5.4 边界限制

玩家碰撞盒（半径 3px）始终保持在屏幕范围内（0-239, 0-319）。

## 6. 角度系统

### 6.1 0-255 角度编码

整个游戏使用 `uint8_t` 表示角度，0-255 对应 0°-360°：
- 0° = 0（正右方向）
- 90° = 64（正下方向）
- 180° = 128（正左方向）
- 270° = 192（正上方向）
- 精度：约 1.406°

### 6.2 正弦查表

预计算 256 个 int16_t 值，存储 `sin(θ) * 1000`，范围 -1000 ~ 1000。

余弦通过查表 `sinTable[(angle + 64) & 0xFF]` 获得（即 sin(θ+90°)）。

角度转方向向量：
```cpp
void angleToVec(uint8_t angle, int16_t& dx, int16_t& dy) {
    dx = sinTable[(angle + 64) & 0xFF];  // cos
    dy = sinTable[angle];                 // sin
}
```

### 6.3 两点间角度

```cpp
uint8_t vecToAngle(int16_t dx, int16_t dy) {
    float rad = atan2f(dy, dx);
    if (rad < 0) rad += 2π;
    return (rad * 256 / (2π)) & 0xFF;
}
```

### 6.4 角度差

```cpp
int16_t angleDiff(uint8_t a, uint8_t b) {
    int16_t d = (int16_t)a - (int16_t)b;
    if (d > 128)  d -= 256;  // 顺时针超过半圈
    if (d < -128) d += 256;  // 逆时针超过半圈
    return d;                 // 范围 -128 ~ 128
}
```

## 7. 武器系统

### 7.1 武器参数表

> 所有数值定义在 `config.h` 中（宏名格式：`WPN_{P|G|S|SH}_<字段>`），`weapon.cpp` 的 `weaponDefs[]` 引用宏构建。

| 参数 | 手枪 (PISTOL) | 榴弹 (GRENADE) | 狙击 (SNIPER) | 霰弹 (SHOTGUN) |
|------|:---:|:---:|:---:|:---:|
| type | 0 | 1 | 2 | 3 |
| 蓄力型 | 否 | 是 | 是 | 否 |
| 投掷物 | 否 | 是 | 否 | 否 |
| 基础伤害 | 20 | 2 (小子弹) | 20→60 | 25 |
| 射速 (网络帧) | 3 | 6 | 6 | 6 |
| 溅射半径 (网格) | 1 | — | 1 | 1 |
| 射程 (网格) | 16 | 8 (摇杆决定) | 12→32 | 12 |
| 射程上限 (网格) | 16 | 8 | 32 | 12 |
| 溅射下限 | 2 | 4 | 1 | 1 |
| 溅射上限 | 2 | 8 | 2 | 1 |
| 伤害上限 | 20 | 2 | 60 | 25 |
| 弹丸数 | 1 | — | 1 | 3 |
| 自瞄角度 | ±15° | — | ±8° | ±20° |

### 7.2 武器分类

#### 非蓄力型（手枪、霰弹）

- **按下按钮 B**（上升沿）即发射
- 有冷却间隔（cooldown），冷却中无法发射
- 冷却以网络帧计数（12fps）

#### 蓄力型（榴弹、狙击）

- **按住按钮 B**：开始蓄力，`chargeLevel` 从 0 递增
- 每网络帧 +21，12 帧达到 255（约 1 秒满蓄力）
- **松开按钮 B**：发射，伤害/射程/溅射根据蓄力等级线性插值
- 蓄力期间移动速度降低（榴弹 ×0.8，狙击 ×0.6）

### 7.3 各武器详细机制

#### 手枪 (WPN_PISTOL)

```
伤害 20，溅射半径 1，射程 16 格
射速 3 帧（每秒 4 发）
自瞄 ±15°（格数判定，16 格外不自瞄）

调用 castParallelRays()：
  numRays = 2×1+1 = 3 条平行射线
  每条间距 1 格，覆盖宽 2 格的矩形
```

#### 榴弹 (WPN_GRENADE)

```
小子弹伤害 2，蓄力溅射 4→8，射程 8 格（最大）
射速 6 帧（每秒 2 发）
无自瞄

落点计算（摇杆控制）：
  投掷距离 = 最大射程 × 摇杆幅度 / 255
  落点 = 玩家位置 + 移动方向 × 投掷距离

调用 castRadialRays()：
  径向射线数 = 3×splashRadius（最少 8，最多 64）
  每条径向射线发射 splash=5 的小子弹
  子伤害 = SUB_DMG_GRENADE (8)

蓄力时指示线终点 = 摇杆落点（实时预览）
溅射半径 = lerp(4, 8, chargeLevel/255)
```

#### 狙击 (WPN_SNIPER)

```
伤害 20→60，溅射 1→2，射程 12→32 格
射速 6 帧，自瞄 ±8°（格数判定，32 格外不自瞄）

伤害 = lerp(20, 60, chargeLevel/255)
射程 = lerp(12, 32, chargeLevel/255)
溅射 = lerp(1, 2, chargeLevel/255)  // 蓄力越满溅射越宽

调用 castParallelRays()：
  numRays = 2×splashRadius+1，射线斜向时格数自动补偿
```

#### 霰弹 (WPN_SHOTGUN)

```
每弹丸伤害 25，3 发弹丸，溅射半径 1，射程 12 格
射速 6 帧（每秒 2 发）
自瞄 ±20°（格数判定，12 格外不自瞄；同时用作散布角）

弹丸分布：
  3 发弹丸均匀分布在 ±spreadCone 范围内
  弹丸 0：左偏移 spreadCone
  弹丸 1：居中（fireDir）
  弹丸 2：右偏移 spreadCone

每发弹丸独立调用 castParallelRays()（溅射 1）
总伤害 = 命中弹丸数 × 25
```

### 7.4 蓄力系统

```
蓄力速度：CHARGE_SPEED = 21/帧
满蓄力：255 = 约 12 帧 ≈ 1 秒

线性插值公式：
  uint8_t  lerp(a, b, t) = a + (b-a) * t / 255
  int16_t  lerp(a, b, t) = a + (b-a) * t / 255

开火后自动重置：
  charging = false
  chargeLevel = 0
  cooldown = fireRate
```

## 8. 自瞄系统

### 8.1 启用条件

- 武器有 `aimAngle > 0`（手枪 15°、狙击 8°、霰弹 20°，榴弹无自瞄）
- 敌方位置已知（`targetX >= 0`）
- 敌方在武器射程格数内（切比雪夫距离：`|Δgx| ≤ rangeMax` 且 `|Δgy| ≤ rangeMax`）

### 8.2 双向检测 + 距离过滤算法

```
calcAimAssist(shooterX, shooterY, moveAngle, aimCone, targetX, targetY, maxGrids, aimOut):
    1. 距离过滤：双方转网格坐标，若 |gx1-gx2| > maxGrids 或 |gy1-gy2| > maxGrids → 返回 false
    2. 计算射手到目标的向量角度 (toEnemy)
    3. 检测 toEnemy 是否在 moveAngle ± aimCone 范围内
       → 是：aimOut = toEnemy，返回 true
    4. 检测 toEnemy 是否在 (moveAngle+180°) ± aimCone 范围内
       → 是：aimOut = toEnemy，返回 true  // 支持后退时攻击
    5. 返回 false
```

双向检测的原因：玩家可以向后方移动的同时攻击前方的敌人（摇杆方向≠攻击方向）。
`maxGrids` 直接取武器 `rangeMax`（格数），不再换算像素，避免溢出。

### 8.3 视觉反馈

- 自瞄锁定中 → 玩家光标显示为**方形**（`drawRect`）
- 未锁定 → 玩家光标显示为**圆形**（`drawCircle`）
- 指示线显示自瞄锥形（两条斜线 ± `aimAngle` 范围）

## 9. 探测射线系统

这是涂色和伤害判定的核心系统，所有武器和技能最终都通过射线函数完成效果。

### 9.1 单条射线 DDA 遍历

`castSingleRay(sx, sy, angle, rangePx, color, result)`

使用 DDA（Digital Differential Analyzer）算法逐格遍历：

```
1. 用 sinTable 查表获取方向向量 (cosVal, sinVal)
2. 计算当前网格坐标 (gx, gy)
3. 计算 tMaxX/tMaxY（到达下一个网格边界所需的 DDA 参数）
4. 格数角度补偿：maxCells = (rangePx/GRID_SIZE) × (|cos|+|sin|) / 1000
   （斜向需要更多格才能达到相同欧氏距离）
5. 循环步进：
   - 比较 tMaxX 和 tMaxY，选择更近的一个
   - 移动到相邻格子
   - 遇到墙壁 → 立即停止
   - 对目标格子执行涂色
   - 记录 paintedCells（最多 64 个）
   - 检测该格子是否覆盖敌方玩家中心像素 → hitTarget = true
   - 格数达到 maxCells → 停止
```

### 9.2 平行射线组（矩形溅射）

`castParallelRays(sx, sy, dirAngle, splashRadius, rangePx, color)`

```
射线数 = 2 × splashRadius + 1
每条射线垂直于 dirAngle 偏移 (i - splashRadius) 格

效果：形成一条宽 2×splashRadius+1 格、长 rangePx 的矩形涂色带

射线不重叠：每条射线间距 = 1 网格单元，紧邻排布
```

用于：手枪、狙击枪、霰弹枪

### 9.3 径向射线组（圆形溅射）

`castRadialRays(cx, cy, splashRadius, color, subDmg)`

```
射线数 = 3 × splashRadius（最少 8，最多 64）
等角分布：angle = i * 256 / numRays

每条径向射线调用 castParallelRays(cx, cy, angle, SUB_BULLET_SPLASH=6, rangePx, color)

效果：每条径向射线发射一个溅射半径 6 的小子弹
      多个小子弹的矩形涂色区域组合近似圆形

子伤害：命中敌方时造成 subDmg 伤害（2 或 3）
```

用于：榴弹（subDmg=2）、自爆技能（subDmg=3）

### 9.4 动画系统

```
animCells[128]：循环数组，存储被涂色格子的动画信息
animCount：当前动画格子数

动画格子生命周期：
  涂色时 → addAnimCell(cellIndex, 4)  // 4 帧闪烁
  每帧   → updateAnims()             // remainFrames--
  remainFrames ≤ 1 → 移除

渲染时：
  animFrame > 0 → 交替显示白色/本色（闪烁效果）
  animFrame = 0 → 正常颜色

去重：同一格子再次被涂色时，重置 remainFrames 为 4
```

## 10. 技能系统

玩家在赛前选择一个技能，对战中按 **按钮 C** 释放。

### 10.1 技能参数

| 技能 | 冷却 (网络帧) | 冷却 (秒) | 效果 |
|------|:---:|:---:|------|
| 自爆 (SELF-DESTRUCT) | 96 | 8 | 以自身为中心，360°溅射，半径 8 格 |
| 冲撞 (DASH) | 36 | 3 | 向移动方向瞬移 20 格 |
| 盾 (SHIELD) | 120 | 10 | 获得 100 临时 HP，移速 +60% |

### 10.2 自爆 (SKILL_SELFDESTRUCT)

```
调用 castRadialRays(player.posX, player.posY, SELFDESTRUCT_SPLASH=8, CLR_ALLY, SUB_DMG_SELFDESTRUCT=3)

  splashRadius = 8
  径向射线数 = 3×8 = 24 条（等角分布）
  每条径向子射线溅射半径 = 6 (SUB_BULLET_SPLASH)
  子伤害 = 3

效果：在玩家脚下产生大片圆形涂色区域
      如果敌方在范围内，造成 3 点伤害
```

### 10.3 冲撞 (SKILL_DASH)

```
限制：潜入时不可使用

逻辑：
  1. 向 moveAngle 方向计算 20 格的目标位置
  2. 边界钳位
  3. 分 20 步沿路径前进
  4. 每步检测墙壁碰撞
  5. 遇到墙壁立即停止（停在墙壁前）
  6. 传送到最终安全位置
```

### 10.4 盾 (SKILL_SHIELD)

```
限制：潜入时不可使用

逻辑：
  1. shieldHp = 100
  2. shielded = true
  3. 移速变为 96 px/s (SPD_SHIELD)

盾消耗：
  每网络帧 shieldHp 减 1（即每秒消耗 12 HP）
  shieldHp ≤ 0 → shielded = false

受伤处理：
  伤害优先由盾吸收：shieldHp -= min(damage, shieldHp)
  剩余伤害扣到 HP

与潜入互斥：
  - 盾激活时不能潜入（toggleStealth 直接返回）
  - 潜入时不能开盾（useShield 直接返回）
```

## 11. 潜入系统

### 11.1 触发条件

按 **按钮 A**（`toggleStealth`）：

```
开启条件：
  - 未处于盾状态 (shielded = false)
  - 玩家中心像素所在格子为己方颜色

关闭：
  - 再次按按钮 A（手动关闭）
  - 强制现形（见下文）
```

### 11.2 潜入效果

| 效果 | 值 |
|------|-----|
| 移速 | 120 px/s（×2.0） |
| HP 回复 | 12 HP/s（每网络帧 +1） |
| 敌方视角 | 己方角色**不可见**（不渲染） |
| 地图显示 | 色块无变化（仅在敌方视角高亮代码中预留） |

### 11.3 强制现形条件

- **受到伤害**：现形 + 固定 20 伤害（`DMG_STEALTH_HIT`），无视原有伤害值
- **踩到敌方色块**：每帧移动后检测中心像素，若在敌方色块上则 `stealth = false`
- **盾激活**：盾激活时无法维持潜入

### 11.4 伤害处理顺序

```
1. 如果 stealth → 现形，dmg = 20（固定）
2. 如果 shielded → 盾吸收 (min(dmg, shieldHp))
3. 剩余伤害 → hp -= dmg
4. hp < 0 → hp = 0
```

## 12. 网络协议

### 12.1 ESP-NOW 通信

- 点对点架构（无主从），双方平等
- 每台设备需硬编码对方的 MAC 地址
- MAC 地址较小的设备在上方出生，大的在下方
- 无加密，channel 使用默认值

### 12.2 网络帧结构

12 字节紧凑打包（`#pragma pack(1)`）：

| 字段 | 类型 | 位宽 | 说明 |
|------|------|------|------|
| frameId | uint8 | 8 | 0-23 循环编号 |
| posX | uint8 | 8 | 像素 X (0-239) |
| posY_half | uint8 | 8 | 像素 Y>>1 (0-159), LSB=2px精度 |
| flags | uint8 | 8 | 见下方分解 |
| joyX_lo | uint8 | 8 | 摇杆X低8位 |
| joyY_lo | uint8 | 8 | 摇杆Y低8位 |
| joy_hi | uint8 | 8 | [7:4]=joyX高4位, [3:0]=joyY高4位 |
| aimAngle | uint8 | 8 | 自瞄锁定角度 0-255 |
| fireDir | uint8 | 8 | 移动/发射方向 0-255 |
| wpnRange | uint8 | 8 | 武器射程（网格数） |
| moveSpeed | uint8 | 8 | 移动速度 px/s |
| reserved | uint8 | 8 | 预留 |

**flags 分解：**

| 位 | 字段 | 说明 |
|----|------|------|
| 0 | stealth | 是否潜入 |
| 1 | stealthTrigger | 潜入状态变化信号 |
| 2-4 | wpnId | 当前武器 (0-3) |
| 5-6 | skillId | 当前技能 (0-2) |
| 7 | firing | 战斗中=蓄力中；选择中=ready |

### 12.3 同步频率

- 网络帧率：**12fps**（每 2 渲染帧执行 1 次网络帧）
- 只有偶数渲染帧（`frameCount % 2 == 0`）执行网络更新
- 伤害判定在武器开火的同一网络帧完成

### 12.4 选择阶段的帧复用

选择阶段发送的帧：
- `wpnId`：所选武器编号
- `skillId`：所选技能编号
- `firing`（bit7）：作为本地 ready 标志
- 其他字段清零

接收方：
- `remoteFrame.firing` → `remoteReady`

## 13. 音频系统

### 13.1 技术方案

- **调制方式**：Sigma-Delta 调制（1-bit DAC）
- **输出引脚**：GPIO17
- **基频**：312.5kHz（ESP32-S3 固定）
- **采样率**：20kHz
- **定时器**：硬件定时器 0，80 分频（1MHz），每 50μs 触发 ISR

### 13.2 音效列表

| ID | 名称 | 波形 | 时长 | 频率/特点 |
|----|------|------|------|-----------|
| 0 | 手枪射击 | 方波+衰减 | 40ms | 800Hz 短促 |
| 1 | 霰弹枪射击 | 方波+衰减 | 80ms | 300Hz 低沉 |
| 2 | 狙击射击 | 频率扫描 | 100ms | 400→100Hz 下降 |
| 3 | 榴弹发射 | 频率扫描 | 60ms | 300→600Hz 上升 |
| 4 | 爆炸 | 白噪声+衰减 | 150ms | 噪声 |
| 5 | 命中 | 方波+衰减 | 30ms | 500Hz 短促 |
| 6 | 潜入开启 | 频率扫描 | 50ms | 200→600Hz 上升 |
| 7 | 潜入关闭 | 频率扫描 | 40ms | 600→200Hz 下降 |
| 8 | 冲撞 | 频率扫描 | 30ms | 100→1000Hz 快速上升 |
| 9 | 盾开启 | 方波+衰减 | 60ms | 200Hz 低频持续 |
| 10 | 盾破裂 | 噪声+衰减 | 75ms | 噪声（预留） |
| 11 | 自爆 | 白噪声+衰减 | 200ms | 最大声 |
| 12 | 死亡 | 频率扫描 | 125ms | 300→50Hz 低沉下降 |

### 13.3 音频生成算法

- **genSquareDecay**：方波 + 线性衰减包络
- **genNoiseDecay**：随机噪声 + 线性衰减（模拟爆炸）
- **genSweep**：频率扫描方波 + 线性衰减（上升/下降音调）

### 13.4 播放机制

- 非阻塞播放：`play(sfx)` 设置当前播放指针和音量，ISR 自动推进
- 可打断：新音效会立即中断正在播放的音效
- 最大样本数：4000（200ms @ 20kHz），所有数据存储在 PSRAM
- 总内存占用：13 × 4000 × 2 bytes ≈ 104KB (PSRAM)
- **关键**：`sigmaDeltaWrite(channel, duty)` 参数是通道号(0-7)，非引脚号。`sigmaDeltaSetup(pin, channel, freq)` 绑定引脚后，后续写入用 channel 编号。

## 14. 渲染系统

### 14.1 渲染分层

| 层序 | 内容 | 函数 |
|------|------|------|
| 1 | 背景（黑色填充） | `sprMap.fillSprite(TFT_BLACK)` |
| 2 | 地图网格 | `drawMap()` — 60×80 格，差分更新 |
| 3 | 指示线 | `drawIndicator()` — 移动方向 + 自瞄锥形 |
| 4 | 玩家 | `drawPlayer()` — 7×7 实体 + 光标 |
| 5 | 特效 | `drawEffects()` — 涂色闪烁动画 |
| 6 | UI 层 | `drawUI()` — 血条/计时器/冷却图标 |

最后一帧通过 DMA 推送到屏幕：`sprMap.pushSprite(0, 0)`

### 14.2 差分地图渲染

维护 `Map::data`（当前帧）和 `Map::prev`（上一帧快照）：
- 如果格子颜色与上帧相同 → 跳过，不重绘
- 如果格子颜色改变或有动画 → 重绘该格
- 每帧结束后 `Map::snapshot()` 将 data 复制到 prev

### 14.3 玩家渲染

```
圆形光标（默认）：
  drawCircle(px, py, r=6, color)          // 外框
  fillRect(px-3, py-3, 7, 7, color)       // 实体方块

方形光标（自瞄锁定）：
  drawRect(px-6, py-6, 12, 12, color)     // 方形外框
  fillRect(px-3, py-3, 7, 7, color)       // 实体方块

敌方潜入时：不渲染（stealth && isEnemy → return）
```

### 14.4 指示线

- 灰色主射线：从玩家中心沿移动方向画出
  - **普通/蓄力武器**：长度 = 武器射程 × 格数（蓄力型随 chargeLevel 伸长）
  - **投掷物（榴弹）**：长度 = 摇杆幅度 × 最大射程，实时反映实际落点
- 自瞄锥形线：主射线 ± aimAngle 两条斜线，长度为主射线的 3/4（仅 aimAngle>0 的武器）

### 14.5 各状态 UI

| 状态 | UI 内容 |
|------|---------|
| SELECT | 标题、4 武器选项（名称+描述）、3 技能选项、双方 ready 状态 |
| COUNTDOWN | 大号数字 3/2/1/"GO!" 居中显示 |
| PLAYING | 顶部计时器、右上血条、左上武器/技能名、左下 3 冷却图标 |
| OVER | WIN/LOSE、双方色块数、覆盖率百分比、重开提示 |

### 14.6 冷却图标

3 个技能冷却图标各 12×12 像素，垂直排列：
- 黄色：自爆冷却
- 青色：冲撞冷却
- 洋红：盾冷却

冷却剩余从底部向上填充灰色覆盖。

## 15. 性能优化

### 15.1 计算优化

| 技术 | 说明 |
|------|------|
| 正弦查表 | 256 条目 int16_t ×1000，cos 通过 sin 偏移 64 获得 |
| 0-255 角度制 | 所有角度用 uint8_t，避免浮点运算 |
| DDA 整数步进 | 射线遍历使用定点数（×1000），无 sqrt/除法 |
| 位运算 | 地图存取使用位移代替乘除 |
| 零动态分配 | 所有缓冲区编译期确定，全局数组或 PSRAM 预分配 |

### 15.2 渲染优化

| 技术 | 说明 |
|------|------|
| 差分地图渲染 | 只重绘变化的格子（比较 current vs prev） |
| DMA 推送 | `pushSprite` 使用 DMA 传输像素 |
| PSRAM 精灵 | `sprMap` 和 `sprUI` 在 PSRAM 创建，节省 SRAM |
| 动画去重 | 同一格子重复被击中时重置动画帧，避免重复条目 |

### 15.3 内存分配

| 数据 | 大小 | 位置 |
|------|------|------|
| Map::data + prev | 1200 × 2 = 2400 bytes | PSRAM |
| sprMap (240×320×16bit) | 153600 bytes | PSRAM |
| sprUI (240×320×16bit) | 153600 bytes | PSRAM |
| 音频采样 (13×4000×2bytes) | ~104KB | PSRAM |
| 动画列表 (128×3bytes) | 384 bytes | SRAM |
| sinTable (256×2bytes) | 512 bytes | SRAM |

总计 PSRAM 占用约 414KB（8MB PSRAM 中的 5%）。

### 15.4 帧预算 (41.7ms @ 24fps)

| 操作 | 预算 |
|------|------|
| 摇杆/按钮读取 | ~1ms |
| ESP-NOW 收发 | ~2ms |
| 游戏逻辑（移动/武器/技能/伤害） | ~5ms |
| 地图差分渲染 | ~10ms |
| 玩家/指示线/特效渲染 | ~15ms |
| UI 渲染 | ~5ms |
| DMA 推送 + 帧同步 | ~3.7ms |

## 16. 常量速查

### 16.1 屏幕

| 常量 | 值 |
|------|-----|
| SCR_W | 240 |
| SCR_H | 320 |
| GRID_SIZE | 4 |
| MAP_COLS | 60 |
| MAP_ROWS | 80 |
| MAP_CELLS | 4800 |
| MAP_BYTES | 1200 |

### 16.2 速度 (px/s)

| 常量 | 值 |
|------|-----|
| SPD_BASE | 60 |
| SPD_STEALTH | 120 |
| SPD_ENEMY_TILE | 54 |
| SPD_CHARGE_B | 48 |
| SPD_CHARGE_C | 36 |
| SPD_SHIELD | 96 |

### 16.3 战斗参数

| 常量 | 值 |
|------|-----|
| HP_INITIAL | 100 |
| HP_STEALTH_REGEN | 12/s (1HP/网络帧) |
| DMG_STEALTH_HIT | 20 |
| SHIELD_HP | 100 |
| SHIELD_DPS | 24/s |
| CHARGE_SPEED | 21/帧 |
| PLAYER_SIZE | 7 |
| INVINCIBILITY_FRAMES | 12 |

### 16.4 技能冷却 (网络帧)

| 常量 | 值 | 秒 |
|------|-----|-----|
| CD_SELFDESTRUCT | 96 | 8 |
| CD_DASH | 36 | 3 |
| CD_SHIELD | 120 | 10 |

### 16.5 比赛计时

| 常量 | 值 |
|------|-----|
| MATCH_DURATION | 1080 (90s) |
| COUNTDOWN_SECS | 3 |
| COUNTDOWN_FRAMES | 36 |

### 16.6 溅射参数

| 常量 | 值 |
|------|-----|
| SELFDESTRUCT_SPLASH | 8 |
| SUB_BULLET_SPLASH | 5 |
| SUB_DMG_SELFDESTRUCT | 12 |
| SUB_DMG_GRENADE | 8 |

### 16.7 代码文件结构

```
src/
├── main.cpp            # 入口（setup/loop），MAC 比较决定出生位置
├── config.h            # 所有常量定义
├── hardware.h/cpp      # 显示屏、摇杆、按钮、sin 表、角度函数
├── network.h/cpp       # ESP-NOW 通信、12 字节帧打包/解包
├── map.h/cpp           # 60×80 网格地图、2bit 压缩存储、墙壁布局
├── raycast.h/cpp       # DDA 射线、平行/径向射线组、动画系统
├── entity.h/cpp        # 玩家实体（移动/碰撞/HP/潜入/速度）
├── weapon.h/cpp        # 4 种武器（蓄力/发射/自瞄/冷却）
├── skill.h/cpp         # 3 种技能（自爆/冲撞/盾）
├── game.h/cpp          # 四状态游戏状态机
├── render.h/cpp        # 5 层渲染（地图/指示线/玩家/特效/UI）
└── audio.h/cpp         # Sigma-Delta 音频（13 种音效）
```

---

## 17. 搭建指南

本章介绍从零开始搭建硬件电路和配置软件的全流程。

### 17.1 所需元件

| 元件 | 数量 | 说明 |
|------|:---:|------|
| ESP32-S3 N16R8 开发板 | 2 | 16MB Flash + 8MB PSRAM（OPI） |
| ST7789 240×320 TFT 显示屏 | 2 | SPI 接口，7 针 |
| 双轴摇杆模块（XY） | 2 | 模拟输出 0-4095，带按钮 |
| 轻触按钮 ×4 | 2 | 6×6mm 微动开关（或面包板按钮） |
| 无源蜂鸣器 | 2 | 压电陶瓷片，GPIO 直驱（可选） |
| 面包板 + 跳线 | 2 | 或自制 PCB |
| 10KΩ 电阻 ×6 | 2 | 按钮上拉（若用 INPUT_PULLUP 可省略） |
| USB-C 数据线 | 2 | 烧录与供电 |

> **推荐购买**：ESP32-S3 开发板选带 TFT 屏幕插接座的版本，省去屏幕接线。

### 17.2 引脚定义速查

> **修改引脚时**：TFT 引脚改 [platformio.ini](platformio.ini) 的 `build_flags`（需 Clean + Rebuild）；按钮/摇杆/蜂鸣器改 [src/config.h](src/config.h#L44-L51)（普通 Rebuild 即可）。

| 功能 | 宏/常量 | 值 | 定义位置 |
|---|---|---|---|
| TFT 时钟 | `TFT_SCLK` | GPIO12 | [platformio.ini](platformio.ini) |
| TFT 主出从入 | `TFT_MOSI` | GPIO11 | [platformio.ini](platformio.ini) |
| TFT 主入从出 | `TFT_MISO` | GPIO13 | [platformio.ini](platformio.ini)（未接线，仅占位） |
| TFT 片选 | `TFT_CS` | GPIO10 | [platformio.ini](platformio.ini) |
| TFT 数据/命令 | `TFT_DC` | GPIO9 | [platformio.ini](platformio.ini) |
| TFT 复位 | `TFT_RST` | GPIO14 | [platformio.ini](platformio.ini) |
| TFT 背光 | `TFT_BL` | GPIO8 | [platformio.ini](platformio.ini) |
| 摇杆 X 轴 | `PIN_JOY_X` | GPIO1 | [src/config.h](src/config.h)（ADC1_CH0） |
| 摇杆 Y 轴 | `PIN_JOY_Y` | GPIO2 | [src/config.h](src/config.h)（ADC1_CH1） |
| 按钮 A（潜入） | `PIN_BTN_A` | GPIO4 | [src/config.h](src/config.h) |
| 按钮 B（攻击） | `PIN_BTN_B` | GPIO5 | [src/config.h](src/config.h) |
| 按钮 C（技能） | `PIN_BTN_C` | GPIO6 | [src/config.h](src/config.h) |
| 按钮 D（预留） | `PIN_BTN_D` | GPIO7 | [src/config.h](src/config.h) |
| 蜂鸣器 | `PIN_AUDIO` | GPIO17 | [src/config.h](src/config.h) |

### 17.3 电路接线

#### 17.3.1 TFT 显示屏 (SPI)

ST7789 7 针接口连接 ESP32-S3：

```
ST7789        ESP32-S3
------        --------
VCC    →      3.3V
GND    →      GND
SCL    →      GPIO12  (TFT_SCLK)
SDA    →      GPIO11  (TFT_MOSI)
RES    →      GPIO14  (TFT_RST)
DC     →      GPIO9   (TFT_DC)
CS     →      GPIO10  (TFT_CS)
BL     →      GPIO8   (TFT_BL，直接接 3.3V 也可)
```

> MISO 不需要连接（TFT 只写不读），板级定义中保留 GPIO13 占位。

#### 17.3.2 摇杆

```
摇杆模块       ESP32-S3
--------       --------
VCC     →      3.3V
GND     →      GND
VRx     →      GPIO1  (PIN_JOY_X, ADC1_CH0)
VRy     →      GPIO2  (PIN_JOY_Y, ADC1_CH1)
SW      →      (不使用，或任意 GPIO 作额外按钮)
```

> 摇杆输出 0-4095 模拟量，中心值约 2047。代码自动减去中心偏移并设置死区 (200)。

#### 17.3.3 按钮

四个轻触按钮，一端接 GPIO，另一端接 GND。使用内部上拉 (`INPUT_PULLUP`)，按下时读数为 0：

```
按钮        GPIO        功能
----        ----        ----
BTN_A       GPIO4       潜入（Stealth）
BTN_B       GPIO5       攻击（开火/蓄力/确认）
BTN_C       GPIO6       技能
BTN_D       GPIO7       预留（暂无功能）
```

> 若使用外部上拉：按钮一端接 GPIO，另一端接 GND；GPIO 与 3.3V 之间并联 10KΩ 电阻。

#### 17.3.4 蜂鸣器（可选）

```
蜂鸣器        ESP32-S3
------        --------
+      →      GPIO17  (PIN_AUDIO)
-      →      GND
```

> 音频使用 Sigma-Delta 调制 1-bit DAC，驱动压电蜂鸣器。蜂鸣器可省略，不影响游玩。

#### 17.2.5 完整接线速查表

| ESP32-S3 引脚 | 连接 |
|:---:|------|
| 3.3V | TFT VCC、摇杆 VCC、按钮上拉 |
| GND | TFT GND、摇杆 GND、按钮 GND、蜂鸣器- |
| GPIO1 | 摇杆 VRx (X 轴) |
| GPIO2 | 摇杆 VRy (Y 轴) |
| GPIO4 | 按钮 A（潜入） |
| GPIO5 | 按钮 B（攻击） |
| GPIO6 | 按钮 C（技能） |
| GPIO7 | 按钮 D（预留） |
| GPIO8 | TFT BL（背光） |
| GPIO9 | TFT DC |
| GPIO10 | TFT CS |
| GPIO11 | TFT SDA (MOSI) |
| GPIO12 | TFT SCL (SCLK) |
| GPIO13 | (MISO — 不需要连接) |
| GPIO14 | TFT RES (RST) |
| GPIO17 | 蜂鸣器+ |

### 17.3 安装 PlatformIO

1. 安装 [VS Code](https://code.visualstudio.com/)
2. 在扩展市场搜索 **PlatformIO IDE**，安装
3. 重启 VS Code，等待 PlatformIO 初始化完成

### 17.4 创建项目

```bash
# 用 PlatformIO Home 创建新项目，或命令行：
pio project init --board esp32-s3-devkitc-1 --project-dir ESP_game
```

将本项目 `src/` 目录下所有文件复制到项目的 `src/`，`platformio.ini` 替换项目根目录的配置文件。

> 本项目的 `platformio.ini` 已配置好 **PSRAM（OPI 模式）**、**16MB Flash 分区表**、**TFT_eSPI 引脚** 和 **SPI_FREQUENCY**，无需额外修改。

### 17.5 需修改的参数（两台设备不同）

#### 17.5.1 对方 MAC 地址（必须修改）

编辑 `src/main.cpp` 第 15 行：

```cpp
// 对方 MAC 地址（每台设备需修改为对方的 MAC）
static uint8_t peerMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
```

**如何获取 MAC 地址：**

1. 先写 `{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}` 占位，烧录一台设备
2. 打开串口监视器（115200 波特率）
3. 启动后设备会打印自身 MAC 地址：
   ```
   MAC: AA:BB:CC:DD:EE:01, spawn: TOP
   ```
4. 将此 MAC 地址填入另一台设备的 `peerMac` 中
5. 反之亦然

**两台设备配置示例：**

设备 A 的 `main.cpp`：
```cpp
static uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02}; // 设备 B 的 MAC
```

设备 B 的 `main.cpp`：
```cpp
static uint8_t peerMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}; // 设备 A 的 MAC
```

> **注意**：MAC 地址较小的设备会自动在上方出生（spawn: TOP），大的在下方（spawn: BOTTOM）。这个行为由 `memcmp` 比较决定，无需手动设置。

#### 17.5.2 GPIO 引脚（如需自定义）

编辑 `src/config.h` 第 45-51 行：

```cpp
constexpr uint8_t PIN_JOY_X  = 1;   // 摇杆 X 轴 ADC
constexpr uint8_t PIN_JOY_Y  = 2;   // 摇杆 Y 轴 ADC
constexpr uint8_t PIN_BTN_A  = 4;   // 按钮 A
constexpr uint8_t PIN_BTN_B  = 5;   // 按钮 B
constexpr uint8_t PIN_BTN_C  = 6;   // 按钮 C
constexpr uint8_t PIN_BTN_D  = 7;   // 按钮 D
constexpr uint8_t PIN_AUDIO  = 17;  // 蜂鸣器
```

如果实际接线不同，修改这些常量即可。确保不要与 TFT 的 SPI 引脚（8-14）冲突。

#### 17.5.3 TFT 引脚（如与默认不同）

编辑 `platformio.ini` 中 `build_flags` 部分的 TFT 引脚定义：

```ini
-D TFT_MISO=13
-D TFT_MOSI=11
-D TFT_SCLK=12
-D TFT_CS=10
-D TFT_DC=9
-D TFT_RST=14
-D TFT_BL=8
```

根据实际接线修改对应数字。注意 TFT_eSPI 需要 `TFT_MISO` 定义（即使不使用），保留任意有效 GPIO 即可。

### 17.6 可选参数调整（两台设备可以相同）

编辑 `src/config.h`，以下是可能需要根据手感调整的参数：

#### 游戏规则

| 参数 | 默认值 | 说明 |
|------|:---:|------|
| `HP_INITIAL` | 100 | 初始血量（上限 100） |
| `MATCH_DURATION` | 1080 | 比赛时长（网络帧, 1080÷12=90秒） |
| `COUNTDOWN_SECS` | 3 | 赛前倒计时秒数 |
| `RESPAWN_FRAMES` | 12 | 死亡复活时间（1秒） |
| `INVINCIBILITY_FRAMES` | 12 | 复活后无敌时间（1秒） |

#### 移动速度

| 参数 | 默认值 | 说明 |
|------|:---:|------|
| `SPD_BASE` | 60 | 基准移速 px/s |
| `SPD_STEALTH` | 120 | 潜入移速（×2.0） |
| `SPD_SHIELD` | 96 | 盾移速（×1.6） |
| `SPD_ENEMY_TILE` | 54 | 敌方色块移速（×0.9） |

#### 技能冷却

| 参数 | 默认值 | 说明 |
|------|:---:|------|
| `CD_SELFDESTRUCT` | 96 | 自爆冷却（网络帧, 96÷12=8秒） |
| `CD_DASH` | 36 | 冲撞冷却（36÷12=3秒） |
| `CD_SHIELD` | 120 | 盾冷却（120÷12=10秒） |

#### 武器参数

武器参数位于 `src/config.h` 的武器数值段（格式：`WPN_{P|G|S|SH}_<字段>`），`src/weapon.cpp` 的 `weaponDefs[]` 引用这些宏构建。每把武器的可调参数：

| 参数 | 类型 | 说明 |
|------|------|------|
| `damage` | int16_t | 基础伤害（每弹丸） |
| `fireRate` | uint8_t | 冷却（网络帧数） |
| `splashRadius` | uint8_t | 溅射半径（网格数） |
| `range` | uint8_t | 射程下限（网格数） |
| `rangeMax` | uint8_t | 射程上限（网格数，蓄力型使用） |
| `splashMin` | uint8_t | 溅射下限（仅蓄力型） |
| `splashMax` | uint8_t | 溅射上限（仅蓄力型） |
| `damageMax` | uint8_t | 蓄力最大伤害（非蓄力型 = damage） |
| `pellets` | uint8_t | 弹丸数（仅霰弹有效） |
| `aimAngle` | int8_t | 自瞄角度 ±值（0 = 无自瞄） |

### 17.7 板级配置说明 (platformio.ini)

```ini
; PSRAM 配置 — ESP32-S3 N16R8 必须使用 OPI
board_build.arduino.memory_type = qio_opi   # Flash + PSRAM 均为 OPI
board_build.psram_type = opi                # Octal PSRAM
board_build.flash_mode = qio                # Quad I/O Flash
board_upload.flash_size = 16MB              # 16MB Flash
board_build.partitions = default_16MB.csv   # 16MB 分区表

; 构建选项
-DBOARD_HAS_PSRAM          # 告知框架启用 PSRAM
-DCONFIG_SPIRAM_USE=1      # 使用 SPIRAM
-DSPI_FREQUENCY=80000000   # SPI 80MHz（ST7789 最高支持频率）
-DSMOOTH_FONT=1            # 启用平滑字体
-DCORE_DEBUG_LEVEL=0       # 关闭调试日志（减少串口输出）
```

> **如果使用 ESP32-S3 N8R2 (2MB PSRAM)**：将 `psram_type` 改为 `qspi`，`memory_type` 改为 `qio_qspi`。2MB PSRAM 足够运行本游戏（实际占用约 414KB）。

> **如果使用普通 ESP32 (无 PSRAM)**：删除所有 PSRAM 相关配置，并在 `hardware.cpp` 中将 `sprBuf[].createSprite()` 的 PSRAM 参数移除。但强烈不推荐——无 PSRAM 会导致内存不足。

### 17.8 已知问题与修复

#### ESP32-S3 FSPI 寄存器崩溃

**现象**：烧录后立即崩溃，串口输出 `Guru Meditation Error: Core 1 panic'ed (StoreProhibited)`。

**原因**：Arduino 框架将 ESP32-S3 的 `FSPI` 定义为 0，而 SDK 的 `REG_SPI_BASE(0)` 返回 0（无效基地址），导致 TFT_eSPI 写 SPI 寄存器时访问空指针。

**修复**：本项目已包含 `src/tft_fix.h`，通过 `-include src/tft_fix.h` 注入编译，在 Arduino SDK 定义 `REG_SPI_BASE` 后立即 `#undef`，使 TFT_eSPI 的正确版本生效。

> 不要从 `platformio.ini` 的 `build_flags` 中删除 `-include src/tft_fix.h`。

### 17.9 烧录与测试

```bash
# 编译（检查是否有错误）
pio run

# 烧录 + 打开串口监视器
pio run --target upload && pio device monitor --baud 115200

# 仅监视（不重新烧录）
pio device monitor --baud 115200
```

**首次启动检查清单：**

1. 屏幕是否点亮（黑色背景）
2. 串口是否打印 MAC 地址和 spawn 位置
3. 摇杆是否可以移动（选择界面上下左右切换选项）
4. 按钮 B 是否可以确认选择
5. 两台设备互相填入 MAC 后，是否可以同时进入倒计时
6. 蜂鸣器是否有声音反馈

> 若屏幕不亮：检查背光 BL 引脚是否接对，或直接给 BL 接 3.3V。
> 若摇杆不响应：检查 ADC 引脚是否正确（GPIO1/2 为 ESP32-S3 的 ADC1_CH0/CH1）。
> 若**摇杆方向错乱**（推上变左、推右变下等）：进入选择界面，按 A 键打开摇杆校准，按 2.4 节步骤完成校准。校准参数会自动保存，断电重启不丢失。
> 若两台设备无法配对：确认 MAC 地址填写正确，两台均使用 `pio run --target upload` 烧录最新固件。

### 17.10 双核架构说明

本游戏使用 ESP32-S3 双核并行工作：

| 核心 | 职责 | 说明 |
|------|------|------|
| Core 1 | 游戏主循环 | `loop()`: 输入读取 → 网络帧 → 游戏逻辑 |
| Core 0 | 纯渲染 | `renderTask()`: 等待信号 → `doRender()` → 发完成信号 |

**同步机制**（两个二进制信号量）：

```
Core 1 (loop):                    Core 0 (renderTask):
  ↓                                 ↓
  Give(renderStartSem) ──────────→ Take(renderStartSem)   // 触发渲染
  执行网络+逻辑...                  doRender()             // 画到后台缓冲
  ↓                                 ↓
  Take(renderDoneSem) ←─────────── Give(renderDoneSem)     // 渲染完成
  ↓                                 ↓
  submitDraw()  // DMA 推送上帧    (循环等待下一个 renderStartSem)
  Give(renderStartSem) ──────────→ ...
```

> Core 0 的渲染与 Core 1 的网络/逻辑完全重叠，有效利用了双核并行能力。双缓冲确保渲染期间不会出现画面撕裂。
