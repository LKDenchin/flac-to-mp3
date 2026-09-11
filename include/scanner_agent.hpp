#pragma once

#include <filesystem>
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include "task_models.hpp"

class ScannerAgent {
public:
    ScannerAgent() = default;
    ~ScannerAgent();

    ScannerAgent(const ScannerAgent&) = delete;
    ScannerAgent& operator=(const ScannerAgent&) = delete;

    void start_scan(const std::filesystem::path& source_dir,
                    const std::filesystem::path& target_dir,
                    TaskDiscoveredCallback on_discovered,
                    ScanFinishedCallback on_finished);

    void cancel();
    bool is_scanning() const { return scanning_.load(); }
    static bool is_valid_flac_file(const std::filesystem::path& file_path);

private:
    void scan_worker(std::filesystem::path source_dir,
                     std::filesystem::path target_dir,
                     TaskDiscoveredCallback on_discovered,
                     ScanFinishedCallback on_finished);

    std::atomic<bool> scanning_{false};
    std::atomic<bool> cancel_requested_{false};
    std::jthread scan_thread_;
};
