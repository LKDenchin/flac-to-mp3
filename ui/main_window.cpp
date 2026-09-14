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
    qRegisterMetaType<AppState>("AppState");
    qRegisterMetaType<TranscodeTask>("TranscodeTask");
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
    resize(1020, 680);

    QWidget* central_widget = new QWidget(this);
    setCentralWidget(central_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(16, 16, 16, 16);

    // Top Settings Grid
    QGridLayout* grid_settings = new QGridLayout();
    grid_settings->setSpacing(10);

    lbl_source_ = new QLabel(this);
    edit_source_ = new QLineEdit(this);
    btn_browse_source_ = new QPushButton(this);
    connect(btn_browse_source_, &QPushButton::clicked, this, &MainWindow::on_browse_source);

    grid_settings->addWidget(lbl_source_, 0, 0);
    grid_settings->addWidget(edit_source_, 0, 1);
    grid_settings->addWidget(btn_browse_source_, 0, 2);

    lbl_target_ = new QLabel(this);
    edit_target_ = new QLineEdit(this);
    btn_browse_target_ = new QPushButton(this);
    connect(btn_browse_target_, &QPushButton::clicked, this, &MainWindow::on_browse_target);

    grid_settings->addWidget(lbl_target_, 1, 0);
    grid_settings->addWidget(edit_target_, 1, 1);
    grid_settings->addWidget(btn_browse_target_, 1, 2);

    // Audio & Processing Settings
    QHBoxLayout* layout_options = new QHBoxLayout();
    lbl_format_ = new QLabel(this);
    combo_format_ = new QComboBox(this);
    combo_format_->addItem("", static_cast<int>(OutputFormat::MP3));
    combo_format_->addItem("", static_cast<int>(OutputFormat::FLAC));
    combo_format_->addItem("", static_cast<int>(OutputFormat::ALAC));
    combo_format_->addItem("", static_cast<int>(OutputFormat::WAV));

    lbl_bitrate_ = new QLabel(this);
    combo_bitrate_ = new QComboBox(this);
    combo_bitrate_->addItem("", static_cast<int>(BitrateProfile::CBR_320K));
    combo_bitrate_->addItem("", static_cast<int>(BitrateProfile::CBR_256K));
    combo_bitrate_->addItem("", static_cast<int>(BitrateProfile::CBR_192K));
    combo_bitrate_->addItem("", static_cast<int>(BitrateProfile::VBR_V0));

    lbl_threads_ = new QLabel(this);
    spin_threads_ = new QSpinBox(this);
    spin_threads_->setRange(1, 64);
    spin_threads_->setValue(static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1)));

    lbl_language_ = new QLabel(this);
    combo_language_ = new QComboBox(this);
    combo_language_->addItem("简体中文", static_cast<int>(Language::zh_CN));
    combo_language_->addItem("繁體中文", static_cast<int>(Language::zh_TW));
    combo_language_->addItem("English", static_cast<int>(Language::en));
    combo_language_->addItem("日本語", static_cast<int>(Language::ja));
    connect(combo_language_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::on_language_changed);

    layout_options->addWidget(lbl_format_);
    layout_options->addWidget(combo_format_);
    layout_options->addSpacing(15);
    layout_options->addWidget(lbl_bitrate_);
    layout_options->addWidget(combo_bitrate_);
    layout_options->addSpacing(15);
    layout_options->addWidget(lbl_threads_);
    layout_options->addWidget(spin_threads_);
    layout_options->addSpacing(15);
    layout_options->addWidget(lbl_language_);
    layout_options->addWidget(combo_language_);
    layout_options->addStretch();

    grid_settings->addLayout(layout_options, 2, 1, 1, 2);
    main_layout->addLayout(grid_settings);

    // Action Buttons Row
    QHBoxLayout* layout_actions = new QHBoxLayout();
    btn_scan_ = new QPushButton(this);
    btn_transcode_ = new QPushButton(this);
    btn_cancel_ = new QPushButton(this);

    btn_select_all_ = new QPushButton(this);
    btn_deselect_all_ = new QPushButton(this);
    lbl_selection_count_ = new QLabel(this);
    lbl_selection_count_->setStyleSheet("color: #89b4fa; font-weight: bold; padding: 0 8px;");

    btn_transcode_->setEnabled(false);

    connect(btn_scan_, &QPushButton::clicked, this, &MainWindow::on_start_scan);
    connect(btn_transcode_, &QPushButton::clicked, this, &MainWindow::on_start_transcode);
    connect(btn_cancel_, &QPushButton::clicked, this, &MainWindow::on_cancel_reset);
    connect(btn_select_all_, &QPushButton::clicked, this, &MainWindow::on_select_all);
    connect(btn_deselect_all_, &QPushButton::clicked, this, &MainWindow::on_deselect_all);

    layout_actions->addWidget(btn_scan_);
    layout_actions->addWidget(btn_transcode_);
    layout_actions->addWidget(btn_cancel_);
    layout_actions->addSpacing(15);
    layout_actions->addWidget(btn_select_all_);
    layout_actions->addWidget(btn_deselect_all_);
    layout_actions->addWidget(lbl_selection_count_);
    layout_actions->addStretch();

    main_layout->addLayout(layout_actions);

    // Table View
    table_tasks_ = new QTableWidget(this);
    table_tasks_->setColumnCount(6);
    table_tasks_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_tasks_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_tasks_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_tasks_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_tasks_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table_tasks_->setColumnWidth(4, 180);
    table_tasks_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_tasks_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_tasks_->setAlternatingRowColors(true);

    connect(table_tasks_, &QTableWidget::itemChanged, this, &MainWindow::on_item_changed);

    main_layout->addWidget(table_tasks_);

    // Bottom Summary & Progress Panel
    QVBoxLayout* layout_bottom = new QVBoxLayout();
    bar_overall_ = new QProgressBar(this);
    bar_overall_->setRange(0, 100);
    bar_overall_->setValue(0);
    bar_overall_->setTextVisible(true);

    label_status_summary_ = new QLabel(this);
    label_status_summary_->setStyleSheet("font-weight: bold; color: #a6adc8;");

    layout_bottom->addWidget(bar_overall_);
    layout_bottom->addWidget(label_status_summary_);

    main_layout->addLayout(layout_bottom);

    retranslate_ui();
}

