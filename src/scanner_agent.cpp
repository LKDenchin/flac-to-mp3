#include "scanner_agent.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <system_error>

#include <unordered_set>

ScannerAgent::~ScannerAgent() {
    cancel();
}

bool ScannerAgent::is_valid_flac_file(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    char header[4] = {0};
    file.read(header, 4);
    if (file.gcount() < 4) {
        return false;
    }

    return (static_cast<unsigned char>(header[0]) == 0x66 &&
            static_cast<unsigned char>(header[1]) == 0x4C &&
            static_cast<unsigned char>(header[2]) == 0x61 &&
            static_cast<unsigned char>(header[3]) == 0x43); // "fLaC"
}

void ScannerAgent::start_scan(const std::filesystem::path& source_dir,
                              const std::filesystem::path& target_dir,
                              TaskDiscoveredCallback on_discovered,
                              ScanFinishedCallback on_finished) {
    cancel();
    scanning_ = true;
    cancel_requested_ = false;

    scan_thread_ = std::jthread([this, source_dir, target_dir, on_discovered, on_finished]() {
        scan_worker(source_dir, target_dir, on_discovered, on_finished);
    });
}

void ScannerAgent::cancel() {
    cancel_requested_ = true;
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
    scanning_ = false;
}

void ScannerAgent::scan_worker(std::filesystem::path source_dir,
                               std::filesystem::path target_dir,
                               TaskDiscoveredCallback on_discovered,
                               ScanFinishedCallback on_finished) {
    uint32_t total_count = 0;
    std::error_code ec;

    if (!std::filesystem::exists(source_dir, ec) || !std::filesystem::is_directory(source_dir, ec)) {
        scanning_ = false;
        if (on_finished) on_finished(0);
        return;
    }

    auto iter_options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iter(source_dir, iter_options, ec);
    std::filesystem::recursive_directory_iterator end;
    std::unordered_set<std::string> used_target_paths;

    while (iter != end && !cancel_requested_.load()) {
        const auto& entry = *iter;
        std::error_code entry_ec;
        bool is_reg = entry.is_regular_file(entry_ec);

        if (!entry_ec && is_reg) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (ext == ".flac") {
                if (is_valid_flac_file(entry.path())) {
                    std::filesystem::path target_filename = entry.path().filename();
                    target_filename.replace_extension(".mp3");
                    std::filesystem::path target_path = target_dir / target_filename;

                    if (used_target_paths.contains(target_path.string())) {
                        std::string stem = entry.path().stem().string();
                        int counter = 1;
                        do {
                            target_path = target_dir / (stem + "_" + std::to_string(counter) + ".mp3");
                            counter++;
                        } while (used_target_paths.contains(target_path.string()));
                    }
                    used_target_paths.insert(target_path.string());

                    TranscodeTask task;
                    task.task_id = entry.path().string();
                    task.source_path = entry.path();
                    task.target_path = target_path;
                    task.file_size_bytes = entry.file_size(entry_ec);
                    task.status = TaskStatus::Queued;
                    task.progress = 0.0f;

                    total_count++;
                    if (on_discovered) {
                        on_discovered(task);
                    }
                }
            }
        }

        // Increment with permission error handling
        iter.increment(ec);
        if (ec) {
            ec.clear();
        }
    }

    scanning_ = false;
    if (on_finished && !cancel_requested_.load()) {
        on_finished(total_count);
    }
}
