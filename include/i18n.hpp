#pragma once

#include <string>
#include <unordered_map>

enum class Language {
    zh_CN, // 简体中文
    zh_TW, // 繁體中文
    en,    // English
    ja     // 日本語
};

enum class StringKey {
    AppTitle,
    SourceFolder,
    TargetFolder,
    SourcePlaceholder,
    TargetPlaceholder,
    Browse,
    OutputFormat,
    BitrateProfile,
    WorkerThreads,
    LanguageSelect,
    ScanFiles,
    StartTranscode,
    Reset,
    SelectAll,
    DeselectAll,
    SelectedCount,
    HeaderSelectNum,
    HeaderFileName,
    HeaderTitleArtist,
    HeaderStatus,
    HeaderProgress,
    HeaderDetails,
    StatusIdle,
    StatusScanning,
    StatusReady,
    StatusTranscoding,
    StatusCompleted,
    StatusReset,
    FmtMP3,
    FmtFLAC,
    FmtALAC,
    FmtWAV,
    Bitrate320k,
    Bitrate256k,
    Bitrate192k,
    BitrateV0
};

class I18n {
public:
    static I18n& instance();

    Language current_language() const { return current_lang_; }
    void set_language(Language lang) { current_lang_ = lang; }

    std::string get(StringKey key) const;
    std::string get(StringKey key, Language lang) const;

private:
    I18n();
    Language current_lang_{Language::zh_CN};
    std::unordered_map<StringKey, std::unordered_map<Language, std::string>> strings_;
};