void MainWindow::apply_stylesheet() {
    QString qss = R"(
        QMainWindow, QWidget {
            background-color: #1e1e2e;
            color: #cdd6f4;
            font-family: 'Segoe UI', 'Ubuntu', 'Microsoft YaHei', sans-serif;
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
        QComboBox QAbstractItemView {
            background-color: #181825;
            color: #cdd6f4;
            selection-background-color: #45475a;
            selection-color: #89b4fa;
            border: 1px solid #45475a;
            outline: none;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            background-color: #45475a;
            border: none;
            width: 16px;
        }
        QPushButton {
            background-color: #89b4fa;
            color: #11111b;
            border: none;
            border-radius: 6px;
            padding: 8px 14px;
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
            selection-background-color: #313244;
            selection-color: #89b4fa;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:hover {
            background-color: #313244;
        }
        QHeaderView::section {
            background-color: #313244;
            color: #cdd6f4;
            padding: 8px;
            border: none;
            border-bottom: 2px solid #45475a;
            font-weight: bold;
        }
        QScrollBar:vertical {
            background-color: #181825;
            width: 12px;
            margin: 0px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical {
            background-color: #45475a;
            min-height: 20px;
            border-radius: 6px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #585b70;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            background-color: #181825;
            height: 12px;
            margin: 0px;
            border-radius: 6px;
        }
        QScrollBar::handle:horizontal {
            background-color: #45475a;
            min-width: 20px;
            border-radius: 6px;
        }
        QScrollBar::handle:horizontal:hover {
            background-color: #585b70;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
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
    int fmt_idx = combo_format_->currentData().toInt();
    auto format = static_cast<OutputFormat>(fmt_idx);
    int profile_idx = combo_bitrate_->currentData().toInt();
    auto profile = static_cast<BitrateProfile>(profile_idx);
    int threads = spin_threads_->value();

    std::vector<std::string> selected_ids;
    for (int r = 0; r < table_tasks_->rowCount(); ++r) {
        auto* item = table_tasks_->item(r, 0);
        if (item && item->checkState() == Qt::Checked) {
            selected_ids.push_back(item->data(Qt::UserRole).toString().toStdString());
        }
    }

    coordinator_->start_transcode(format, profile, selected_ids, static_cast<std::size_t>(threads));
}

void MainWindow::on_select_all() {
    table_tasks_->blockSignals(true);
    for (int r = 0; r < table_tasks_->rowCount(); ++r) {
        if (auto* item = table_tasks_->item(r, 0)) {
            item->setCheckState(Qt::Checked);
        }
    }
    table_tasks_->blockSignals(false);
    update_selection_counter();
}

void MainWindow::on_deselect_all() {
    table_tasks_->blockSignals(true);
    for (int r = 0; r < table_tasks_->rowCount(); ++r) {
        if (auto* item = table_tasks_->item(r, 0)) {
            item->setCheckState(Qt::Unchecked);
        }
    }
    table_tasks_->blockSignals(false);
    update_selection_counter();
}

void MainWindow::on_item_changed(QTableWidgetItem* item) {
    if (item && item->column() == 0) {
        update_selection_counter();
    }
}

void MainWindow::on_language_changed(int index) {
    auto lang = static_cast<Language>(combo_language_->itemData(index).toInt());
    I18n::instance().set_language(lang);
    retranslate_ui();
}

void MainWindow::retranslate_ui() {
    auto& i18n = I18n::instance();
    setWindowTitle(QString::fromStdString(i18n.get(StringKey::AppTitle)));

    lbl_source_->setText(QString::fromStdString(i18n.get(StringKey::SourceFolder)));
    edit_source_->setPlaceholderText(QString::fromStdString(i18n.get(StringKey::SourcePlaceholder)));
    btn_browse_source_->setText(QString::fromStdString(i18n.get(StringKey::Browse)));

    lbl_target_->setText(QString::fromStdString(i18n.get(StringKey::TargetFolder)));
    edit_target_->setPlaceholderText(QString::fromStdString(i18n.get(StringKey::TargetPlaceholder)));
    btn_browse_target_->setText(QString::fromStdString(i18n.get(StringKey::Browse)));

    lbl_format_->setText(QString::fromStdString(i18n.get(StringKey::OutputFormat)));
    int current_fmt = combo_format_->currentIndex();
    combo_format_->setItemText(0, QString::fromStdString(i18n.get(StringKey::FmtMP3)));
    combo_format_->setItemText(1, QString::fromStdString(i18n.get(StringKey::FmtFLAC)));
    combo_format_->setItemText(2, QString::fromStdString(i18n.get(StringKey::FmtALAC)));
    combo_format_->setItemText(3, QString::fromStdString(i18n.get(StringKey::FmtWAV)));
    combo_format_->setCurrentIndex(current_fmt >= 0 ? current_fmt : 0);

    lbl_bitrate_->setText(QString::fromStdString(i18n.get(StringKey::BitrateProfile)));
    int current_br = combo_bitrate_->currentIndex();
    combo_bitrate_->setItemText(0, QString::fromStdString(i18n.get(StringKey::Bitrate320k)));
    combo_bitrate_->setItemText(1, QString::fromStdString(i18n.get(StringKey::Bitrate256k)));
    combo_bitrate_->setItemText(2, QString::fromStdString(i18n.get(StringKey::Bitrate192k)));
    combo_bitrate_->setItemText(3, QString::fromStdString(i18n.get(StringKey::BitrateV0)));
    combo_bitrate_->setCurrentIndex(current_br >= 0 ? current_br : 0);

    lbl_threads_->setText(QString::fromStdString(i18n.get(StringKey::WorkerThreads)));
    lbl_language_->setText(QString::fromStdString(i18n.get(StringKey::LanguageSelect)));

    btn_scan_->setText(QString::fromStdString(i18n.get(StringKey::ScanFiles)));
    btn_transcode_->setText(QString::fromStdString(i18n.get(StringKey::StartTranscode)));
    btn_cancel_->setText(QString::fromStdString(i18n.get(StringKey::Reset)));
    btn_select_all_->setText(QString::fromStdString(i18n.get(StringKey::SelectAll)));
    btn_deselect_all_->setText(QString::fromStdString(i18n.get(StringKey::DeselectAll)));

    QStringList headers = {
        QString::fromStdString(i18n.get(StringKey::HeaderSelectNum)),
        QString::fromStdString(i18n.get(StringKey::HeaderFileName)),
        QString::fromStdString(i18n.get(StringKey::HeaderTitleArtist)),
        QString::fromStdString(i18n.get(StringKey::HeaderStatus)),
        QString::fromStdString(i18n.get(StringKey::HeaderProgress)),
        QString::fromStdString(i18n.get(StringKey::HeaderDetails))
    };
    table_tasks_->setHorizontalHeaderLabels(headers);

    update_selection_counter();

    switch (coordinator_->current_state()) {
        case AppState::IDLE:
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusIdle)));
            break;
        case AppState::SCANNING:
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusScanning)));
            break;
        case AppState::READY:
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusReady)).arg(coordinator_->total_tasks()));
            break;
        case AppState::TRANSCODING:
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusTranscoding)));
            break;
        case AppState::COMPLETED:
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusCompleted)));
            break;
    }
}

