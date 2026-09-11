# Modern C++ Batch FLAC-to-MP3 Transcoder & Tagger

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support)
[![Qt](https://img.shields.io/badge/Qt-6.x-green.svg)](https://www.qt.io/)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-7.x-red.svg)](https://ffmpeg.org/)
[![TagLib](https://img.shields.io/badge/TagLib-2.x-orange.svg)](https://taglib.org/)

高性能、跨平台批量 FLAC 音频转码与元数据/专辑封面标签继承工具。采用 **C++20** 构建，基于 Multi-Agent 生产者-消费者工作线程池与事件驱动状态机，提供现代 **Qt6 桌面 GUI** 与 **Headless CLI** 命令行双模式。

---

## 🌟 核心特性 (Key Features)

- **Multi-Agent 异步并发架构**：
  - **Scanner Agent**：基于 `std::filesystem` 递归扫描目录，捕获权限异常；强校验文件头 4 字节 **Magic Number** (`0x66 0x4C 0x61 0x43` / `"fLaC"`) 剔除伪造扩展名文件。
  - **Transcoder Agent**：原生 **FFmpeg C API** (`libavformat`, `libavcodec`, `libswresample`, `libavutil`) 解码、重采样（规范化为 44.1kHz 双声道）与 LAME MP3 压制。
  - **Metadata Agent**：基于 **TagLib** 继承原 FLAC 的 Vorbis Comments 标签与 Picture Block 封面图片，无损写入目标 MP3 的 ID3v2 标签与 `APIC` 封面帧。
  - **Coordinator**：`IDLE` -> `SCANNING` -> `READY` -> `TRANSCODING` -> `COMPLETED` 状态机与 `std::jthread` 线程池调度。
- **100% 内存安全**：C 风格 FFmpeg 句柄、上下文与帧全生命周期由 RAII 智能指针重载删除器自动管理，零内存泄漏。
- **双运行模式 (Dual Mode)**：
  - **Modern Qt6 GUI**：深色调精致界面，支持实时表格视图、单曲与全局双进度条、并发线程数与比特率（CBR 320k/256k/192k/VBR V0）选择。
  - **Headless CLI**：无界面服务器环境批量调用，支持命令行进度输出。

---

## 🏗️ 架构设计 (Architecture)

```text
                     ┌───────────────────────────────────┐
                     │   Coordinator / App Loop Agent    │
                     │       (Qt6 / CLI Mode Switch)     │
                     └─────────────────┬─────────────────┘
                                       │
        ┌──────────────────────────────┼──────────────────────────────┐
        ▼                              ▼                              ▼
┌────────────────────┐      ┌────────────────────┐      ┌────────────────────┐
│   Scanner Agent    │      │  Transcoder Agent  │      │   Metadata Agent   │
│ (std::filesystem)  │──队列─▶│  (FFmpeg C APIs)   │◀─回调─│  (TagLib / ID3v2)  │
└────────────────────┘      └────────────────────┘      └────────────────────┘
```

---

## ⚙️ 环境依赖 (Prerequisites)

- **编译器**：支持 C++20 的 C++ 编译器（GCC 10+ / Clang 12+ / MSVC 2019+）
- **构建工具**：CMake >= 3.20
- **依赖库**：
  - Qt6 (`Qt6Widgets`)
  - FFmpeg (`libavformat`, `libavcodec`, `libswresample`, `libavutil`)
  - TagLib (`taglib >= 2.0`)

### Linux (Arch / Manjaro)
```bash
sudo pacman -S cmake gcc qt6-base ffmpeg taglib pkgconf
```

### Linux (Ubuntu 22.04+ / Debian)
```bash
sudo apt update
sudo apt install cmake g++ qt6-base-dev libavcodec-dev libavformat-dev libswresample-dev libavutil-dev libtag1-dev pkg-config
```

---

## 🛠️ 构建指南 (Building)

```bash
# 1. 克隆仓库
git clone https://github.com/LKDenchin/flac-to-mp3.git
cd flac-to-mp3

# 2. 创建并进入 build 目录
mkdir -p build && cd build

# 3. CMake 配置与编译
cmake ..
make -j$(nproc)
```

---

## 🚀 使用说明 (Usage)

### 1. 图形界面模式 (GUI Mode)

在桌面环境中直接启动编译生成的可执行程序：

```bash
./build/FlacToMp3Converter
```

- 选择包含 `.flac` 文件的源目录与目标保存目录。
- 选择比特率配置（如 CBR 320 kbps 或 VBR V0）及并发线程数。
- 点击 **🔍 Scan FLAC Files** 扫描，随后点击 **⚡ Start Transcoding** 开启批量压制。

### 2. 命令行模式 (CLI Mode)

在 Headless 服务器或自动化脚本中使用 `--cli` 参数：

```bash
# 基本用法
./build/FlacToMp3Converter --cli -i /path/to/flac/folder -o /path/to/mp3/folder

# 指定比特率与并发线程数
./build/FlacToMp3Converter --cli -i /music/flac -o /music/mp3 -b 320k -t 8
```

#### CLI 参数列表

| 参数 | 长参数 | 说明 | 默认值 |
| :--- | :--- | :--- | :--- |
| `--cli` | `--cli` | 启用命令行 Headless 模式 | GUI 模式 |
| `-i` | `--input` | 源 FLAC 文件夹路径 | 必填 |
| `-o` | `--output` | 目标 MP3 文件夹路径 | `<source>_mp3` |
| `-b` | `--bitrate` | 目标码率 (`320k`, `256k`, `192k`, `v0`) | `320k` |
| `-t` | `--threads` | 并发转码线程数 | CPU 逻辑核心数 - 1 |
| `-h` | `--help` | 显示帮助说明 | - |

---

## 📂 项目工程结构 (Directory Structure)

```text
flac-to-mp3/
├── CMakeLists.txt              # CMake 构建配置
├── LICENSE                     # GNU General Public License v3.0
├── README.md                   # 项目说明文档
├── include/
│   ├── coordinator.hpp         # 工作流调度器与状态机
│   ├── scanner_agent.hpp       # 目录递归扫描与 Magic Number 校验
│   ├── transcoder_agent.hpp    # FFmpeg C API 音频转码引擎与 RAII
│   ├── metadata_agent.hpp      # TagLib 元数据提取与 ID3v2/APIC 写入
│   ├── safe_queue.hpp          # 线程安全阻塞队列
│   ├── thread_pool.hpp         # C++20 std::jthread 动态线程池
│   └── task_models.hpp         # 核心数据结构与契约
├── src/
│   ├── main.cpp                # 规范入口 (GUI / CLI 双模式分支)
│   ├── coordinator.cpp
│   ├── scanner_agent.cpp
│   ├── transcoder_agent.cpp
│   ├── metadata_agent.cpp
│   └── thread_pool.cpp
├── ui/
│   ├── main_window.hpp         # Qt6 主窗口声明
│   └── main_window.cpp         # 现代深色 UI 布局与信号槽绑定
└── tools/
    └── create_test_flac.py     # 测试音轨与封面合成脚本
```

---

## 📄 开源协议 (License)

本项目基于 **[GNU General Public License v3.0 (GPL-3.0)](LICENSE)** 协议开源。
