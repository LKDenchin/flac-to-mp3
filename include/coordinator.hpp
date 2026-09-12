#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>

#include "task_models.hpp"
#include "scanner_agent.hpp"
#include "transcoder_agent.hpp"
#include "metadata_agent.hpp"
#include "thread_pool.hpp"

using TaskUpdatedCallback = std::function<void(const TranscodeTask& task)>;
using SummaryCallback = std::function<void(uint32_t total, uint32_t completed, uint32_t failed, double total_seconds)>;

class Coordinator {
public:
    Coordinator();
    ~Coordinator();

    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    void start_scan(const std::filesystem::path& source_dir, const std::filesystem::path& target_dir);
    void cancel_scan();

    void start_transcode(OutputFormat format, BitrateProfile profile, const std::vector<std::string>& selected_task_ids, std::size_t num_threads = 0);
    void start_transcode(OutputFormat format, BitrateProfile profile, std::size_t num_threads = 0) {
        start_transcode(format, profile, {}, num_threads);
    }
    void start_transcode(BitrateProfile profile, std::size_t num_threads = 0) {
        start_transcode(OutputFormat::MP3, profile, {}, num_threads);
    }
    void cancel_transcode();
    void reset();

    AppState current_state() const { return state_.load(); }
    std::vector<TranscodeTask> get_all_tasks() const;

    // Callback setters
    void set_state_change_callback(StateChangeCallback cb) { on_state_change_ = cb; }
    void set_task_discovered_callback(TaskDiscoveredCallback cb) { on_task_discovered_ = cb; }
    void set_task_updated_callback(TaskUpdatedCallback cb) { on_task_updated_ = cb; }
    void set_summary_callback(SummaryCallback cb) { on_summary_ = cb; }

    uint32_t total_tasks() const { return total_count_.load(); }
    uint32_t completed_tasks() const { return completed_count_.load(); }
    uint32_t failed_tasks() const { return failed_count_.load(); }
    float overall_progress() const;

private:
    void set_state(AppState new_state);
    void update_task_in_list(const TranscodeTask& task);

    std::atomic<AppState> state_{AppState::IDLE};
    mutable std::mutex tasks_mutex_;
    std::vector<TranscodeTask> tasks_;

    std::atomic<uint32_t> total_count_{0};
    std::atomic<uint32_t> completed_count_{0};
    std::atomic<uint32_t> failed_count_{0};

    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;

    std::unique_ptr<ScannerAgent> scanner_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::atomic<bool> cancel_transcode_{false};

    StateChangeCallback on_state_change_;
    TaskDiscoveredCallback on_task_discovered_;
    TaskUpdatedCallback on_task_updated_;
    SummaryCallback on_summary_;
};
