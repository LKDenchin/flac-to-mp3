#include <QApplication>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include "main_window.hpp"
#include "coordinator.hpp"

void print_cli_help() {
    std::cout << "Usage: flac_converter [OPTIONS]\n"
              << "Options:\n"
              << "  --cli                  Run in headless Command Line Mode\n"
              << "  -i, --input <dir>      Source directory containing FLAC files\n"
              << "  -o, --output <dir>     Target directory to save converted MP3 files\n"
              << "  -b, --bitrate <prof>   Bitrate profile: 320k (default), 256k, 192k, v0\n"
              << "  -t, --threads <num>    Number of concurrent worker threads\n"
              << "  -h, --help             Show this help message\n";
}

int run_cli(int argc, char* argv[]) {
    std::string source_dir;
    std::string target_dir;
    BitrateProfile profile = BitrateProfile::CBR_320K;
    std::size_t threads = std::max(1u, std::thread::hardware_concurrency() - 1);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            source_dir = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            target_dir = argv[++i];
        } else if ((arg == "-b" || arg == "--bitrate") && i + 1 < argc) {
            std::string b = argv[++i];
            if (b == "320k") profile = BitrateProfile::CBR_320K;
            else if (b == "256k") profile = BitrateProfile::CBR_256K;
            else if (b == "192k") profile = BitrateProfile::CBR_192K;
            else if (b == "v0") profile = BitrateProfile::VBR_V0;
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            try {
                threads = std::stoul(argv[++i]);
            } catch (...) {}
        } else if (arg == "-h" || arg == "--help") {
            print_cli_help();
            return 0;
        }
    }

    if (source_dir.empty()) {
        std::cerr << "Error: Source directory is required in CLI mode (-i / --input).\n";
        print_cli_help();
        return 1;
    }
    if (target_dir.empty()) {
        target_dir = source_dir + "_mp3";
    }

    std::cout << "[CLI] Starting batch FLAC-to-MP3 transcode...\n";
    std::cout << "[CLI] Source: " << source_dir << "\n";
    std::cout << "[CLI] Target: " << target_dir << "\n";
    std::cout << "[CLI] Threads: " << threads << "\n";

    Coordinator coordinator;
    std::mutex cv_mutex;
    std::condition_variable cv;
    std::atomic<bool> scan_done{false};
    std::atomic<bool> transcode_done{false};

    coordinator.set_task_discovered_callback([](const TranscodeTask& task) {
        std::cout << "  -> Found: " << task.source_path.filename().string() << "\n";
    });

    coordinator.set_state_change_callback([&](AppState state) {
        if (state == AppState::READY) {
            {
                std::lock_guard<std::mutex> lock(cv_mutex);
                scan_done = true;
            }
            cv.notify_all();
        } else if (state == AppState::COMPLETED) {
            {
                std::lock_guard<std::mutex> lock(cv_mutex);
                transcode_done = true;
            }
            cv.notify_all();
        }
    });

    coordinator.set_summary_callback([](uint32_t total, uint32_t completed, uint32_t failed, double total_seconds) {
        std::cout << "\n================ Summary Report ================\n";
        std::cout << "  Total Files Found: " << total << "\n";
        std::cout << "  Successfully Converted: " << completed << "\n";
        std::cout << "  Failed Conversions: " << failed << "\n";
        std::cout << "  Total Time Elapsed: " << total_seconds << " s\n";
        std::cout << "=================================================\n";
    });

    // 1. Scan
    coordinator.start_scan(source_dir, target_dir);
    {
        std::unique_lock<std::mutex> lock(cv_mutex);
        cv.wait(lock, [&]() { return scan_done.load(); });
    }

    if (coordinator.total_tasks() == 0) {
        std::cout << "[CLI] No valid FLAC files found.\n";
        return 0;
    }

    // 2. Transcode
    std::cout << "[CLI] Transcoding " << coordinator.total_tasks() << " file(s)...\n";
    coordinator.start_transcode(profile, threads);

    {
        std::unique_lock<std::mutex> lock(cv_mutex);
        cv.wait(lock, [&]() { return transcode_done.load(); });
    }

    return coordinator.failed_tasks() > 0 ? 1 : 0;
}

int main(int argc, char *argv[]) {
    // Check if CLI mode flag is present
    bool is_cli = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli" || arg == "-h" || arg == "--help") {
            is_cli = true;
            break;
        }
    }

    if (is_cli) {
        return run_cli(argc, argv);
    }

    // Qt GUI mode
    QApplication app(argc, argv);
    MainWindow main_win;
    main_win.show();
    return app.exec();
}
