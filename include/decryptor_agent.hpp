#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include "task_models.hpp"

class DecryptorAgent {
public:
    // Checks if the file is an encrypted format (.ncm, .qmc, .mflac, .qmcflac, .mgg)
    static bool is_encrypted_file(const std::filesystem::path& file_path);

    // Decrypts file to a temporary file path or output path if encrypted,
    // returning true on success and updating out_decrypted_path & out_metadata (if embedded in header).
    static bool prepare_audio_source(const std::filesystem::path& source_path,
                                    std::filesystem::path& out_decrypted_path,
                                    AudioMetadata& out_metadata,
                                    std::string& out_error);

private:
    static bool decrypt_ncm(const std::filesystem::path& source_path,
                            const std::filesystem::path& target_path,
                            AudioMetadata& out_metadata,
                            std::string& out_error);

    static bool decrypt_qmc(const std::filesystem::path& source_path,
                            const std::filesystem::path& target_path,
                            std::string& out_error);
};
