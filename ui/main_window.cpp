#include "main_window.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QHeaderView>
#include <QMetaObject>
#include <QApplication>
#include <QFileInfo>
#include <thread>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    coordinator_ = std::make_unique<Coordinator>();
    setup_ui();
    apply_stylesheet();

    // Bind thread-safe coordinator callbacks via QueuedConnection
    coordinator_->set_state_change_callback([this](AppState state) {
        QMetaObject::invokeMethod(this, "handle_state_change", Qt::QueuedConnection, Q_ARG(AppState, state));
    });

    coordinator_->set_task_discovered_callback([this](const TranscodeTask& task) {
        QMetaObject::invokeMethod(this, "handle_task_discovered", Qt::QueuedConnection, Q_ARG(TranscodeTask, task));
    });

    coordinator_->set_task_updated_callback([this](const TranscodeTask& task) {
        QMetaObject::invokeMethod(this, "handle_task_updated", Qt::QueuedConnection, Q_ARG(TranscodeTask, task));
    });

    coordinator_->set_summary_callback([this](uint32_t total, uint32_t completed, uint32_t failed, double total_seconds) {
        QMetaObject::invokeMethod(this, "handle_summary", Qt::QueuedConnection,
                                  Q_ARG(uint32_t, total), Q_ARG(uint32_t, completed),
                                  Q_ARG(uint32_t, failed), Q_ARG(double, total_seconds));
    });
}

void MainWindow::setup_ui() {
    setWindowTitle("Modern FLAC-to-MP3 Transcoder & Tagger");
    resize(980, 680);

    QWidget* central_widget = new QWidget(this);
    setCentralWidget(central_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(16, 16, 16, 16);

    // Top Settings Grid
    QGridLayout* grid_settings = new QGridLayout();
    grid_settings->setSpacing(10);

    QLabel* lbl_source = new QLabel("Source FLAC Folder:", this);
    edit_source_ = new QLineEdit(this);
    edit_source_->setPlaceholderText("Select directory containing FLAC files...");
    btn_browse_source_ = new QPushButton("Browse...", this);
    connect(btn_browse_source_, &QPushButton::clicked, this, &MainWindow::on_browse_source);

    grid_settings->addWidget(lbl_source, 0, 0);
    grid_settings->addWidget(edit_source_, 0, 1);
    grid_settings->addWidget(btn_browse_source_, 0, 2);

    QLabel* lbl_target = new QLabel("Target MP3 Folder:", this);
    edit_target_ = new QLineEdit(this);
    edit_target_->setPlaceholderText("Select directory to save converted MP3 files...");
    btn_browse_target_ = new QPushButton("Browse...", this);
    connect(btn_browse_target_, &QPushButton::clicked, this, &MainWindow::on_browse_target);

    grid_settings->addWidget(lbl_target, 1, 0);
    grid_settings->addWidget(edit_target_, 1, 1);
    grid_settings->addWidget(btn_browse_target_, 1, 2);

    // Audio & Processing Settings
    QHBoxLayout* layout_options = new QHBoxLayout();
    QLabel* lbl_bitrate = new QLabel("Bitrate Profile:", this);
    combo_bitrate_ = new QComboBox(this);
    combo_bitrate_->addItem("CBR 320 kbps (High Quality)", static_cast<int>(BitrateProfile::CBR_320K));
    combo_bitrate_->addItem("CBR 256 kbps", static_cast<int>(BitrateProfile::CBR_256K));
    combo_bitrate_->addItem("CBR 192 kbps", static_cast<int>(BitrateProfile::CBR_192K));
    combo_bitrate_->addItem("VBR V0 (Extreme Quality)", static_cast<int>(BitrateProfile::VBR_V0));

    QLabel* lbl_threads = new QLabel("Worker Threads:", this);
    spin_threads_ = new QSpinBox(this);
    spin_threads_->setRange(1, 64);
    spin_threads_->setValue(static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1)));

    layout_options->addWidget(lbl_bitrate);
    layout_options->addWidget(combo_bitrate_);
    layout_options->addSpacing(20);
    layout_options->addWidget(lbl_threads);
    layout_options->addWidget(spin_threads_);
    layout_options->addStretch();

    grid_settings->addLayout(layout_options, 2, 1, 1, 2);
    main_layout->addLayout(grid_settings);

    // Action Buttons Row
    QHBoxLayout* layout_actions = new QHBoxLayout();
    btn_scan_ = new QPushButton("🔍 Scan FLAC Files", this);
    btn_transcode_ = new QPushButton("⚡ Start Transcoding", this);
    btn_cancel_ = new QPushButton("❌ Cancel / Reset", this);

    btn_transcode_->setEnabled(false);

    connect(btn_scan_, &QPushButton::clicked, this, &MainWindow::on_start_scan);
    connect(btn_transcode_, &QPushButton::clicked, this, &MainWindow::on_start_transcode);
    connect(btn_cancel_, &QPushButton::clicked, this, &MainWindow::on_cancel_reset);

    layout_actions->addWidget(btn_scan_);
    layout_actions->addWidget(btn_transcode_);
    layout_actions->addWidget(btn_cancel_);
    layout_actions->addStretch();

    main_layout->addLayout(layout_actions);

    // Table View
    table_tasks_ = new QTableWidget(this);
    table_tasks_->setColumnCount(6);
    QStringList headers = {"#", "File Name", "Title / Artist", "Status", "Progress", "Details / Error"};
    table_tasks_->setHorizontalHeaderLabels(headers);
    table_tasks_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_tasks_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_tasks_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_tasks_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_tasks_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table_tasks_->setColumnWidth(4, 180);
    table_tasks_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_tasks_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_tasks_->setAlternatingRowColors(true);

    main_layout->addWidget(table_tasks_);

    // Bottom Summary & Progress Panel
    QVBoxLayout* layout_bottom = new QVBoxLayout();
    bar_overall_ = new QProgressBar(this);
    bar_overall_->setRange(0, 100);
    bar_overall_->setValue(0);
    bar_overall_->setTextVisible(true);

    label_status_summary_ = new QLabel("Status: Idle. Select source directory to scan.", this);
    label_status_summary_->setStyleSheet("font-weight: bold; color: #a6adc8;");

    layout_bottom->addWidget(bar_overall_);
    layout_bottom->addWidget(label_status_summary_);

    main_layout->addLayout(layout_bottom);
}

