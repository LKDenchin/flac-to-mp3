#include "coordinator.hpp"
#include <iostream>

Coordinator::Coordinator() {
    scanner_ = std::make_unique<ScannerAgent>();
}

Coordinator::~Coordinator() {
    cancel_scan();
    cancel_transcode();
}

void Coordinator::set_state(AppState new_state) {
    state_ = new_state;
    if (on_state_change_) {
        on_state_change_(new_state);
    }
}

std::vector<TranscodeTask> Coordinator::get_all_tasks() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return tasks_;
}

float Coordinator::overall_progress() const {
    uint32_t total = total_count_.load();
    if (total == 0) return 0.0f;

    std::lock_guard<std::mutex> lock(tasks_mutex_);
    float sum_progress = 0.0f;
    for (const auto& task : tasks_) {
        if (task.status == TaskStatus::Completed) {
            sum_progress += 100.0f;
        } else if (task.status == TaskStatus::Converting) {
            sum_progress += task.progress;
        }
    }
    return sum_progress / total;
}

void Coordinator::reset() {
    cancel_scan();
    cancel_transcode();
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.clear();
    }
    total_count_ = 0;
    completed_count_ = 0;
    failed_count_ = 0;
    set_state(AppState::IDLE);
}

void Coordinator::start_scan(const std::filesystem::path& source_dir, const std::filesystem::path& target_dir) {
    reset();
    set_state(AppState::SCANNING);

    scanner_->start_scan(
        source_dir,
        target_dir,
        [this](const TranscodeTask& task) {
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                tasks_.push_back(task);
            }
            total_count_++;
            if (on_task_discovered_) {
                on_task_discovered_(task);
            }
        },
        [this](uint32_t total) {
            total_count_ = total;
            set_state(AppState::READY);
        }
    );
}

void Coordinator::cancel_scan() {
    if (scanner_ && scanner_->is_scanning()) {
        scanner_->cancel();
        set_state(AppState::IDLE);
    }
}

void Coordinator::update_task_in_list(const TranscodeTask& task) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.task_id == task.task_id) {
                t = task;
                break;
            }
        }
    }
    if (on_task_updated_) {
        on_task_updated_(task);
    }
}

void Coordinator::start_transcode(BitrateProfile profile, std::size_t num_threads) {
    if (state_.load() != AppState::READY) {
        return;
    }

    set_state(AppState::TRANSCODING);
    cancel_transcode_ = false;
    completed_count_ = 0;
    failed_count_ = 0;
    start_time_ = std::chrono::steady_clock::now();

    if (num_threads == 0) {
        num_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }
    thread_pool_ = std::make_unique<ThreadPool>(num_threads);

    std::vector<TranscodeTask> local_tasks;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        local_tasks = tasks_;
    }

    if (local_tasks.empty()) {
        set_state(AppState::COMPLETED);
        return;
    }

    for (auto task : local_tasks) {
        thread_pool_->enqueue([this, task, profile]() mutable {
            if (cancel_transcode_.load()) {
                task.status = TaskStatus::Skipped;
                update_task_in_list(task);
                return;
            }

            // Extract metadata from source FLAC
            MetadataAgent::extract_metadata(task.source_path, task.metadata);

            task.status = TaskStatus::Converting;
            task.progress = 0.0f;
            update_task_in_list(task);

            std::string transcode_err;
            bool success = TranscoderAgent::transcode(
                task,
                profile,
                [this, task](const std::string& /*task_id*/, float pct) mutable {
                    task.progress = pct;
                    update_task_in_list(task);
                },
                transcode_err
            );

            if (success) {
                // Inject metadata & APIC album art into target MP3
                MetadataAgent::inject_metadata(task.target_path, task.metadata);
                task.status = TaskStatus::Completed;
                task.progress = 100.0f;
                completed_count_++;
            } else {
                task.status = TaskStatus::Failed;
                task.error_message = transcode_err;
                failed_count_++;
            }

            update_task_in_list(task);

            uint32_t processed = completed_count_.load() + failed_count_.load();
            if (processed == total_count_.load() || cancel_transcode_.load()) {
                end_time_ = std::chrono::steady_clock::now();
                double elapsed_sec = std::chrono::duration<double>(end_time_ - start_time_).count();
                set_state(AppState::COMPLETED);
                if (on_summary_) {
                    on_summary_(total_count_.load(), completed_count_.load(), failed_count_.load(), elapsed_sec);
                }
            }
        });
    }
}

void Coordinator::cancel_transcode() {
    cancel_transcode_ = true;
    if (thread_pool_) {
        thread_pool_->stop();
        thread_pool_.reset();
    }
}
