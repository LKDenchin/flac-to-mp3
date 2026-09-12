#include "transcoder_agent.hpp"
#include "decryptor_agent.hpp"
#include <iostream>
#include <vector>
#include <cmath>

static std::string av_err_to_string(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, AV_ERROR_MAX_STRING_SIZE);
    return std::string(errbuf);
}

#include "metadata_agent.hpp"

bool TranscoderAgent::transcode(TranscodeTask& task,
                                OutputFormat format,
                                BitrateProfile profile,
                                TaskProgressCallback progress_cb,
                                std::string& out_error) {
    // 0. Decrypt file if source is encrypted (.ncm, .qmc, etc.)
    std::filesystem::path actual_source;
    if (!DecryptorAgent::prepare_audio_source(task.source_path, actual_source, task.metadata, out_error)) {
        return false;
    }

    // Extract metadata & lyrics from decrypted stream or FLAC source
    MetadataAgent::extract_metadata(actual_source, task.metadata);

    // 1. Open input file
    AVFormatContext* raw_in_ctx = nullptr;
    int ret = avformat_open_input(&raw_in_ctx, actual_source.c_str(), nullptr, nullptr);
    if (ret < 0) {
        out_error = "Failed to open input file: " + av_err_to_string(ret);
        return false;
    }
    AVFormatInputContextPtr in_ctx(raw_in_ctx);

    ret = avformat_find_stream_info(in_ctx.get(), nullptr);
    if (ret < 0) {
        out_error = "Failed to find stream info: " + av_err_to_string(ret);
        return false;
    }

    int audio_stream_idx = -1;
    const AVCodec* decoder = nullptr;
    audio_stream_idx = av_find_best_stream(in_ctx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (audio_stream_idx < 0 || !decoder) {
        out_error = "No audio stream found in input file.";
        return false;
    }

    AVStream* in_stream = in_ctx->streams[audio_stream_idx];
    AVCodecContextPtr dec_ctx(avcodec_alloc_context3(decoder));
    if (!dec_ctx) {
        out_error = "Failed to allocate decoder context.";
        return false;
    }

    ret = avcodec_parameters_to_context(dec_ctx.get(), in_stream->codecpar);
    if (ret < 0) {
        out_error = "Failed to copy decoder parameters: " + av_err_to_string(ret);
        return false;
    }

    ret = avcodec_open2(dec_ctx.get(), decoder, nullptr);
    if (ret < 0) {
        out_error = "Failed to open decoder: " + av_err_to_string(ret);
        return false;
    }

    double total_duration_sec = 0.0;
    if (in_stream->duration != AV_NOPTS_VALUE) {
        total_duration_sec = in_stream->duration * av_q2d(in_stream->time_base);
    } else if (in_ctx->duration != AV_NOPTS_VALUE) {
        total_duration_sec = static_cast<double>(in_ctx->duration) / AV_TIME_BASE;
    }
    task.duration_seconds = total_duration_sec;

    // 2. Select Encoder according to OutputFormat
    const AVCodec* encoder = nullptr;
    const char* format_name = "mp3";

    if (format == OutputFormat::FLAC) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_FLAC);
        format_name = "flac";
    } else if (format == OutputFormat::ALAC) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_ALAC);
        format_name = "ipod";
    } else if (format == OutputFormat::WAV) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
        format_name = "wav";
    } else {
        encoder = avcodec_find_encoder_by_name("libmp3lame");
        if (!encoder) {
            encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
        }
        format_name = "mp3";
    }

    if (!encoder) {
        out_error = "Target audio encoder not found.";
        return false;
    }

    AVCodecContextPtr enc_ctx(avcodec_alloc_context3(encoder));
    if (!enc_ctx) {
        out_error = "Failed to allocate encoder context.";
        return false;
    }

    int target_sample_rate = dec_ctx->sample_rate;
    if (target_sample_rate <= 0) {
        target_sample_rate = 44100;
    } else if (format == OutputFormat::MP3 && target_sample_rate > 48000) {
        target_sample_rate = (target_sample_rate % 48000 == 0) ? 48000 : 44100;
    }
    enc_ctx->sample_rate = target_sample_rate;

    AVChannelLayout out_ch_layout;
    if (dec_ctx->ch_layout.nb_channels == 1) {
        out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else {
        out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    }
    ret = av_channel_layout_copy(&enc_ctx->ch_layout, &out_ch_layout);
    if (ret < 0) {
        out_error = "Failed to copy output channel layout.";
        return false;
    }

    // Select sample format supported by encoder
    enum AVSampleFormat selected_sample_fmt = AV_SAMPLE_FMT_S16P;
    const enum AVSampleFormat* sample_fmts = nullptr;
    int num_sample_fmts = 0;
    int cfg_ret = avcodec_get_supported_config(enc_ctx.get(), encoder, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                               reinterpret_cast<const void**>(&sample_fmts), &num_sample_fmts);
    if (cfg_ret >= 0 && sample_fmts) {
        selected_sample_fmt = sample_fmts[0];
        for (int i = 0; (num_sample_fmts > 0 ? i < num_sample_fmts : sample_fmts[i] != AV_SAMPLE_FMT_NONE); ++i) {
            if (sample_fmts[i] == dec_ctx->sample_fmt) {
                selected_sample_fmt = sample_fmts[i];
                break;
            } else if (sample_fmts[i] == AV_SAMPLE_FMT_S16P || sample_fmts[i] == AV_SAMPLE_FMT_S16) {
                selected_sample_fmt = sample_fmts[i];
            }
        }
    }
    enc_ctx->sample_fmt = selected_sample_fmt;

    if (format == OutputFormat::MP3) {
        switch (profile) {
            case BitrateProfile::CBR_320K: enc_ctx->bit_rate = 320000; break;
            case BitrateProfile::CBR_256K: enc_ctx->bit_rate = 256000; break;
            case BitrateProfile::CBR_192K: enc_ctx->bit_rate = 192000; break;
            case BitrateProfile::VBR_V0:
                enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
                enc_ctx->global_quality = 0 * FF_QP2LAMBDA;
                break;
        }
        enc_ctx->compression_level = 0;
    } else if (format == OutputFormat::FLAC) {
        enc_ctx->compression_level = 5;
    }

    if (in_ctx->oformat && (in_ctx->oformat->flags & AVFMT_GLOBALHEADER)) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_open2(enc_ctx.get(), encoder, nullptr);
    if (ret < 0) {
        out_error = "Failed to open audio encoder: " + av_err_to_string(ret);
        return false;
    }

    // 3. Prepare output file & format context
    std::filesystem::create_directories(task.target_path.parent_path());

    AVFormatContext* raw_out_ctx = nullptr;
    ret = avformat_alloc_output_context2(&raw_out_ctx, nullptr, format_name, task.target_path.c_str());
    if (ret < 0 || !raw_out_ctx) {
        out_error = "Failed to allocate output format context: " + av_err_to_string(ret);
        return false;
    }
    AVFormatOutputContextPtr out_ctx(raw_out_ctx);

    AVStream* out_stream = avformat_new_stream(out_ctx.get(), nullptr);
    if (!out_stream) {
        out_error = "Failed to create output stream.";
        return false;
    }

    ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx.get());
    if (ret < 0) {
        out_error = "Failed to copy encoder parameters to output stream.";
        return false;
    }
    out_stream->time_base = {1, enc_ctx->sample_rate};

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_ctx->pb, task.target_path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            out_error = "Failed to open output file for writing: " + av_err_to_string(ret);
            return false;
        }
    }

    ret = avformat_write_header(out_ctx.get(), nullptr);
    if (ret < 0) {
        out_error = "Failed to write output header: " + av_err_to_string(ret);
        return false;
    }

    // 4. Setup Resampler (SwrContext)
    SwrContext* raw_swr = nullptr;
    ret = swr_alloc_set_opts2(&raw_swr,
                              &enc_ctx->ch_layout,
                              enc_ctx->sample_fmt,
                              enc_ctx->sample_rate,
                              &dec_ctx->ch_layout,
                              dec_ctx->sample_fmt,
                              dec_ctx->sample_rate,
                              0, nullptr);
    if (ret < 0 || !raw_swr) {
        out_error = "Failed to allocate SwrContext: " + av_err_to_string(ret);
        return false;
    }
    SwrContextPtr swr_ctx(raw_swr);

    ret = swr_init(swr_ctx.get());
    if (ret < 0) {
        out_error = "Failed to initialize SwrContext: " + av_err_to_string(ret);
        return false;
    }

    // 5. Setup Audio FIFO
    int frame_size = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 1024;
    AVAudioFifoPtr fifo(av_audio_fifo_alloc(enc_ctx->sample_fmt, enc_ctx->ch_layout.nb_channels, frame_size * 4));
    if (!fifo) {
        out_error = "Failed to allocate AVAudioFifo.";
        return false;
    }

    // Helper lambdas for encoding buffered FIFO frames
    int64_t next_pts = 0;
    auto encode_and_write_frame = [&](AVFrame* frame_to_encode) -> bool {
        if (frame_to_encode) {
            frame_to_encode->pts = next_pts;
            next_pts += frame_to_encode->nb_samples;
        }

        int send_ret = avcodec_send_frame(enc_ctx.get(), frame_to_encode);
        if (send_ret < 0) {
            out_error = "Error sending frame to encoder: " + av_err_to_string(send_ret);
            return false;
        }

        while (send_ret >= 0) {
            AVPacketPtr out_pkt(av_packet_alloc());
            int rec_ret = avcodec_receive_packet(enc_ctx.get(), out_pkt.get());
            if (rec_ret == AVERROR(EAGAIN) || rec_ret == AVERROR_EOF) {
                break;
            } else if (rec_ret < 0) {
                out_error = "Error receiving packet from encoder: " + av_err_to_string(rec_ret);
                return false;
            }

            av_packet_rescale_ts(out_pkt.get(), enc_ctx->time_base, out_stream->time_base);
            out_pkt->stream_index = out_stream->index;

            int write_ret = av_interleaved_write_frame(out_ctx.get(), out_pkt.get());
            if (write_ret < 0) {
                out_error = "Error writing packet to container: " + av_err_to_string(write_ret);
                return false;
            }
        }
        return true;
    };

    auto pop_and_encode_fifo = [&](bool flush_all) -> bool {
        while (av_audio_fifo_size(fifo.get()) >= frame_size || (flush_all && av_audio_fifo_size(fifo.get()) > 0)) {
            int current_frame_size = std::min(av_audio_fifo_size(fifo.get()), frame_size);
            AVFramePtr enc_frame(av_frame_alloc());
            if (!enc_frame) return false;

            enc_frame->nb_samples = current_frame_size;
            ret = av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
            if (ret < 0) return false;
            enc_frame->format = enc_ctx->sample_fmt;
            enc_frame->sample_rate = enc_ctx->sample_rate;

            ret = av_frame_get_buffer(enc_frame.get(), 0);
            if (ret < 0) return false;

            ret = av_audio_fifo_read(fifo.get(), reinterpret_cast<void**>(enc_frame->data), current_frame_size);
            if (ret < current_frame_size) return false;

            if (!encode_and_write_frame(enc_frame.get())) {
                return false;
            }
        }
        return true;
    };

    // 6. Transcoding Loop
    AVPacketPtr in_pkt(av_packet_alloc());
    AVFramePtr dec_frame(av_frame_alloc());

    while (av_read_frame(in_ctx.get(), in_pkt.get()) >= 0) {
        if (in_pkt->stream_index == audio_stream_idx) {
            ret = avcodec_send_packet(dec_ctx.get(), in_pkt.get());
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                // Ignore corrupt packet, continue
                av_packet_unref(in_pkt.get());
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx.get(), dec_frame.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_packet_unref(in_pkt.get());
                    out_error = "Error receiving frame from decoder: " + av_err_to_string(ret);
                    return false;
                }

                // Resample samples
                int max_dst_samples = av_rescale_rnd(swr_get_delay(swr_ctx.get(), dec_ctx->sample_rate) + dec_frame->nb_samples,
                                                      enc_ctx->sample_rate, dec_ctx->sample_rate, AV_ROUND_UP);

                uint8_t** resampled_data = nullptr;
                int resampled_linesize = 0;
                ret = av_samples_alloc_array_and_samples(&resampled_data, &resampled_linesize,
                                                          enc_ctx->ch_layout.nb_channels,
                                                          max_dst_samples, enc_ctx->sample_fmt, 0);
                if (ret < 0) {
                    av_frame_unref(dec_frame.get());
                    av_packet_unref(in_pkt.get());
                    out_error = "Failed to allocate resample buffer.";
                    return false;
                }

                int dst_samples = swr_convert(swr_ctx.get(),
                                              resampled_data, max_dst_samples,
                                              const_cast<const uint8_t**>(dec_frame->data), dec_frame->nb_samples);
                if (dst_samples > 0) {
                    if (av_audio_fifo_size(fifo.get()) + dst_samples > av_audio_fifo_size(fifo.get())) {
                        int r = av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + dst_samples);
                        (void)r;
                    }
                    av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(resampled_data), dst_samples);
                }

                av_freep(&resampled_data[0]);
                av_freep(&resampled_data);

                if (!pop_and_encode_fifo(false)) {
                    av_frame_unref(dec_frame.get());
                    av_packet_unref(in_pkt.get());
                    return false;
                }

                // Progress update calculation
                if (progress_cb && total_duration_sec > 0.0) {
                    double current_sec = dec_frame->pts != AV_NOPTS_VALUE
                        ? dec_frame->pts * av_q2d(in_stream->time_base)
                        : 0.0;
                    float pct = static_cast<float>((current_sec / total_duration_sec) * 100.0);
                    if (pct < 0.0f) pct = 0.0f;
                    if (pct > 99.0f) pct = 99.0f;
                    progress_cb(task.task_id, pct);
                }

                av_frame_unref(dec_frame.get());
            }
        }
        av_packet_unref(in_pkt.get());
    }

    // 7. Flush decoder
    avcodec_send_packet(dec_ctx.get(), nullptr);
    while (avcodec_receive_frame(dec_ctx.get(), dec_frame.get()) >= 0) {
        int max_dst_samples = av_rescale_rnd(swr_get_delay(swr_ctx.get(), dec_ctx->sample_rate) + dec_frame->nb_samples,
                                              enc_ctx->sample_rate, dec_ctx->sample_rate, AV_ROUND_UP);
        uint8_t** resampled_data = nullptr;
        int resampled_linesize = 0;
        if (av_samples_alloc_array_and_samples(&resampled_data, &resampled_linesize,
                                                 enc_ctx->ch_layout.nb_channels,
                                                 max_dst_samples, enc_ctx->sample_fmt, 0) >= 0) {
            int dst_samples = swr_convert(swr_ctx.get(), resampled_data, max_dst_samples,
                                          const_cast<const uint8_t**>(dec_frame->data), dec_frame->nb_samples);
            if (dst_samples > 0) {
                int r = av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + dst_samples);
                (void)r;
                av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(resampled_data), dst_samples);
            }
            av_freep(&resampled_data[0]);
            av_freep(&resampled_data);
        }
        av_frame_unref(dec_frame.get());
    }

    // Flush resampler buffer
    int extra_dst_samples = swr_get_delay(swr_ctx.get(), dec_ctx->sample_rate);
    if (extra_dst_samples > 0) {
        uint8_t** resampled_data = nullptr;
        int resampled_linesize = 0;
        if (av_samples_alloc_array_and_samples(&resampled_data, &resampled_linesize,
                                                 enc_ctx->ch_layout.nb_channels,
                                                 extra_dst_samples, enc_ctx->sample_fmt, 0) >= 0) {
            int dst_samples = swr_convert(swr_ctx.get(), resampled_data, extra_dst_samples, nullptr, 0);
            if (dst_samples > 0) {
                int r = av_audio_fifo_realloc(fifo.get(), av_audio_fifo_size(fifo.get()) + dst_samples);
                (void)r;
                av_audio_fifo_write(fifo.get(), reinterpret_cast<void**>(resampled_data), dst_samples);
            }
            av_freep(&resampled_data[0]);
            av_freep(&resampled_data);
        }
    }

    // Flush FIFO & encode remaining frames
    if (!pop_and_encode_fifo(true)) {
        return false;
    }

    // Flush encoder
    if (!encode_and_write_frame(nullptr)) {
        return false;
    }

    // Write container trailer
    av_write_trailer(out_ctx.get());

    if (progress_cb) {
        progress_cb(task.task_id, 100.0f);
    }

    return true;
}
