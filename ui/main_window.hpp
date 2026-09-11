#pragma once

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <memory>
#include "coordinator.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void on_browse_source();
    void on_browse_target();
    void on_start_scan();
    void on_start_transcode();
    void on_cancel_reset();

    // Safe UI update slots
    void handle_state_change(AppState state);
    void handle_task_discovered(const TranscodeTask& task);
    void handle_task_updated(const TranscodeTask& task);
    void handle_summary(uint32_t total, uint32_t completed, uint32_t failed, double total_seconds);

private:
    void setup_ui();
    void apply_stylesheet();
    int find_row_by_task_id(const QString& task_id);

    // UI Widgets
    QLineEdit* edit_source_;
    QPushButton* btn_browse_source_;
    QLineEdit* edit_target_;
    QPushButton* btn_browse_target_;
    QComboBox* combo_bitrate_;
    QSpinBox* spin_threads_;

    QPushButton* btn_scan_;
    QPushButton* btn_transcode_;
    QPushButton* btn_cancel_;

    QTableWidget* table_tasks_;
    QProgressBar* bar_overall_;
    QLabel* label_status_summary_;

    // Backend Coordinator
    std::unique_ptr<Coordinator> coordinator_;
};