void MainWindow::update_selection_counter() {
    int total = table_tasks_->rowCount();
    int selected = 0;
    for (int r = 0; r < total; ++r) {
        if (auto* item = table_tasks_->item(r, 0)) {
            if (item->checkState() == Qt::Checked) {
                selected++;
            }
        }
    }
    std::string sel_label = I18n::instance().get(StringKey::SelectedCount);
    lbl_selection_count_->setText(QString("%1: %2 / %3")
                                  .arg(QString::fromStdString(sel_label))
                                  .arg(selected).arg(total));
    if (coordinator_->current_state() == AppState::READY) {
        btn_transcode_->setEnabled(selected > 0);
    }
}

void MainWindow::on_cancel_reset() {
    coordinator_->reset();
    table_tasks_->setRowCount(0);
    bar_overall_->setValue(0);
    update_selection_counter();
    label_status_summary_->setText(QString::fromStdString(I18n::instance().get(StringKey::StatusReset)));
}

int MainWindow::find_row_by_task_id(const QString& task_id) {
    for (int row = 0; row < table_tasks_->rowCount(); ++row) {
        auto* item = table_tasks_ ->item(row,0);
        if (item &&item->data(Qt::UserRole).toString() == task_id) {
        return row;
        }
    }
    return -1;
}

