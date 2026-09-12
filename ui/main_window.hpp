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

#include "i18n.hpp"

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
    void on_select_all();
    void on_deselect_all();
    void on_item_changed(QTableWidgetItem* item);
    void on_language_changed(int index);

    // Safe UI update slots
    void handle_state_change(AppState state);
    void handle_task_discovered(const TranscodeTask& task);
    void handle_task_updated(const TranscodeTask& task);
    void handle_summary(uint32_t total, uint32_t completed, uint32_t failed, double total_seconds);

private:
    void setup_ui();
    void apply_stylesheet();
    void retranslate_ui();
    int find_row_by_task_id(const QString& task_id);
    void update_selection_counter();

    // UI Widgets
    QLabel* lbl_source_;
    QLineEdit* edit_source_;
    QPushButton* btn_browse_source_;
    QLabel* lbl_target_;
    QLineEdit* edit_target_;
    QPushButton* btn_browse_target_;
    QLabel* lbl_format_;
    QComboBox* combo_format_;
    QLabel* lbl_bitrate_;
    QComboBox* combo_bitrate_;
    QLabel* lbl_threads_;
    QSpinBox* spin_threads_;
    QLabel* lbl_language_;
    QComboBox* combo_language_;

    QPushButton* btn_scan_;
    QPushButton* btn_transcode_;
    QPushButton* btn_cancel_;
    QPushButton* btn_select_all_;
    QPushButton* btn_deselect_all_;
    QLabel* lbl_selection_count_;

    QTableWidget* table_tasks_;
    QProgressBar* bar_overall_;
    QLabel* label_status_summary_;

    // Backend Coordinator
    std::unique_ptr<Coordinator> coordinator_;
};
