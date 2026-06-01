# AIWearable - AI-Powered Wearable Device Firmware
[![视频标题](https://链接到你的缩略图图片.jpg)](https://github.com/zhou19830318/AIWearable/blob/main/AIWearable.mp4)

**AIWearable** 是一个基于 ESP32-S3 的开源可穿戴设备固件项目，整合了先进的 AI 语音交互能力、OpenClaw 网关集成，以及多种硬件支持。该项目使用 C 语言开发，基于 ESP-IDF 框架构建。

## 📋 目录

- [项目概述](#项目概述)
- [主要功能](#主要功能)
- [硬件支持](#硬件支持)
- [系统架构](#系统架构)
- [快速开始](#快速开始)
- [编译和部署](#编译和部署)
- [项目结构](#项目结构)
- [组件说明](#组件说明)
- [配置指南](#配置指南)
- [API 和使用](#api-和使用)
- [故障排查](#故障排查)
- [许可证](#许可证)

---

## 项目概述

**AIWearable** 是一个完整的可穿戴 AI 设备解决方案，旨在为各类硬件平台提供统一的、功能丰富的 AI 交互体验。该项目支持硬件板卡：Waveshare ESP32-S3 Audio Board。

### 主要特性

- 🎯 **多硬件支持**：针对不同 ESP32-S3 开发板的优化实现
- 🔊 **完整的语音交互**：集成 STT（语音转文本）、TTS（文本转语音）、Wake Word 唤醒词检测
- 🏠 **openclaw集成**：与 OpenClaw 网关无缝集成，支持硬件控制
- 🎨 **现代 UI**：基于 LVGL 的响应式用户界面，支持 GIF 动画和多语言（包括 RTL 文本）
- 🔐 **安全认证**：ED25519 加密认证机制
- 📸 **多媒体支持**：图像采集、MP3 播放、WebSocket 实时通信
- 🧠 **智能状态管理**：完整的应用状态机和事件系统
- ⚡ **高效内存管理**：PSRAM 支持、动态内存监控、频率缩放

---

## 主要功能

### 1. 语音处理系统
- **Wake Word Detection（唤醒词检测）**
  - 基于 ESP-SR WakeNet 引擎
  - 支持多个内置唤醒词：Hi ESP、Hey Jarvis、你好，小智等
  - 可配置的检测阈值（0-100）

- **Speech-to-Text（STT）**
  - 集成阿里云 DashScope API（百炼）
  - 支持实时语音流处理
  - 自动处理音频编码/解码

- **Text-to-Speech（TTS）**
  - 集成 Xiaomi MiMo 语音合成
  - 支持多种语音选择
  - 自动 MP3 播放

### 2. OpenClaw 智能家居集成
- WebSocket 连接到 OpenClaw 网关
- 双向通信：设备命令发送和服务器通知接收
- ED25519 认证支持
- 实时设备状态同步

### 3. 用户界面（UI）
- 基于 LVGL 图形库
- 支持多种显示屏类型（JD9853）
- 触摸屏支持（AXS5106L）
- GIF 动画状态指示
- 应用程序状态可视化

### 4. 音频系统
- 支持多种音频编解码器（ES8311、ES7210）
- PDM 和 I2S 接口支持
- MP3 播放器
- 实时音频流处理
- 双麦克风输入（部分硬件）

### 5. 系统管理
- WiFi 连接管理
- 设置管理（非易失性存储）
- 错误日志记录
- 内存监控和诊断
- WebServer 支持（HTTP 接口）
- 串行控制命令接口

---

## 硬件支持

### 支持的开发板

| 开发板 | MCU | 显示屏 | 音频 | 闪存 | 特性 |
|------|-----|-------|------|------|------|
| **Waveshare Audio** | ESP32-S3 | 无 | ES8311+ES7210 双麦 | 16MB | 纯音频方案 |


### 推荐规格
- **MCU**：ESP32-S3（双核，240MHz）
- **内存**：512KB SRAM + 8MB+ PSRAM
- **闪存**：8MB+ (16MB 推荐)
- **电源**：5V USB 或电池
- **音频**：双路麦克风输入、扬声器/耳机输出

---

## 系统架构

### 高层架构

```
┌─────────────────────────────────────────────────────┐
│                 Application Layer                    │
│  (app_main, app_state, app_tasks, voice_chat)      │
└─────────────────────────────────────────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│   UI Layer   │  │  Voice Chat  │  │   WiFi Mgr   │
│   (LVGL)     │  │   (STT/TTS)  │  │              │
└──────────────┘  └──────────────┘  └──────────────┘
        │                 │                 │
        └─────────────────┼─────────────────┘
                          ▼
        ┌─────────────────────────────────┐
        │    OpenClaw Gateway Interface   │
        │    (WebSocket, ED25519 Auth)    │
        └─────────────────────────────────┘
        ┌─────────────────────────────────┐
        │      Hardware Abstraction       │
        │  (Board, Audio, Camera, etc.)   │
        └─────────────────────────────────┘
        ┌─────────────────────────────────┐
        │         ESP-IDF / FreeRTOS      │
        └─────────────────────────────────┘
```

### 核心组件

| 组件 | 功能描述 | 依赖 |
|------|---------|------|
| **board** | 硬件板卡驱动（显示屏、触摸、LED 等） | LVGL, LCD 驱动 |
| **wifi_manager** | WiFi 连接和状态管理 | ESP WiFi 驱动 |
| **openclaw** | OpenClaw 网关通信 | WebSocket, 加密库 |
| **stt** | 语音识别（DashScope API）| HTTP Client, 音频处理 |
| **tts** | 语音合成（MiMo API）| HTTP Client, MP3 播放 |
| **wake_word** | 唤醒词检测（ESP-SR）| ESP-SR WakeNet 库 |
| **ui** | 用户界面渲染 | LVGL, Board |
| **mp3_player** | MP3 音乐播放 | 音频编解码库 |
| **camera** | 图像采集 | 相机驱动 |
| **settings** | 非易失性配置存储 | NVS Flash |
| **error_log** | 错误日志记录 | NVS Flash |
| **webserver** | HTTP 服务器 | HTTP Server |

---

## 快速开始

### 前置条件

1. **ESP-IDF v5.5 或更高版本**
   ```bash
   # 安装 ESP-IDF
   mkdir ~/esp
   cd ~/esp
   git clone -b v5.5 https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   source export.sh
   ```

2. **Python 3.8+** 和依赖工具
   ```bash
   pip install esptool
   ```

3. **CMake 3.16+** 和 ninja/make

### 克隆项目

```bash
git clone https://github.com/zhou19830318/AIWearable.git
cd AIWearable
```

### 配置秘密信息

1. **复制示例文件**
   ```bash
   cp secrets_example.txt secrets.txt
   ```

2. **编辑 `secrets.txt` 填入真实配置**
   ```bash
   # WiFi 配置
   WIFI_SSID=YourWiFiSSID
   WIFI_PASSWORD=YourWiFiPassword
   
   # OpenClaw 网关
   OC_HOST=192.168.1.100
   OC_PORT=18789
   OC_TOKEN=your_openclaw_token_here
   
   # 设备密钥（ED25519）
   DEVICE_KEY_HEX=<生成的 64 位十六进制字符串>
   
   # DashScope STT API
   DASHSCOPE_API_KEY=your_api_key
   STT_MODEL=fun-asr-realtime-2026-02-28
   
   # MiMo TTS API
   MIMO_API_KEY=your_api_key
   MIMO_ENDPOINT=https://api.xiaomimimo.com/v1/chat/completions
   ```

3. **生成设备密钥**（如果需要）
   ```bash
   python tools/test_openclaw_auth.py
   ```

---

## 编译和部署

### 编译指定硬件

#### 1. Waveshare ESP32-S3 Audio Board
```bash
bash build_audio_board.sh
```

#### 2. SenseCAP Watcher 或其他硬件
```bash
# 设置目标
idf.py set-target esp32s3

# 使用菜单配置
idf.py menuconfig
# 在 "AIWearable Application Configuration" 中选择目标板卡

# 编译
idf.py build
```

### 烧写固件

#### 自动检测串口
```bash
idf.py flash monitor
```

#### 手动指定串口
```bash
export ESPPORT=/dev/ttyUSB0  # Linux/Mac
# 或
set ESPPORT=COM3             # Windows

idf.py flash monitor
```

#### 使用 esptool 直接烧写
```bash
esptool.py -p /dev/ttyUSB0 write_flash \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/aiwearable.bin
```

### 查看运行日志

```bash
idf.py monitor                    # 实时日志
idf.py monitor -b 115200          # 指定波特率
```

---

## 项目结构

```
AIWearable/
├── CMakeLists.txt              # 顶级 CMake 配置
├── build_audio_board.sh         # Waveshare Audio 快速构建脚本
├── sdkconfig.defaults           # ESP-IDF 默认配置
├── partitions.csv              # Flash 分区表
├── secrets_example.txt         # 秘密配置示例
├── dependencies.lock           # 依赖管理
│
├── main/                        # 主应用程序
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild       # menuconfig 配置选项
│   ├── app_main.c              # 应用入口和初始化
│   ├── app_state.c             # 应用状态管理
│   ├── app_state_machine.c     # 有限状态机实现
│   ├── app_tasks.c             # 主要任务定义
│   ├── voice_chat.c            # 语音对话逻辑
│   ├── serial_cmd.c            # 串行命令解析
│   ├── mem_monitor.c           # 内存监控工具
│   └── include/                # 公共头文件
│
├── components/                 # 可复用组件库
│   ├── board/                  # 硬件驱动抽象层
│   ├── wifi_manager/           # WiFi 管理
│   ├── openclaw/               # OpenClaw 网关通信
│   ├── stt/                    # 语音识别（Speech-to-Text）
│   ├── tts/                    # 语音合成（Text-to-Speech）
│   ├── wake_word/              # 唤醒词检测
│   ├── ui/                     # 用户界面（LVGL）
│   ├── mp3_player/             # MP3 音乐播放
│   ├── camera/                 # 摄像头支持
│   ├── settings/               # 配置存储
│   ├── error_log/              # 错误日志
│   ├── webserver/              # HTTP 服务器
│   ├── notes_manager/          # 笔记管理
│   ├── ed25519_lib/            # ED25519 加密库
│   ├── esp_audio_codec/        # 音频编解码
│   ├── esp_audio_simple_player/# 简单音频播放器
│   ├── gmf_core/               # GMF 核心库
│   ├── gmf_audio/              # GMF 音频模块
│   ├── gmf_io/                 # GMF IO 模块
│   └── sscma_client/           # SSCMA 客户端
│
├── docs/                        # 文档目录
├── PRD/                         # 产品需求文档
└── tools/                       # 工具和脚本
    └── test_openclaw_auth.py   # OpenClaw 认证测试工具
```

---

## 组件说明

### 1. **board** - 硬件抽象层
提供统一的硬件接口，支持多种开发板：
- 显示屏驱动（JD9853）
- 触摸屏支持
- LED 灯条控制（RGB 环）
- SD 卡接口
- GPIO 和中断管理

### 2. **wifi_manager** - WiFi 管理
- 自动连接和重连逻辑
- WiFi 事件处理
- 连接状态回调
- SSID/密码管理

### 3. **openclaw** - OpenClaw 网关通信
- WebSocket 客户端实现
- ED25519 认证
- 双向消息通信
- 状态同步和控制命令
- SSL/TLS 支持

### 4. **stt** - 语音识别
- DashScope API 集成（阿里云）
- 实时音频流处理
- 自动噪音处理
- 语言模型选择（中文、英文等）

### 5. **tts** - 语音合成
- MiMo 语音合成 API
- 多语言支持
- MP3 格式输出
- 自动播放集成

### 6. **wake_word** - 唤醒词检测
- ESP-SR WakeNet 引擎
- 低功耗处理
- 可配置灵敏度
- 支持多个唤醒词模型

### 7. **ui** - 用户界面
- LVGL 图形库
- 响应式布局
- GIF 动画支持
- 多语言（包括 RTL）
- 触摸事件处理

### 8. **mp3_player** - MP3 播放
- MP3 解码和播放
- 音量控制
- 播放状态反馈
- 队列管理（可选）

### 9. **camera** - 图像采集
- OV2640/OV5640 支持
- JPEG 编码
- 实时预览
- 照片捕获

### 10. **settings** - 配置存储
- NVS（Non-Volatile Storage）基础
- 键值对存储
- 自动加载/保存
- 类型安全的 API

---

## 配置指南

### 通过 menuconfig 配置

```bash
idf.py menuconfig
```

#### AIWearable Application Configuration（主菜单）

```
├─ Select target board
│  ├─ Waveshare ESP32-S3 Audio Board
│
├─ Wake word detection engine
│  ├─ WakeNet (short wake phrase) [推荐]
│  └─ MultiNet (custom speech command)
│
├─ Custom wake word phrase (MultiNet only)
│
├─ Wake word detection threshold (0-100)
│  └─ 默认: 45
│
├─ OpenClaw Gateway host
│  └─ 默认: 192.168.1.100
│
└─ OpenClaw Gateway port
   └─ 默认: 18789
```

#### 其他重要配置

```
Component config
├─ ESP HTTP Client
│  └─ Enable HTTPS [推荐启用]
│
├─ mbedTLS
│  ├─ External memory allocation [启用]
│  ├─ Dynamic buffer [启用]
│  └─ Hardware AES [禁用 - 内存限制]
│
├─ LVGL
│  ├─ Enable GIF support [启用]
│  ├─ BiDi support (RTL) [启用]
│  └─ Fonts (Montserrat 12/14/16/18/20/28/36/48)
│
├─ FreeRTOS
│  ├─ Timer task stack: 4096
│  └─ Main task stack: 16384
│
└─ WiFi
   ├─ Max sockets: 16
   └─ Buffer optimization
```

### 编译时配置

在 `sdkconfig.defaults` 中预设默认值：

```cmake
# 闪存配置
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y

# PSRAM 配置
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# 功率管理
CONFIG_PM_ENABLE=y  # CPU 频率缩放

# 日志级别
CONFIG_LOG_DEFAULT_LEVEL_INFO=y

# 控制台
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

---

## API 和使用

### 应用状态管理

```c
// 获取当前状态
app_state_t current_state = app_get_state();

// 设置应用状态
void app_set_state(app_state_t new_state);

// 注册状态改变回调
void app_register_state_callback(app_state_callback_t callback);
```

### 语音对话 API

```c
// 启动语音识别
void voice_chat_start_listening(void);

// 停止语音识别
void voice_chat_stop_listening(void);

// 发送 TTS 文本
void voice_chat_speak(const char *text);

// 设置语音回调
void voice_chat_set_callback(voice_chat_callback_t callback);
```

### OpenClaw 通信

```c
// 连接到 OpenClaw 网关
esp_err_t openclaw_connect(const char *host, int port, const char *token);

// 发送命令
esp_err_t openclaw_send_command(const char *json_command);

// 断开连接
void openclaw_disconnect(void);

// 状态回调
void openclaw_set_state_callback(openclaw_state_callback_t callback);
```

### WiFi 管理

```c
// 启动 WiFi 连接
esp_err_t wifi_manager_start(const char *ssid, const char *password);

// 获取连接状态
wifi_state_t wifi_manager_get_state(void);

// 状态回调
void wifi_manager_register_callback(wifi_state_callback_t callback);
```

### UI 控制

```c
// 获取当前 UI 状态
ui_state_t ui_get_state(void);

// 设置 UI 状态
void ui_set_state(ui_state_t state);

// 显示通知
void ui_show_notification(const char *text, uint32_t duration_ms);
```

### 设置存储

```c
// 读取字符串配置
char value[256];
settings_read_string("key_name", value, sizeof(value), "default");

// 写入字符串配置
settings_write_string("key_name", "value");

// 读取整数配置
int int_value = settings_read_int("int_key", 42);

// 写入整数配置
settings_write_int("int_key", 100);
```

---

## 故障排查

### 编译问题

#### 错误：`idf_component_register not found`
**解决方案**：确保使用 ESP-IDF v5.5+
```bash
idf.py --version
# 应显示 v5.5 或更高版本
```

#### 错误：`PSRAM not initialized`
**解决方案**：检查 sdkconfig：
```bash
grep CONFIG_SPIRAM sdkconfig
# 应输出: CONFIG_SPIRAM=y
```

### 运行问题

#### 症状：应用崩溃，显示 "tlsf.c:630" 错误
**原因**：AES 硬件加速内存溢出
**解决方案**：已在 `sdkconfig.defaults` 中禁用
```c
CONFIG_MBEDTLS_HARDWARE_AES=n  // 使用软件 AES
```

#### 症状：WiFi 连接失败
**检查项**：
1. WiFi SSID 和密码正确
2. 设备与网关距离
3. 查看日志：`idf.py monitor`

#### 症状：语音识别不工作
**检查项**：
1. DashScope API 密钥有效
2. 麦克风硬件正常工作
3. 检查音频输入设备
4. 查看日志中的 STT 错误

#### 症状：OpenClaw 连接断开
**检查项**：
1. OpenClaw 网关在线
2. ED25519 认证密钥正确
3. 网络连接正常
4. 防火墙允许 WebSocket 18789 端口

### 性能优化

#### 减少内存占用
```bash
# 禁用 MultiNet（不需要时）
CONFIG_SR_MN_CN_NONE=y
CONFIG_SR_MN_EN_NONE=y

# 减少日志级别
CONFIG_LOG_DEFAULT_LEVEL_WARN=y

# 禁用 WiFi 缓冲优化（如果不需要）
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
```

#### 提高性能
```bash
# 启用编译器优化
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# 增加任务堆栈（如果有足够 RAM）
CONFIG_ESP_MAIN_TASK_STACK_SIZE=20480

# 启用 CPU 频率缩放（功耗优化）
CONFIG_PM_ENABLE=y
```

### 调试技巧

#### 启用详细日志
```bash
idf.py reconfigure
# 在 menuconfig 中：
# Logging → Default log verbosity → Verbose
# Component config → Log output → All components
```

#### 使用 GDB 调试
```bash
idf.py gdb
```

#### 查看内存使用情况
```bash
# 在代码中调用
mem_monitor_print_stats();

# 或通过串行命令
mem info
```

#### 性能分析
```bash
# 启用堆跟踪
CONFIG_HEAP_TRACING=y

# 生成统计
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
```

---

## 许可证

本项目采用 [MIT 许可证](LICENSE) - 详见 LICENSE 文件

---

## 贡献指南

欢迎提交 Issue 和 Pull Request！

### 报告 Bug
1. 使用 [GitHub Issues](https://github.com/zhou19830318/AIWearable/issues)
2. 详细描述问题和复现步骤
3. 包含完整的日志输出

### 提交改进
1. Fork 项目
2. 创建功能分支：`git checkout -b feature/amazing-feature`
3. 提交更改：`git commit -m 'Add amazing feature'`
4. 推送到分支：`git push origin feature/amazing-feature`
5. 开启 Pull Request

---

## 相关资源

- 📚 **ESP-IDF 文档**：https://docs.espressif.com/projects/esp-idf/
- 🎨 **LVGL 文档**：https://docs.lvgl.io/
- 🔊 **ESP-SR 文档**：https://github.com/espressif/esp-sr
- 🏠 **OpenClaw**：支持智能家居网关
- ☁️ **DashScope**：https://dashscope.aliyuncs.com/
- 🤖 **MiMo TTS**：Xiaomi 语音合成服务

---

## 常见问题 (FAQ)

### Q: 支持哪些语言？
**A:** 当前支持中文和英文。可通过配置 STT/TTS 模型切换语言。

### Q: 可以离线使用吗？
**A:** 目前需要网络连接（STT/TTS API）。未来版本计划支持本地模型。

### Q: 电池续航如何？
**A:** 依赖于硬件和使用场景。启用 CPU 频率缩放和 WiFi 休眠可显著改善续航。

### Q: 如何自定义唤醒词？
**A:** 
- 使用 WakeNet 引擎：选择预定义的唤醒词模型
- 使用 MultiNet：配置自定义唤醒词短语（3+ 词）

### Q: 支持自定义 OpenClaw 命令吗？
**A:** 是的，可以通过 `openclaw_send_command()` API 发送自定义 JSON 命令。

### Q: 如何添加新的硬件支持？
**A:** 
1. 在 `components/board/` 中创建新的硬件驱动
2. 在 `main/Kconfig.projbuild` 中添加板卡选项
3. 为新硬件创建特定的编译脚本

---

## 技术支持

- 📧 **GitHub Issues**：https://github.com/zhou19830318/AIWearable/issues
- 💬 **讨论区**：https://github.com/zhou19830318/AIWearable/discussions
- 📝 **Wiki**：https://github.com/zhou19830318/AIWearable/wiki

---

**最后更新**：2026-05-29  
**版本**：1.0.0-beta  
**作者**：zhou19830318