void MainWindow::handle_state_change(AppState state) {
    auto& i18n = I18n::instance();
    switch (state) {
        case AppState::IDLE:
            btn_scan_->setEnabled(true);
            btn_transcode_->setEnabled(false);
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusIdle)));
            break;
        case AppState::SCANNING:
            btn_scan_->setEnabled(false);
            btn_transcode_->setEnabled(false);
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusScanning)));
            break;
        case AppState::READY:
            btn_scan_->setEnabled(true);
            update_selection_counter();
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusReady)).arg(coordinator_->total_tasks()));
            break;
        case AppState::TRANSCODING:
            btn_scan_->setEnabled(false);
            btn_transcode_->setEnabled(false);
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusTranscoding)));
            break;
        case AppState::COMPLETED:
            btn_scan_->setEnabled(true);
            btn_transcode_->setEnabled(true);
            bar_overall_->setValue(100);
            label_status_summary_->setText(QString::fromStdString(i18n.get(StringKey::StatusCompleted)));
            break;
    }
}

void MainWindow::handle_task_discovered(const TranscodeTask& task) {
    int row = table_tasks_->rowCount();
    table_tasks_->insertRow(row);

    QTableWidgetItem* item_num = new QTableWidgetItem(QString::number(row + 1));
    item_num->setData(Qt::UserRole, QString::fromStdString(task.task_id));
    item_num->setFlags(item_num->flags() | Qt::ItemIsUserCheckable);
    item_num->setCheckState(Qt::Checked);
    table_tasks_->setItem(row, 0, item_num);

    QTableWidgetItem* item_file = new QTableWidgetItem(QString::fromStdString(task.source_path.filename().string()));
    table_tasks_->setItem(row, 1, item_file);

    QTableWidgetItem* item_meta = new QTableWidgetItem("...");
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

    update_selection_counter();
}

void MainWindow::handle_task_updated(const TranscodeTask& task) {
    QString tid = QString::fromStdString(task.task_id);
    int row = find_row_by_task_id(tid);
    if (row < 0) return;

    auto* item_meta = table_tasks_ -> item(row,2);
    auto* item_status = table_tasks_ -> item(row,3);
    auto* item_details = table_tasks_ -> item(row,5);

    if (!item_meta || !item_status || !item_details) return;

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
    item_meta->setText(meta_str);

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
    item_status->setText(status_str);

    // Update progress bar cell widget
    QWidget* widget = table_tasks_->cellWidget(row, 4);
    if (auto* pbar = qobject_cast<QProgressBar*>(widget)) {
        pbar->setValue(static_cast<int>(task.progress));
    }

    if (task.error_message.has_value()) {
        item_details->setText(QString::fromStdString(task.error_message.value()));
    }

    // Update overall progress bar
    bar_overall_->setValue(static_cast<int>(coordinator_->overall_progress()));
}

void MainWindow::handle_summary(uint32_t total, uint32_t completed, uint32_t failed, double total_seconds) {
    auto& i18n = I18n::instance();
    label_status_summary_->setText(QString("%1 Total: %2 | Success: %3 | Failed: %4 | Time: %5s")
                                  .arg(QString::fromStdString(i18n.get(StringKey::StatusCompleted)))
                                  .arg(total).arg(completed).arg(failed).arg(total_seconds, 0, 'f', 2));
}