void MainWindow::apply_stylesheet() {
    QString qss = R"(
        QMainWindow {
            background-color: #1e1e2e;
            color: #cdd6f4;
            font-family: 'Segoe UI', 'Ubuntu', 'Helvetica Neue', sans-serif;
            font-size: 13px;
        }
        QLabel {
            color: #cdd6f4;
            font-size: 13px;
        }
        QLineEdit {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 6px 10px;
        }
        QLineEdit:focus {
            border: 1px solid #89b4fa;
        }
        QComboBox, QSpinBox {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 6px 10px;
        }
        QPushButton {
            background-color: #89b4fa;
            color: #11111b;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #b4befe;
        }
        QPushButton:pressed {
            background-color: #74c7ec;
        }
        QPushButton:disabled {
            background-color: #45475a;
            color: #6c7086;
        }
        QTableWidget {
            background-color: #181825;
            color: #cdd6f4;
            gridline-color: #313244;
            border: 1px solid #313244;
            border-radius: 8px;
            alternate-background-color: #1e1e2e;
        }
        QHeaderView::section {
            background-color: #313244;
            color: #cdd6f4;
            padding: 6px;
            border: none;
            font-weight: bold;
        }
        QProgressBar {
            border: 1px solid #45475a;
            border-radius: 6px;
            text-align: center;
            background-color: #313244;
            color: #cdd6f4;
            height: 20px;
        }
        QProgressBar::chunk {
            background-color: #a6e3a1;
            border-radius: 5px;
        }
    )";
    setStyleSheet(qss);
}

void MainWindow::on_browse_source() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Source FLAC Directory");
    if (!dir.isEmpty()) {
        edit_source_->setText(dir);
        if (edit_target_->text().isEmpty()) {
            edit_target_->setText(dir + "_mp3");
        }
    }
}

void MainWindow::on_browse_target() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Target MP3 Directory");
    if (!dir.isEmpty()) {
        edit_target_->setText(dir);
    }
}

void MainWindow::on_start_scan() {
    QString src = edit_source_->text().trimmed();
    QString tgt = edit_target_->text().trimmed();
    if (src.isEmpty()) return;
    if (tgt.isEmpty()) tgt = src + "_mp3";

    table_tasks_->setRowCount(0);
    bar_overall_->setValue(0);
    coordinator_->start_scan(src.toStdString(), tgt.toStdString());
}

void MainWindow::on_start_transcode() {
    int profile_idx = combo_bitrate_->currentData().toInt();
    auto profile = static_cast<BitrateProfile>(profile_idx);
    int threads = spin_threads_->value();
    coordinator_->start_transcode(profile, static_cast<std::size_t>(threads));
}

void MainWindow::on_cancel_reset() {
    coordinator_->reset();
    table_tasks_->setRowCount(0);
    bar_overall_->setValue(0);
    label_status_summary_->setText("Status: Reset complete. Ready.");
}

