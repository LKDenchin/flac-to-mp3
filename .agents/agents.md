# AGENTS.md: Modern C++ Batch FLAC-to-MP3 Transcoder & Tagger

本文档定义了使用 **C++20** 构建高性能、跨平台批量音频转码与元数据补全工具的 Multi-Agent 开发规范与系统架构，供开发人员及代码生成智能体遵循。

---

## 1. 架构总览与拓扑设计

系统采用**生产者-工作线程池（Producer-Consumer Worker Pool）**与**事件驱动状态机**设计，各模块解耦并通过强类型数据契约与线程安全总线交互。

```
                     ┌───────────────────────────────────┐
                     │   Coordinator / App Loop Agent    │
                     │       (Qt6 / Dear ImGui / CLI)    │
                     └─────────────────┬─────────────────┘
                                       │
        ┌──────────────────────────────┼──────────────────────────────┐
        ▼                              ▼                              ▼
┌────────────────────┐      ┌────────────────────┐      ┌────────────────────┐
│   Scanner Agent    │      │  Transcoder Agent  │      │   Metadata Agent   │
│ (std::filesystem)  │──队列─▶│  (FFmpeg C APIs)   │◀─回调─│  (TagLib / Libcurl)│
└────────────────────┘      └────────────────────┘      └────────────────────┘

```

### 系统关键指标

* **并发模型**：基于 `std::jthread` 的无锁/自旋保护任务队列。
* **内存安全**：全生命周期 RAII 封装 C 风格资源（FFmpeg 上下文、文件句柄、TagLib 资源池）。
* **UI 响应**：计算与 I/O 100% 运行于后台线程，确保主界面维持稳定 60 FPS 刷新率。

---

## 2. Agent 角色与接口规约

### 2.1 Scanner Agent (文件扫描与校验)

* **核心职责**：非阻塞递归遍历源目录，快速过滤并验证 FLAC 容器合法性。
* **核心技术**：`std::filesystem`、二进制文件流读取。
* **执行约束**：
1. 采用 `std::filesystem::recursive_directory_iterator` 深度遍历，捕获并安全忽略 `std::filesystem_error`（处理权限不足目录）。
2. 扩展名过滤：大小写不敏感匹配 `.flac`。
3. **魔数校验（Magic Number）**：读取文件头前 4 字节，必须匹配十六进制 `0x66 0x4C 0x61 0x43`（ASCII `"fLaC"`）。丢弃重命名伪装文件。
4. 快速抽取基本属性（文件字节大小、最后修改时间），不直接在主线程中执行耗时的全文件元数据解析。


* **输出事件**：`FILE_FOUND(TaskItem)`、`SCAN_FINISHED(uint32_t total_count)`。

### 2.2 Transcoder Agent (FFmpeg 原生转码引擎)

* **核心职责**：调用 FFmpeg 原生 C API 解码 FLAC PCM 数据并将其重编码为 MP3。
* **核心依赖**：`libavformat`、`libavcodec`、`libswresample`、`libavutil`。
* **处理管线**：
```text
[FLAC Container]
       │ (avformat_open_input)
       ▼
[FLAC Decoder: libavcodec] ──▶ Raw AVFrame (PCM / FLTP)
                                        │
                                        ▼
                             [SwrContext (Resampler)]
                               (重采样: 规范为 44100Hz, S16P, 2声道)
                                        │
                                        ▼
[MP3 Encoder: libmp3lame]  ◀── Packed AVFrame (S16P / FLTP)
       │ (avcodec_send_frame / receive_packet)
       ▼
[MP3 Container] (avformat_write_header / av_interleaved_write_frame)

```


* **并发控制与资源隔离**：
1. 线程池工作线程数：默认设置为 `std::max(1u, std::thread::hardware_concurrency() - 1)`。
2. **完全资源隔离**：各 Worker 线程独立持有自己的 `AVFormatContext`、`AVCodecContext` 与 `SwrContext` 实例，严禁跨线程复用。
3. **RAII 自定义删除器**：
```cpp
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, decltype(&avformat_close_input)>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, decltype(&avcodec_free_context)>;
using AVFramePtr = std::unique_ptr<AVFrame, decltype(&av_frame_free)>;
using AVPacketPtr = std::unique_ptr<AVPacket, decltype(&av_packet_free)>;

```




* **进度计算**：以 `AVPacket::pts` 结合 `AVStream::duration` 换算实时完成比例，周期性回调刷新 UI。

### 2.3 Metadata Agent (元数据读写与云端富化)

* **核心职责**：转码过程中的元数据无损迁移及云端补全。
* **核心依赖**：`TagLib`（本地读写）、`libchromaprint`（声学指纹）、`cpr` / `libcurl`（网络查询）。
* **执行阶段**：
* **Phase 1 (原生迁移 - 本地必备)**：
1. 从源 FLAC Vorbis Comment 提取 `TITLE`、`ARTIST`、`ALBUM`、`GENRE`、`TRACKNUMBER`。
2. 读取 Vorbis Picture Block 提取嵌入封面二进制流。
3. MP3 转码结束后，使用 TagLib 写入目标文件的 ID3v2.3 / ID3v2.4 标签，并注入 `APIC` 封面帧。


