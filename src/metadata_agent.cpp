#include "metadata_agent.hpp"
#include <taglib/flacfile.h>
#include <taglib/mpegfile.h>
#include <taglib/mp4file.h>
#include <taglib/fileref.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/xiphcomment.h>
#include <taglib/tpropertymap.h>
#include <taglib/flacpicture.h>
#include <taglib/tag.h>
#include <algorithm>
#include <fstream>

bool MetadataAgent::extract_metadata(const std::filesystem::path& flac_path, AudioMetadata& out_metadata) {
    std::string ext = flac_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".flac") {
        TagLib::FLAC::File flac_file(flac_path.c_str());
        if (flac_file.isValid()) {
            TagLib::Tag* tag = flac_file.tag();
            if (tag) {
                out_metadata.title = tag->title().to8Bit(true);
                out_metadata.artist = tag->artist().to8Bit(true);
                out_metadata.album = tag->album().to8Bit(true);
                out_metadata.genre = tag->genre().to8Bit(true);
                out_metadata.year = (tag->year() > 0) ? std::to_string(tag->year()) : "";
                out_metadata.track_number = tag->track();
            }

            TagLib::Ogg::XiphComment* xiph = flac_file.xiphComment();
            if (xiph) {
                const auto& map = xiph->fieldListMap();
                for (auto it = map.begin(); it != map.end(); ++it) {
                    std::string key = it->first.to8Bit(true);
                    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                    if (key == "lyrics" || key == "unsyncedlyrics" || key == "lyric" || key == "uslt") {
                        if (!it->second.isEmpty()) {
                            std::string val = it->second.front().to8Bit(true);
                            if (!val.empty()) {
                                out_metadata.lyrics = val;
                                break;
                            }
                        }
                    }
                }
            }

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
        }
    } else {
        TagLib::FileRef f(flac_path.c_str());
        if (!f.isNull() && f.tag()) {
            TagLib::Tag* tag = f.tag();
            out_metadata.title = tag->title().to8Bit(true);
            out_metadata.artist = tag->artist().to8Bit(true);
            out_metadata.album = tag->album().to8Bit(true);
            out_metadata.genre = tag->genre().to8Bit(true);
            out_metadata.year = (tag->year() > 0) ? std::to_string(tag->year()) : "";
            out_metadata.track_number = tag->track();
        }
    }

    if (out_metadata.lyrics.empty()) {
        TagLib::FileRef f_ref(flac_path.c_str());
        if (!f_ref.isNull() && f_ref.file()) {
            TagLib::PropertyMap props = f_ref.file()->properties();
            for (auto it = props.begin(); it != props.end(); ++it) {
                std::string key = it->first.to8Bit(true);
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                if (key == "lyrics" || key == "unsyncedlyrics" || key == "lyric" || key == "uslt") {
                    if (!it->second.isEmpty()) {
                        std::string val = it->second.front().to8Bit(true);
                        if (!val.empty()) {
                            out_metadata.lyrics = val;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Fallback: Check sidecar .lrc file in the same directory
    if (out_metadata.lyrics.empty()) {
        std::filesystem::path lrc_path = flac_path;
        lrc_path.replace_extension(".lrc");
        if (std::filesystem::exists(lrc_path)) {
            std::ifstream lrc_file(lrc_path, std::ios::binary);
            if (lrc_file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(lrc_file)),
                                     std::istreambuf_iterator<char>());
                out_metadata.lyrics = content;
            }
        }
    }

    if (out_metadata.title.empty()) {
        apply_fallback_metadata(flac_path, out_metadata);
    }

    return true;
}

bool MetadataAgent::inject_metadata(const std::filesystem::path& target_path, const AudioMetadata& metadata) {
    std::string ext = target_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".flac") {
        TagLib::FLAC::File flac_file(target_path.c_str());
        if (!flac_file.isValid()) return false;

        TagLib::Tag* tag = flac_file.tag();
        if (tag) {
            if (!metadata.title.empty()) tag->setTitle(TagLib::String(metadata.title, TagLib::String::UTF8));
            if (!metadata.artist.empty()) tag->setArtist(TagLib::String(metadata.artist, TagLib::String::UTF8));
            if (!metadata.album.empty()) tag->setAlbum(TagLib::String(metadata.album, TagLib::String::UTF8));
            if (!metadata.genre.empty()) tag->setGenre(TagLib::String(metadata.genre, TagLib::String::UTF8));
            if (!metadata.year.empty()) {
                try { tag->setYear(static_cast<unsigned int>(std::stoul(metadata.year))); } catch(...) {}
            }
            if (metadata.track_number > 0) tag->setTrack(metadata.track_number);
        }

        TagLib::Ogg::XiphComment* xiph = flac_file.xiphComment(true);
        if (xiph && !metadata.lyrics.empty()) {
            xiph->addField("LYRICS", TagLib::String(metadata.lyrics, TagLib::String::UTF8), true);
            xiph->addField("UNSYNCEDLYRICS", TagLib::String(metadata.lyrics, TagLib::String::UTF8), true);
        }

        if (!metadata.cover_image_bytes.empty()) {
            flac_file.removePictures();
            auto* pic = new TagLib::FLAC::Picture();
            pic->setType(TagLib::FLAC::Picture::FrontCover);
            std::string mime = metadata.cover_mime_type.empty() ? "image/jpeg" : metadata.cover_mime_type;
            pic->setMimeType(TagLib::String(mime, TagLib::String::UTF8));
            TagLib::ByteVector img_data(reinterpret_cast<const char*>(metadata.cover_image_bytes.data()),
                                        static_cast<unsigned int>(metadata.cover_image_bytes.size()));
            pic->setData(img_data);
            flac_file.addPicture(pic);
        }
        return flac_file.save();
    } else if (ext == ".m4a") {
        TagLib::MP4::File mp4_file(target_path.c_str());
        if (!mp4_file.isValid()) return false;

        TagLib::Tag* tag = mp4_file.tag();
        if (tag) {
            if (!metadata.title.empty()) tag->setTitle(TagLib::String(metadata.title, TagLib::String::UTF8));
            if (!metadata.artist.empty()) tag->setArtist(TagLib::String(metadata.artist, TagLib::String::UTF8));
            if (!metadata.album.empty()) tag->setAlbum(TagLib::String(metadata.album, TagLib::String::UTF8));
            if (!metadata.genre.empty()) tag->setGenre(TagLib::String(metadata.genre, TagLib::String::UTF8));
            if (!metadata.year.empty()) {
                try { tag->setYear(static_cast<unsigned int>(std::stoul(metadata.year))); } catch(...) {}
            }
            if (metadata.track_number > 0) tag->setTrack(metadata.track_number);
        }

        if (mp4_file.tag()) {
            if (!metadata.lyrics.empty()) {
                mp4_file.tag()->setItem("\xA9lyr", TagLib::MP4::Item(TagLib::StringList(TagLib::String(metadata.lyrics, TagLib::String::UTF8))));
            }
            if (!metadata.cover_image_bytes.empty()) {
                TagLib::ByteVector img_data(reinterpret_cast<const char*>(metadata.cover_image_bytes.data()),
                                            static_cast<unsigned int>(metadata.cover_image_bytes.size()));
                TagLib::MP4::CoverArt::Format fmt = TagLib::MP4::CoverArt::JPEG;
                if (metadata.cover_mime_type == "image/png") fmt = TagLib::MP4::CoverArt::PNG;
                TagLib::MP4::CoverArt art(fmt, img_data);
                TagLib::MP4::CoverArtList art_list;
                art_list.append(art);
                mp4_file.tag()->setItem("covr", TagLib::MP4::Item(art_list));
            }
        }
        return mp4_file.save();
    } else {
        TagLib::MPEG::File mp3_file(target_path.c_str());
        if (!mp3_file.isValid()) return false;

        TagLib::ID3v2::Tag* id3v2 = mp3_file.ID3v2Tag(true);
        if (!id3v2) return false;

        if (!metadata.title.empty()) id3v2->setTitle(TagLib::String(metadata.title, TagLib::String::UTF8));
        if (!metadata.artist.empty()) id3v2->setArtist(TagLib::String(metadata.artist, TagLib::String::UTF8));
        if (!metadata.album.empty()) id3v2->setAlbum(TagLib::String(metadata.album, TagLib::String::UTF8));
        if (!metadata.genre.empty()) id3v2->setGenre(TagLib::String(metadata.genre, TagLib::String::UTF8));
        if (!metadata.year.empty()) {
            try { id3v2->setYear(static_cast<unsigned int>(std::stoul(metadata.year))); } catch (...) {}
        }
        if (metadata.track_number > 0) id3v2->setTrack(metadata.track_number);

        if (!metadata.lyrics.empty()) {
            TagLib::ID3v2::FrameList existing_uslt = id3v2->frameList("USLT");
            for (auto frame : existing_uslt) id3v2->removeFrame(frame, true);

            auto* frame1 = new TagLib::ID3v2::UnsynchronizedLyricsFrame(TagLib::String::UTF8);
            frame1->setText(TagLib::String(metadata.lyrics, TagLib::String::UTF8));
            frame1->setLanguage(TagLib::ByteVector("eng", 3));
            id3v2->addFrame(frame1);

            auto* frame2 = new TagLib::ID3v2::UnsynchronizedLyricsFrame(TagLib::String::UTF8);
            frame2->setText(TagLib::String(metadata.lyrics, TagLib::String::UTF8));
            frame2->setLanguage(TagLib::ByteVector("XXX", 3));
            id3v2->addFrame(frame2);
        }

        if (!metadata.cover_image_bytes.empty()) {
            TagLib::ID3v2::FrameList existing_apic = id3v2->frameList("APIC");
            for (auto frame : existing_apic) id3v2->removeFrame(frame, true);

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

