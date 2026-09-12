#include "i18n.hpp"

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

I18n::I18n() {
    auto add = [this](StringKey key, const std::string& cn, const std::string& tw, const std::string& en_str, const std::string& ja_str) {
        strings_[key][Language::zh_CN] = cn;
        strings_[key][Language::zh_TW] = tw;
        strings_[key][Language::en] = en_str;
        strings_[key][Language::ja] = ja_str;
    };

    add(StringKey::AppTitle, "音频格式转换与元数据处理工具", "音訊格式轉換與元數據處理工具", "Audio Transcoder & Tagging Tool", "音声フォーマット変換＆タグ編集ツール");
    add(StringKey::SourceFolder, "源文件夹:", "來源資料夾:", "Source Folder:", "元フォルダ:");
    add(StringKey::TargetFolder, "目标文件夹:", "目標資料夾:", "Target Folder:", "保存先フォルダ:");
    add(StringKey::SourcePlaceholder, "选择包含音频或加密文件的目录...", "選擇包含音訊或加密檔案的目錄...", "Select directory containing audio or encrypted files...", "音声または暗号化ファイルが含まれるディレクトリを選択...");
    add(StringKey::TargetPlaceholder, "选择保存转换后文件的目录...", "選擇儲存轉換後檔案的目錄...", "Select directory to save converted files...", "変換後のファイルを保存するディレクトリを選択...");
    add(StringKey::Browse, "浏览...", "瀏覽...", "Browse...", "参照...");
    add(StringKey::OutputFormat, "输出格式:", "輸出格式:", "Output Format:", "出力フォーマット:");
    add(StringKey::BitrateProfile, "比特率预设:", "位元率預設:", "Bitrate Profile:", "ビットレート設定:");
    add(StringKey::WorkerThreads, "工作线程数:", "工作執行緒數:", "Worker Threads:", "スレッド数:");
    add(StringKey::LanguageSelect, "界面语言:", "介面語言:", "Language:", "言語:");

    add(StringKey::ScanFiles, "扫描文件", "掃描檔案", "Scan Files", "ファイルをスキャン");
    add(StringKey::StartTranscode, "开始转换", "開始轉換", "Start Transcode", "変換開始");
    add(StringKey::Reset, "重置", "重設", "Reset", "リセット");
    add(StringKey::SelectAll, "全选", "全選", "Select All", "すべて選択");
    add(StringKey::DeselectAll, "取消全选", "取消全選", "Deselect All", "選択解除");
    add(StringKey::SelectedCount, "已选择", "已選擇", "Selected", "選択済み");

    add(StringKey::HeaderSelectNum, "选择 / #", "選擇 / #", "Select / #", "選択 / #");
    add(StringKey::HeaderFileName, "文件名", "檔案名稱", "File Name", "ファイル名");
    add(StringKey::HeaderTitleArtist, "标题与艺术家", "標題與演出者", "Title & Artist", "タイトルとアーティスト");
    add(StringKey::HeaderStatus, "状态", "狀態", "Status", "ステータス");
    add(StringKey::HeaderProgress, "进度", "進度", "Progress", "進捗");
    add(StringKey::HeaderDetails, "详情 / 错误", "詳情 / 錯誤", "Details / Error", "詳細 / エラー");

    add(StringKey::StatusIdle, "状态: 就绪。请选择源文件夹开始扫描。", "狀態: 就緒。請選擇來源資料夾開始掃描。", "Status: Idle. Select source directory to scan.", "ステータス: 待機中。スキャンするフォルダを選択してください。");
    add(StringKey::StatusScanning, "状态: 正在扫描目录中的音频与加密文件...", "狀態: 正在掃描目錄中的音訊與加密檔案...", "Status: Scanning directory for audio and encrypted files...", "ステータス: ディレクトリ内のファイルをスキャン中...");
    add(StringKey::StatusReady, "状态: 扫描完成。共找到 %1 个文件。准备转换。", "狀態: 掃描完成。共找到 %1 個檔案。準備轉換。", "Status: Scan finished. Found %1 file(s). Ready to transcode.", "ステータス: スキャン完了。%1 件のファイルが見つかりました。");
    add(StringKey::StatusTranscoding, "状态: 正在批量转换处理中...", "狀態: 正在批次轉換處理中...", "Status: Batch transcoding in progress...", "ステータス: バッチ変換中...");
    add(StringKey::StatusCompleted, "状态: 批量转换完成！", "狀態: 批次轉換完成！", "Status: Batch Transcode Complete!", "ステータス: 変換が完了しました！");
    add(StringKey::StatusReset, "状态: 重置完成。就绪。", "狀態: 重設完成。就緒。", "Status: Reset complete. Ready.", "ステータス: リセット完了。");

    add(StringKey::FmtMP3, "MP3 (高品质)", "MP3 (高品質)", "MP3 (High Quality)", "MP3 (高品質)");
    add(StringKey::FmtFLAC, "FLAC (无损原音)", "FLAC (無損原音)", "FLAC (Lossless)", "FLAC (ロスレス)");
    add(StringKey::FmtALAC, "ALAC / M4A (苹果无损)", "ALAC / M4A (蘋果無損)", "ALAC / M4A (Apple Lossless)", "ALAC / M4A (Apple ロスレス)");
    add(StringKey::FmtWAV, "WAV (未压缩 PCM)", "WAV (未壓縮 PCM)", "WAV (Uncompressed PCM)", "WAV (非圧縮 PCM)");

    add(StringKey::Bitrate320k, "CBR 320 kbps (High Quality)", "CBR 320 kbps (高品質)", "CBR 320 kbps (High Quality)", "CBR 320 kbps (高品質)");
    add(StringKey::Bitrate256k, "CBR 256 kbps", "CBR 256 kbps", "CBR 256 kbps", "CBR 256 kbps");
    add(StringKey::Bitrate192k, "CBR 192 kbps", "CBR 192 kbps", "CBR 192 kbps", "CBR 192 kbps");
    add(StringKey::BitrateV0, "VBR V0 (Extreme Quality)", "VBR V0 (極致品質)", "VBR V0 (Extreme Quality)", "VBR V0 (最高品質)");
}

std::string I18n::get(StringKey key) const {
    return get(key, current_lang_);
}

std::string I18n::get(StringKey key, Language lang) const {
    auto it = strings_.find(key);
    if (it != strings_.end()) {
        auto lang_it = it->second.find(lang);
        if (lang_it != it->second.end()) {
            return lang_it->second;
        }
    }
    return "";
}
