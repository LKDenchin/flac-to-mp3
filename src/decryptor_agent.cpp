#include "decryptor_agent.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

extern "C" {
#include <libavutil/aes.h>
#include <libavutil/mem.h>
}

static const uint8_t ncm_core_key[16] = {
    0x68, 0x7A, 0x48, 0x52, 0x41, 0x6D, 0x35, 0x6B, 0x49, 0x6E, 0x62, 0x18, 0x57
}; // "hZHRams5kInbaxW" -> 0x68, 0x7a, 0x48, 0x52, 0x41, 0x6d, 0x35, 0x6b, 0x49, 0x6e, 0x62, 0x61, 0x78, 0x57

static const uint8_t ncm_meta_key[16] = {
    0x23, 0x31, 0x34, 0x36, 0x6C, 0x6A, 0x6B, 0x5F, 0x21, 0x5C, 0x5D, 0x26, 0x30, 0x55, 0x3C, 0x27
}; // "#146ljk_!\\]&0U<'"

static void aes128_ecb_decrypt(const uint8_t key[16], const uint8_t* in, uint8_t* out, size_t len) {
    AVAES* aes = av_aes_alloc();
    if (!aes) return;
    av_aes_init(aes, key, 128, 1);
    for (size_t i = 0; i < len; i += 16) {
        av_aes_crypt(aes, out + i, in + i, 1, nullptr, 1);
    }
    av_freep(&aes);
}

bool DecryptorAgent::is_encrypted_file(const std::filesystem::path& file_path) {
    std::string ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".ncm" || ext == ".qmc" || ext == ".qmc0" || ext == ".qmc3" ||
            ext == ".qmcflac" || ext == ".mflac" || ext == ".mgg");
}

bool DecryptorAgent::prepare_audio_source(const std::filesystem::path& source_path,
                                           std::filesystem::path& out_decrypted_path,
                                           AudioMetadata& out_metadata,
                                           std::string& out_error) {
    if (!is_encrypted_file(source_path)) {
        out_decrypted_path = source_path;
        return true;
    }

    std::string ext = source_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Create temp directory for decrypted payload
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "flac_to_mp3_cache";
    std::filesystem::create_directories(temp_dir);

    if (ext == ".ncm") {
        std::filesystem::path temp_out = temp_dir / (source_path.stem().string() + "_decrypted.flac");
        if (decrypt_ncm(source_path, temp_out, out_metadata, out_error)) {
            out_decrypted_path = temp_out;
            return true;
        }
    } else {
        std::filesystem::path temp_out = temp_dir / (source_path.stem().string() + "_decrypted.flac");
        if (decrypt_qmc(source_path, temp_out, out_error)) {
            out_decrypted_path = temp_out;
            return true;
        }
    }

    return false;
}

bool DecryptorAgent::decrypt_ncm(const std::filesystem::path& source_path,
                                  const std::filesystem::path& target_path,
                                  AudioMetadata& out_metadata,
                                  std::string& out_error) {
    std::ifstream in(source_path, std::ios::binary);
    if (!in.is_open()) {
        out_error = "Failed to open NCM file: " + source_path.string();
        return false;
    }

    // 1. Verify Magic Header "CTENFDAM"
    char magic[8];
    in.read(magic, 8);
    if (std::memcmp(magic, "CTENFDAM", 8) != 0) {
        out_error = "Invalid NCM file header.";
        return false;
    }

    // Skip 2 bytes gap
    in.seekg(2, std::ios::cur);

    // 2. Read AES Key Box length
    uint32_t key_len = 0;
    in.read(reinterpret_cast<char*>(&key_len), 4);
    if (key_len <= 0 || key_len > 1024 * 1024) {
        out_error = "Corrupt NCM key length.";
        return false;
    }

    std::vector<uint8_t> raw_key(key_len);
    in.read(reinterpret_cast<char*>(raw_key.data()), key_len);
    for (size_t i = 0; i < key_len; ++i) {
        raw_key[i] ^= 0x64;
    }

    uint8_t actual_ncm_key[16] = {0x68, 0x7a, 0x48, 0x52, 0x41, 0x6d, 0x35, 0x6b, 0x49, 0x6e, 0x62, 0x61, 0x78, 0x57, 0x00, 0x00};
    std::vector<uint8_t> decrypted_key(key_len);
    aes128_ecb_decrypt(actual_ncm_key, raw_key.data(), decrypted_key.data(), key_len);

    // Strip "neteasecloudmusic" prefix (17 bytes)
    size_t key_data_offset = 17;
    size_t key_data_len = (decrypted_key.size() > key_data_offset) ? (decrypted_key.size() - key_data_offset) : 0;
    const uint8_t* key_data = decrypted_key.data() + key_data_offset;

    // 3. Build S-Box
    uint8_t key_box[256];
    for (int i = 0; i < 256; i++) key_box[i] = static_cast<uint8_t>(i);
    uint8_t last_byte = 0;
    uint8_t key_offset = 0;
    if (key_data_len > 0) {
        for (int i = 0; i < 256; i++) {
            uint8_t swap = key_box[i];
            uint8_t c = (swap + last_byte + key_data[key_offset]) & 0xff;
            key_offset = static_cast<uint8_t>((key_offset + 1) % key_data_len);
            key_box[i] = key_box[c];
            key_box[c] = swap;
            last_byte = c;
        }
    }

    // 4. Read Metadata block
    uint32_t meta_len = 0;
    in.read(reinterpret_cast<char*>(&meta_len), 4);
    if (meta_len > 0) {
        in.seekg(meta_len, std::ios::cur); // Skip raw metadata string
    }

    // Skip CRC / gap bytes (5 bytes)
    in.seekg(5, std::ios::cur);

    // 5. Read Album Cover image length
    uint32_t img_len = 0;
    in.read(reinterpret_cast<char*>(&img_len), 4);
    if (img_len > 0 && img_len < 20 * 1024 * 1024) {
        out_metadata.cover_image_bytes.resize(img_len);
        in.read(reinterpret_cast<char*>(out_metadata.cover_image_bytes.data()), img_len);
        out_metadata.cover_mime_type = "image/jpeg";
    }

    // 6. Decrypt Audio Payload Stream
    std::ofstream out(target_path, std::ios::binary);
    if (!out.is_open()) {
        out_error = "Failed to create output file for decrypted NCM audio.";
        return false;
    }

    constexpr size_t BUF_SIZE = 65536;
    std::vector<uint8_t> buffer(BUF_SIZE);
    size_t total_read = 0;

    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), BUF_SIZE);
        size_t read_bytes = in.gcount();
        if (read_bytes == 0) break;

        for (size_t i = 0; i < read_bytes; ++i) {
            size_t idx = (total_read + i + 1) & 0xff;
            uint8_t mask = key_box[(key_box[idx] + key_box[(key_box[idx] + idx) & 0xff]) & 0xff];
            buffer[i] ^= mask;
        }

        out.write(reinterpret_cast<char*>(buffer.data()), read_bytes);
        total_read += read_bytes;
    }

    return true;
}

