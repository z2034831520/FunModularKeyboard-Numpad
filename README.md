# FunModularKeyboard-Numpad — 独立宏键盘

> 这是 FunModularKeyboard 的独立数字小键盘版本，采用 ESP32-S3，保留可配置键位、屏幕、语音、USB/BLE HID 和 RGB 等功能。

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/framework-Arduino-blue)](https://www.arduino.cc/)

## 🔀 Fork 说明

本项目 Fork 自 [ZhiFun/FunModularKeyboard](https://github.com/ZhiFun/FunModularKeyboard)，当前仓库为 [z2034831520/FunModularKeyboard-Numpad](https://github.com/z2034831520/FunModularKeyboard-Numpad)。

当前定制开发分支为 `standalone-numpad`，基于上游提交 `06c5d73` 继续开发。

该 Fork 目前专注于主机数字小键盘的独立使用。原项目的扩展模块源码和硬件资料仍保留在仓库中，但默认固件不启用这些模块。

### 当前 Fork 的修改

| 修改项 | 说明 |
|--------|------|
| 禁用扩展模块 | 通过 `ENABLE_EXTENSION_MODULES=0` 关闭 ModA/ModB 的初始化、轮询和相关界面功能，使固件专注于主机数字小键盘。扩展模块代码未删除，后续仍可重新启用。 |
| 统一源码格式 | 对 `firmware/FunModularKeyboard/src` 下的 C/C++ 源码进行格式统一，不以改变原有功能为目的。 |
| 添加真实电量显示 | 新增 `BatteryMonitor`，使用 GPIO4 和主板现有的 R5/R10 分压电路采集电池电压；通过多次采样、平滑滤波和锂电池电压曲线估算剩余电量，并每 5 秒更新状态栏电池图标。 |

> **电量显示限制：** 当前百分比由电池端电压估算，不是库仑计精确计量。由于 TP4056 的充电状态引脚未连接到 ESP32-S3，固件暂时无法准确区分“充电中”和“已充满”。

---

<img width="2444" height="1373" alt="屏幕截图 2026-07-19 162423" src="https://github.com/user-attachments/assets/a6944f46-91a1-47f6-a329-ca03bb6be444" />

<img width="2791" height="1857" alt="屏幕截图 2026-07-18 232027" src="https://github.com/user-attachments/assets/8670e7e6-1b32-4f78-a43f-0e52d614776c" />

<img width="1178" height="1762" alt="屏幕截图 2026-07-18 231906" src="https://github.com/user-attachments/assets/400e814f-a2e0-40ae-8332-3e637d6bc0cb" />

<img width="2079" height="1385" alt="屏幕截图 2026-07-18 232058" src="https://github.com/user-attachments/assets/46842f9d-65c4-4469-bbd4-f5d500d36c73" />

<img width="1711" height="983" alt="屏幕截图 2026-07-19 162929" src="https://github.com/user-attachments/assets/fd741ef1-5618-45f0-b17b-afeae2ffd4d2" />

---

## 📖 项目概述

**FunModularKeyboard** 是一款高度可定制的宏键盘，由 **1 个主模块和 2 个磁吸扩展模块**组成。主模块通过磁吸 Pogo-Pin 接口与扩展模块进行 I²C 通信，并支持热插拔。

### 🧩 硬件架构

| 模块 | MCU | 说明 |
|--------|-----|-------------|
| **主模块** | ESP32-S3 | TFT LCD 屏幕、16 键矩阵、旋转编码器、扬声器、MEMS 麦克风、锂电池、RGB LED 和磁吸主机接口 |
| **A 模块（ModA）** | STM8S103F3 | 3 个旋转编码器和 2 个滑动电位器，通过磁吸接口进行 I²C 通信 |
| **B 模块（ModB）** | ESP32-S3 | 基于 [SmartKnob](https://github.com/scottbez1/smartknob)，集成无刷电机旋钮、应变片按压检测和 LED 灯环，通过磁吸接口进行 I²C 通信 |

```
┌─────────────────────────────────────────────┐
│              主模块 (ESP32-S3)              │
│  ┌─────┐  ┌──────────┐  ┌────────────────┐  │
│  │ TFT │  │ 16 键    │  │   旋转编码器   │  │
│  │ LCD │  │ 矩阵     │  │    + 按键      │  │
│  └─────┘  └──────────┘  └────────────────┘  │
│  ┌─────┐  ┌──────┐  ┌──────┐  ┌─────────┐  │
│  │麦克风│  │扬声器│  │ RGB  │  │  电池   │  │
│  └─────┘  └──────┘  └──────┘  └─────────┘  │
│         ┌──────┐  ┌──────┐                  │
│         │ USB  │  │ BLE  │    双模 HID     │
│         └──────┘  └──────┘                  │
│       ══════ 磁吸 Pogo-Pin 接口 ══════       │
│           I²C + 电源 + UART                 │
├──────────────────┬──────────────────────────┤
│  A 模块 (STM8)   │  B 模块 (ESP32-S3)       │
│  3× 旋转编码器  │  无刷电机旋钮 (FOC)       │
│  2× 滑块        │  应变片按压检测          │
│                  │  8× LED 灯环             │
└──────────────────┴──────────────────────────┘
```

---

## ✨ 功能特性

### ⌨️ 键盘

- **8 套可切换键位配置** — 通过旋转编码器随时切换，适配不同工作流程
- **宏组合键** — 按下一个键即可触发多键组合，例如使用 `Ctrl+Win+D` 显示桌面
- **多媒体控制** — 支持播放/暂停、音量调节和切换曲目等操作
- **USB HID + BLE 双模** — 支持有线和无线两种连接方式

### 🎤 语音

- **语音指令** — 长按 MIC 键后说出指令，通过百度语音识别触发预设操作，例如“打开 Spotify”
- **语音转文字** — 将语音输入转换为文字并发送到电脑
- 支持自定义关键词与脚本的绑定关系

### 💻 桌面配套应用

桌面配置工具通过 WiFi/TCP 与键盘通信，支持以下功能：

- 🗺️ **可视化键位编辑器** — 通过拖放配置按键功能
- ⚙️ **设备设置** — 配置 WiFi、RGB 效果、屏幕亮度、主题和音量等参数
- 📜 **脚本引擎** — 支持 Python 脚本，可一键启动应用或执行桌面自动化
- 🔄 **固件 OTA 升级** — 通过无线网络升级键盘固件
- 📊 **电脑状态监控** — 在键盘屏幕上实时显示 CPU 使用率和磁盘空间等信息
- 🎵 **音乐歌词同步** — 播放音乐时同步显示歌词
- 🏠 **HomeAssistant 集成** — 预留智能家居控制接口

### 🎨 显示与灯效

- **TFT LCD + LVGL** — 提供丰富的图形界面和多主题支持
- **16 颗 WS2812B RGB LED** — 支持彩虹、火焰、流星和脉冲等灯效
- **音乐频谱可视化** — 实时显示 FFT 音频频谱动画
- **状态栏** — 显示时钟、电量、连接状态和当前键位配置等信息

### 🧲 扩展模块

> 当前 `standalone-numpad` 分支已默认禁用扩展模块，以下为上游项目保留的硬件能力。

- **A 模块** — 配备 3 个编码器和 2 个滑块，适合调色、混音和视频编辑
- **B 模块** — 配备无刷电机旋钮（SimpleFOC）、触觉反馈和应变片按压检测
- 扩展模块通过磁吸接口实现**即插即用**和自动检测

---

## 📁 项目结构

```
FunModularKeyboard-Numpad/
├── firmware/                         # 固件源码
│   ├── FunModularKeyboard/           # 主模块固件（ESP32-S3、Arduino）
│   │   ├── src/                      # 核心源码
│   │   │   ├── main.cpp              # 程序入口
│   │   │   ├── MainTask.cpp/h        # 主任务调度
│   │   │   ├── DisplayTask.cpp/h     # LVGL 显示任务
│   │   │   ├── MatrixScanner.cpp/h   # 按键矩阵扫描
│   │   │   ├── RotaryEncoder.cpp/h   # 旋转编码器驱动
│   │   │   ├── RGBLightControl.cpp/h # RGB LED 灯效控制
│   │   │   ├── BatteryMonitor.cpp/h   # 电池电压采样与电量估算
│   │   │   ├── USBKeyboardImpl.cpp/h # USB HID 键盘实现
│   │   │   ├── BLEKeyboardImpl.cpp/h # BLE 键盘实现
│   │   │   ├── VoiceRecognizer.cpp/h # 百度 ASR 语音识别
│   │   │   ├── AudioAnalyzer.cpp/h   # FFT 音频频谱分析
│   │   │   ├── Mic.cpp/h             # MEMS 麦克风驱动
│   │   │   ├── Speaker.cpp/h         # 扬声器驱动
│   │   │   ├── SerialProtocol.cpp/h  # 上位机通信协议
│   │   │   ├── Configuration.cpp/h   # 配置文件管理
│   │   │   ├── Upgrade.cpp/h         # OTA 固件升级
│   │   │   ├── LogManager.cpp/h      # 日志系统
│   │   │   └── modules/              # I²C 主控与扩展模块
│   │   ├── data/                     # SPIFFS 数据文件
│   │   │   ├── config.ini            # 默认配置
│   │   │   └── keymap1~8.ini         # 8 套键位配置
│   │   └── platformio.ini            # PlatformIO 构建配置
│   ├── FunModularKeyboard_ModA/      # A 模块固件（STM8S103F3、IAR）
│   │   ├── main.c                    # 主程序
│   │   ├── ec11.c/h                  # EC11 编码器驱动
│   │   ├── adc.c/h                   # ADC 滑块采样
│   │   ├── i2c.c/h                   # I²C 从机通信
│   │   └── Uart.c/h                  # UART 调试输出
│   └── FunModularKeyboard_ModB/      # B 模块固件（ESP32-S3、PlatformIO）
│       └── firmware/src/             # 基于 SmartKnob 修改的固件
├── electronics/                      # PCB 设计（立创 EDA / EasyEDA）
│   ├── MainBoard/                    # 主板 PCB 工程
│   ├── ModA/                         # A 模块 PCB 工程
│   └── ModB/                         # B 模块 PCB 工程
├── cad/                              # 3D 机械结构文件
│   ├── Top.step / Bottom.step        # 主机外壳上下盖
│   ├── Plate.step                    # 轴体定位板
│   ├── Keycap.step / Keycap2.step    # 键帽
│   ├── FunModularKeyboard.3mf        # 3D 打印工程文件
│   ├── ModA/                         # A 模块外壳
│   └── ModB/                         # B 模块外壳
├── software/scripts/                 # 桌面自动化脚本
│   ├── desktop_automation_launch_apps_template.py  # 一键启动应用模板
│   └── desktop_automation_long_press_shutdown.py   # 长按关机脚本
├── tools/                            # 工具目录
└── README.md
```

---

## 🔧 技术栈

| 类别 | 技术 |
|----------|------------|
| **主控 MCU** | ESP32-S3（240 MHz、320 KB RAM、8 MB Flash） |
| **A 模块 MCU** | STM8S103F3 |
| **B 模块 MCU** | ESP32-S3 |
| **开发框架** | Arduino（PlatformIO）/ IAR（STM8） |
| **图形界面库** | LVGL 8.3 + TFT_eSPI |
| **键盘协议** | USB HID（TinyUSB）+ BLE HID |
| **语音识别** | 百度语音识别 API（REST） |
| **电机控制** | SimpleFOC 2.3 |
| **通信方式** | I²C（模块间）/ WiFi TCP（上位机） |
| **文件系统** | SPIFFS |
| **RGB 驱动** | FastLED（WS2812B） |
| **FFT 分析** | arduinoFFT |

### 主要库依赖

```
ESP32 BLE Keyboard    — BLE HID 键盘
FastLED               — RGB LED 控制
TFT_eSPI              — TFT 屏幕驱动
lvgl                  — 嵌入式图形界面框架
ESP32-audioI2S        — I²S 音频输入
SimpleIni             — INI 配置文件解析
ArduinoJson           — JSON 序列化
arduinoFFT            — 快速傅里叶变换
SimpleFOC             — FOC 电机控制
TLV493D               — 3D 磁场传感器
```

---

## 🚀 快速开始

### 主模块固件

1. 安装 [PlatformIO IDE](https://platformio.org/install)（VS Code 扩展）。

2. 克隆本 Fork 的 `standalone-numpad` 分支，然后进入主模块固件目录。

```bash
git clone --branch standalone-numpad https://github.com/z2034831520/FunModularKeyboard-Numpad.git
cd FunModularKeyboard-Numpad/firmware/FunModularKeyboard
```

3. 配置 WiFi 和百度语音 API 凭据（编辑 `data/config.ini` 或通过串口配置）。

4. 编译并烧录。

```bash
# 编译固件
pio run

# 烧录固件并上传 SPIFFS 数据
pio run --target upload
pio run --target uploadfs
```

### A 模块固件

使用 **IAR for STM8** 打开 `firmware/FunModularKeyboard_ModA/RGB_INIT.eww`，然后编译并烧录。

### B 模块固件

```bash
cd FunModularKeyboard-Numpad/firmware/FunModularKeyboard_ModB
pio run --target upload
```

> A/B 扩展模块固件由上游项目保留。当前独立数字小键盘模式不需要编译或烧录这两部分。

### 引脚参考

主模块的主要引脚分配如下，完整定义请参考 `platformio.ini` 和源码。

| 功能 | GPIO |
|----------|------|
| LED 数据 | 6 |
| LED 电源 | 36 |
| 编码器 CLK | 5 |
| 编码器 DT | 21 |
| 编码器按键 SW | 9 |
| 电池电压 ADC | 4 |
| I²C SDA | 15 |
| I²C SCL | 8 |
| 按键矩阵行 | 48, 10, 47, 33, 14 |
| 按键矩阵列 | 35, 34, 7, 13 |

---

## 🎮 使用指南

### 键位配置

1. 通过 WiFi 连接桌面配套应用。
2. 使用可视化拖放编辑器为每个按键分配功能，包括单键、组合键、宏和媒体控制等。
3. 最多可配置 8 套键位方案，长按旋转编码器即可切换。

### 语音功能

1. 在配套应用中配置百度 API Key 和 Secret Key。
2. 长按映射为 MIC 的按键开始录音。
3. 系统将识别结果与预设关键词匹配，并触发对应脚本。

### 固件升级

1. 配套应用通过 WiFi 将新固件推送到键盘。
2. 键盘自动重启进入 OTA 模式并完成升级。

---

## 🏗️ 硬件制作

- **PCB**：使用[立创 EDA](https://pro.lceda.cn/) 打开 `electronics/` 目录下的 `.epro2` 工程文件。
- **3D 外壳**：`cad/` 目录中提供 STEP 机械结构文件和可直接打印的 3MF 文件。
- **磁吸接口**：使用 Pogo-Pin 弹簧触点，用于模块间的 I²C 通信和供电。

---

## 📝 开发计划

- [ ] 开源桌面配套配置应用
- [ ] 接收社区贡献的扩展模块
- [ ] 完整集成 HomeAssistant
- [ ] 支持宏录制与回放
- [ ] 支持更多语音识别引擎
- [ ] 完善英文项目文档

---

## 🤝 参与贡献

欢迎提交 Issue 和 Pull Request。如果你设计了有趣的扩展模块，也欢迎在这里分享。

---

## 🙏 致谢

- [ZhiFun/FunModularKeyboard](https://github.com/ZhiFun/FunModularKeyboard) — 本 Fork 的上游项目
- [SmartKnob](https://github.com/scottbez1/smartknob) — 优秀的开源触觉反馈旋钮项目
- [LVGL](https://lvgl.io/) — 轻量级嵌入式图形界面库
- [PlatformIO](https://platformio.org/) — 跨平台嵌入式开发生态
- [百度语音识别](https://ai.baidu.com/tech/speech) — 中文语音识别服务

