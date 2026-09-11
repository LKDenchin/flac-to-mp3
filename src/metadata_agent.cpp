#include "metadata_agent.hpp"
#include <taglib/flacfile.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacpicture.h>
#include <taglib/tag.h>
#include <algorithm>

bool MetadataAgent::extract_metadata(const std::filesystem::path& flac_path, AudioMetadata& out_metadata) {
    TagLib::FLAC::File flac_file(flac_path.c_str());
    if (!flac_file.isValid()) {
        return false;
    }

    TagLib::Tag* tag = flac_file.tag();
    if (tag) {
        out_metadata.title = tag->title().to8Bit(true);
        out_metadata.artist = tag->artist().to8Bit(true);
        out_metadata.album = tag->album().to8Bit(true);
        out_metadata.genre = tag->genre().to8Bit(true);
        out_metadata.year = (tag->year() > 0) ? std::to_string(tag->year()) : "";
        out_metadata.track_number = tag->track();
    }

    // Extract FLAC picture block
    const auto& pic_list = flac_file.pictureList();
    if (!pic_list.isEmpty()) {
        TagLib::FLAC::Picture* pic = pic_list.front();
        if (pic) {
            TagLib::ByteVector pic_bytes = pic->data();
            if (!pic_bytes.isEmpty()) {
                const auto* data_ptr = reinterpret_cast<const uint8_t*>(pic_bytes.data());
                size_t data_size = pic_bytes.size();
                out_metadata.cover_image_bytes.assign(data_ptr, data_ptr + data_size);
                out_metadata.cover_mime_type = pic->mimeType().to8Bit(true);
            }
        }
    }

    if (out_metadata.title.empty()) {
        apply_fallback_metadata(flac_path, out_metadata);
    }

    return true;
}

bool MetadataAgent::inject_metadata(const std::filesystem::path& mp3_path, const AudioMetadata& metadata) {
    TagLib::MPEG::File mp3_file(mp3_path.c_str());
    if (!mp3_file.isValid()) {
        return false;
    }

    TagLib::ID3v2::Tag* id3v2 = mp3_file.ID3v2Tag(true);
    if (!id3v2) {
        return false;
    }

    if (!metadata.title.empty()) {
        id3v2->setTitle(TagLib::String(metadata.title, TagLib::String::UTF8));
    }
    if (!metadata.artist.empty()) {
        id3v2->setArtist(TagLib::String(metadata.artist, TagLib::String::UTF8));
    }
    if (!metadata.album.empty()) {
        id3v2->setAlbum(TagLib::String(metadata.album, TagLib::String::UTF8));
    }
    if (!metadata.genre.empty()) {
        id3v2->setGenre(TagLib::String(metadata.genre, TagLib::String::UTF8));
    }
    if (!metadata.year.empty()) {
        try {
            id3v2->setYear(static_cast<unsigned int>(std::stoul(metadata.year)));
        } catch (...) {}
    }
    if (metadata.track_number > 0) {
        id3v2->setTrack(metadata.track_number);
    }

    // Embed album artwork APIC frame
    if (!metadata.cover_image_bytes.empty()) {
        // Remove existing APIC frames
        TagLib::ID3v2::FrameList existing_apic = id3v2->frameList("APIC");
        for (auto frame : existing_apic) {
            id3v2->removeFrame(frame, true);
        }

        auto* frame = new TagLib::ID3v2::AttachedPictureFrame();
        frame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
        std::string mime = metadata.cover_mime_type.empty() ? "image/jpeg" : metadata.cover_mime_type;
        frame->setMimeType(TagLib::String(mime, TagLib::String::UTF8));
        TagLib::ByteVector img_data(reinterpret_cast<const char*>(metadata.cover_image_bytes.data()),
                                    static_cast<unsigned int>(metadata.cover_image_bytes.size()));
        frame->setPicture(img_data);
        id3v2->addFrame(frame);
    }

    return mp3_file.save(TagLib::MPEG::File::AllTags);
}

void MetadataAgent::apply_fallback_metadata(const std::filesystem::path& file_path, AudioMetadata& metadata) {
    std::string stem = file_path.stem().string();
    size_t dash_pos = stem.find(" - ");
    if (dash_pos != std::string::npos) {
        if (metadata.artist.empty()) {
            metadata.artist = stem.substr(0, dash_pos);
        }
        if (metadata.title.empty()) {
            metadata.title = stem.substr(dash_pos + 3);
        }
    } else {
        if (metadata.title.empty()) {
            metadata.title = stem;
        }
    }
}