int MainWindow::find_row_by_task_id(const QString& task_id) {
    for (int row = 0; row < table_tasks_->rowCount(); ++row) {
        if (table_tasks_->item(row, 0)->data(Qt::UserRole).toString() == task_id) {
            return row;
        }
    }
    return -1;
}

void MainWindow::handle_state_change(AppState state) {
    switch (state) {
        case AppState::IDLE:
            btn_scan_->setEnabled(true);
            btn_transcode_->setEnabled(false);
            label_status_summary_->setText("Status: Idle.");
            break;
        case AppState::SCANNING:
            btn_scan_->setEnabled(false);
            btn_transcode_->setEnabled(false);
            label_status_summary_->setText("Status: Scanning directory for FLAC files...");
            break;
        case AppState::READY:
            btn_scan_->setEnabled(true);
            btn_transcode_->setEnabled(coordinator_->total_tasks() > 0);
            label_status_summary_->setText(QString("Status: Scan finished. Found %1 FLAC file(s). Ready to transcode.")
                                          .arg(coordinator_->total_tasks()));
            break;
        case AppState::TRANSCODING:
            btn_scan_->setEnabled(false);
            btn_transcode_->setEnabled(false);
            label_status_summary_->setText("Status: Batch transcoding in progress...");
            break;
        case AppState::COMPLETED:
            btn_scan_->setEnabled(true);
            btn_transcode_->setEnabled(true);
            bar_overall_->setValue(100);
            break;
    }
}

void MainWindow::handle_task_discovered(const TranscodeTask& task) {
    int row = table_tasks_->rowCount();
    table_tasks_->insertRow(row);

    QTableWidgetItem* item_num = new QTableWidgetItem(QString::number(row + 1));
    item_num->setData(Qt::UserRole, QString::fromStdString(task.task_id));
    table_tasks_->setItem(row, 0, item_num);

    QTableWidgetItem* item_file = new QTableWidgetItem(QString::fromStdString(task.source_path.filename().string()));
    table_tasks_->setItem(row, 1, item_file);

    QTableWidgetItem* item_meta = new QTableWidgetItem("Scanning...");
    table_tasks_->setItem(row, 2, item_meta);

    QTableWidgetItem* item_status = new QTableWidgetItem("Queued");
    table_tasks_->setItem(row, 3, item_status);

    QProgressBar* pbar = new QProgressBar(this);
    pbar->setRange(0, 100);
    pbar->setValue(0);
    pbar->setTextVisible(true);
    table_tasks_->setCellWidget(row, 4, pbar);

    QTableWidgetItem* item_details = new QTableWidgetItem("");
    table_tasks_->setItem(row, 5, item_details);
}

void MainWindow::handle_task_updated(const TranscodeTask& task) {
    QString tid = QString::fromStdString(task.task_id);
    int row = find_row_by_task_id(tid);
    if (row < 0) return;

    // Update metadata info if available
    QString meta_str;
    if (!task.metadata.title.empty()) {
        meta_str = QString::fromStdString(task.metadata.title);
        if (!task.metadata.artist.empty()) {
            meta_str += " - " + QString::fromStdString(task.metadata.artist);
        }
    } else {
        meta_str = QString::fromStdString(task.source_path.stem().string());
    }
    table_tasks_->item(row, 2)->setText(meta_str);

    // Update status text
    QString status_str;
    switch (task.status) {
        case TaskStatus::Queued: status_str = "Queued"; break;
        case TaskStatus::Scanning: status_str = "Scanning"; break;
        case TaskStatus::Converting: status_str = "Converting"; break;
        case TaskStatus::Completed: status_str = "Completed"; break;
        case TaskStatus::Failed: status_str = "Failed"; break;
        case TaskStatus::Skipped: status_str = "Skipped"; break;
    }
    table_tasks_->item(row, 3)->setText(status_str);

    // Update progress bar cell widget
    QWidget* widget = table_tasks_->cellWidget(row, 4);
    if (auto* pbar = qobject_cast<QProgressBar*>(widget)) {
        pbar->setValue(static_cast<int>(task.progress));
    }

    if (task.error_message.has_value()) {
        table_tasks_->item(row, 5)->setText(QString::fromStdString(task.error_message.value()));
    }

    // Update overall progress bar
    bar_overall_->setValue(static_cast<int>(coordinator_->overall_progress()));
}

void MainWindow::handle_summary(uint32_t total, uint32_t completed, uint32_t failed, double total_seconds) {
    label_status_summary_->setText(QString("Status: Batch Transcode Complete! Total: %1 | Success: %2 | Failed: %3 | Time Elapsed: %4s")
                                  .arg(total).arg(completed).arg(failed).arg(total_seconds, 0, 'f', 2));
}