bool DecryptorAgent::decrypt_qmc(const std::filesystem::path& source_path,
                                  const std::filesystem::path& target_path,
                                  std::string& out_error) {
    std::ifstream in(source_path, std::ios::binary);
    if (!in.is_open()) {
        out_error = "Failed to open QMC file: " + source_path.string();
        return false;
    }

    std::ofstream out(target_path, std::ios::binary);
    if (!out.is_open()) {
        out_error = "Failed to create output file for decrypted QMC audio.";
        return false;
    }

    static const uint8_t qmc_key[128] = {
        0x77, 0x48, 0x32, 0x73, 0xDE, 0xF2, 0xC0, 0xC8, 0x95, 0xEC, 0x9B, 0x2E, 0x4B, 0x7E, 0xAE, 0x41,
        0xA1, 0x0D, 0xBC, 0x6E, 0x9B, 0x50, 0x57, 0x92, 0x0E, 0xFE, 0x6D, 0xDE, 0x96, 0x3F, 0x46, 0x7D,
        0x4B, 0x5E, 0x9F, 0x48, 0x89, 0xB7, 0x4A, 0x61, 0xA6, 0x09, 0x1A, 0x63, 0xB3, 0xC8, 0xC2, 0x37,
        0xA1, 0x77, 0xA9, 0x4D, 0x3A, 0x4F, 0x7B, 0x45, 0x6C, 0xAA, 0x3D, 0xA2, 0x80, 0x71, 0x4D, 0x42,
        0x6C, 0xBA, 0x7D, 0x6B, 0x6B, 0xE7, 0x75, 0x08, 0xD4, 0x81, 0xF4, 0x3F, 0x1F, 0xB2, 0xA0, 0x08,
        0x9B, 0xED, 0x41, 0xC6, 0xB9, 0x3A, 0xC5, 0x9C, 0x53, 0x0C, 0x77, 0xD2, 0x52, 0x91, 0x6B, 0xFB,
        0x6A, 0xC5, 0x76, 0x22, 0x85, 0xED, 0x02, 0xAE, 0x80, 0xC2, 0xDE, 0x97, 0x82, 0x2B, 0x0B, 0x80,
        0x4F, 0xFC, 0x74, 0xB8, 0x5A, 0x86, 0x92, 0xF5, 0x21, 0xD9, 0xAE, 0x3C, 0xC7, 0x61, 0x1D, 0x82
    };

    constexpr size_t BUF_SIZE = 65536;
    std::vector<uint8_t> buffer(BUF_SIZE);
    size_t pos = 0;

    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), BUF_SIZE);
        size_t read_bytes = in.gcount();
        if (read_bytes == 0) break;

        for (size_t i = 0; i < read_bytes; ++i) {
            buffer[i] ^= qmc_key[(pos + i) % 128];
        }

        out.write(reinterpret_cast<char*>(buffer.data()), read_bytes);
        pos += read_bytes;
    }

    return true;
}
