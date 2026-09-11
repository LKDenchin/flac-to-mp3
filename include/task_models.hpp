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

enum class AppState {
    IDLE,
    SCANNING,
    READY,
    TRANSCODING,
    COMPLETED
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
using TaskDiscoveredCallback = std::function<void(const TranscodeTask& task)>;
using ScanFinishedCallback = std::function<void(uint32_t total_count)>;
using StateChangeCallback = std::function<void(AppState new_state)>;