* **Phase 2 (云端指纹补全 - 可选扩展)**：
1. 对解码阶段提取的前 120 秒 PCM 采样流计算 `chromaprint` 指纹。
2. 异步请求 AcoustID / MusicBrainz 开放 REST API 获取发行元数据。
3. 查询失败或超时设置 3 秒限制，静默回退至文件名回退解析规则，不得阻塞主转码链路。





### 2.4 Coordinator & UI Agent (全局调度与交互)

* **核心职责**：管理全局任务流水线与状态转移。
* **状态机模型**：
* `IDLE`：就绪等待用户输入源目录与目标目录。
* `SCANNING`：锁定目录选择器，流式追加任务列表。
* `READY`：扫描完成，支持勾选过滤、码率配置（CBR 320k / VBR V0）。
* `TRANSCODING`：触发多线程转码，更新双重进度条（单曲进度 + 整体进度）。
* `COMPLETED`：展示任务结算面板（成功数、失败数、总耗时），释放资源。



---

## 3. 核心数据契约 (C++20 Header)

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <functional>

enum class TaskStatus {
    Queued,
    Scanning,
    Converting,
    Completed,
    Failed,
    Skipped
};

enum class BitrateProfile {
    CBR_320K,
    CBR_256K,
    CBR_192K,
    VBR_V0
};

struct AudioMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    uint32_t track_number{0};
    std::vector<uint8_t> cover_image_bytes;
    std::string cover_mime_type;
};

struct TranscodeTask {
    std::string task_id;
    std::filesystem::path source_path;
    std::filesystem::path target_path;
    uintmax_t file_size_bytes{0};
    double duration_seconds{0.0};
    
    TaskStatus status{TaskStatus::Queued};
    float progress{0.0f}; // 0.0f - 100.0f
    
    AudioMetadata metadata;
    std::optional<std::string> error_message{std::nullopt};
};

using TaskProgressCallback = std::function<void(const std::string& task_id, float progress)>;
using TaskCompletionCallback = std::function<void(const std::string& task_id, bool success, const std::string& err)>;

```

---

## 4. 线程安全队列与并发模型设计

```cpp
#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class SafeTaskQueue {
public:
    SafeTaskQueue() = default;
    ~SafeTaskQueue() { stop(); }

    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cond_var_.notify_one();
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this]() { return !queue_.empty() || stopped_; });
        if (stopped_ && queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cond_var_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_var_;
    bool stopped_{false};
};

```

---

## 5. 工程目录结构规范

```text
flac_converter/
├── CMakeLists.txt
├── vcpkg.json                  # 跨平台依赖配置 (FFmpeg, TagLib)
├── include/
│   ├── coordinator.hpp         # 全局调度与生命周期
│   ├── scanner_agent.hpp       # Scanner 接口定义
│   ├── transcoder_agent.hpp    # 转码引擎接口
│   ├── metadata_agent.hpp      # TagLib 标签与图片管理
│   ├── task_models.hpp         # 统一结构体定义
│   ├── thread_pool.hpp         # std::jthread 线程池
│   └── safe_queue.hpp          # 线程安全阻塞队列
├── src/
│   ├── main.cpp                # 程序入口
│   ├── coordinator.cpp
│   ├── scanner_agent.cpp
│   ├── transcoder_agent.cpp
│   └── metadata_agent.cpp
└── ui/
    ├── main_window.hpp         # GUI 主窗口头文件
    └── main_window.cpp         # 列表视图与事件绑定

```

---

## 6. CMakeLists.txt 构建规范

```cmake
cmake_minimum_required(VERSION 3.20)
project(FlacToMp3Converter LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 依赖查找 (基于 vcpkg 或系统 PkgConfig)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libswresample
    libavutil
)

find_package(TagLib REQUIRED)

# 主程序源码构建
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/scanner_agent.cpp
    src/transcoder_agent.cpp
    src/metadata_agent.cpp
    src/coordinator.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::FFMPEG
    TagLib::TagLib
    pthread
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

```

---

## 7. 实施里程碑 (Milestones)

* **M1: 目录扫描与任务队列建立**
* 实现基于 `std::filesystem` 的快速扫描与 4 字节魔数过滤。
* 验证大文件夹（10,000+ 文件）遍历性能，确保主线程不阻塞。


* **M2: FFmpeg 解码与编码管道**
* 编写 RAII 内存管理套件，消除 C 资源泄漏隐患。
* 跑通单文件 FLAC 解码 -> 重采样 -> LAME MP3 压制流程。
* 接入线程池，实现多文件多核并行压制。


* **M3: TagLib 标签与内嵌封面无损继承**
* 在转码完成后，自动提取源 FLAC 的 Vorbis 注释与图片流，规范化写入 MP3 ID3v2 容器。


* **M4: 界面适配与双向事件绑定**
* 接入 Qt 6（或 Dear ImGui），实现表格虚拟刷新与毫秒级进度状态同步。