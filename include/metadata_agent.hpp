#pragma once

#include <filesystem>
#include <string>
#include "task_models.hpp"

class MetadataAgent {
public:
    MetadataAgent() = default;
    ~MetadataAgent() = default;

    MetadataAgent(const MetadataAgent&) = delete;
    MetadataAgent& operator=(const MetadataAgent&) = delete;

    // Read metadata and picture from source FLAC file
    static bool extract_metadata(const std::filesystem::path& flac_path, AudioMetadata& out_metadata);

    // Write metadata and picture to target MP3 file (ID3v2 & APIC frame)
    static bool inject_metadata(const std::filesystem::path& mp3_path, const AudioMetadata& metadata);

    // Fallback: parse title & artist from filename if metadata is empty
    static void apply_fallback_metadata(const std::filesystem::path& file_path, AudioMetadata& metadata);
};
