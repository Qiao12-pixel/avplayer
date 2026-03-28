//
// Created by Codex on 26-3-28.
//

#include "AudioEncoder.h"

#include <algorithm>
#include <iostream>

namespace av {
    namespace {
        constexpr int kAudioBitrate = 128'000;
    }

    IAudioEncoder* IAudioEncoder::Create() {
        return new AudioEncoder();
    }

    AudioEncoder::AudioEncoder() = default;

    AudioEncoder::~AudioEncoder() {
        Close();
    }

    void AudioEncoder::SetListener(Listener* listener) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listener = listener;
    }

    bool AudioEncoder::Open(const RecorderConfig& config, bool globalHeader) {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupLocked();

        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec) {
            std::cerr << "[AudioEncoder] no available AAC encoder" << std::endl;
            return false;
        }

        m_codecContext = avcodec_alloc_context3(codec);
        if (!m_codecContext) {
            return false;
        }

        m_codecContext->codec_type = AVMEDIA_TYPE_AUDIO;
        m_codecContext->codec_id = codec->id;
        m_codecContext->sample_rate = config.audioSampleRate;
        m_codecContext->channels = config.audioChannels;
        m_codecContext->channel_layout = av_get_default_channel_layout(config.audioChannels);
        m_codecContext->bit_rate = kAudioBitrate;
        m_codecContext->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        m_codecContext->time_base = AVRational{1, config.audioSampleRate};
        if (globalHeader) {
            m_codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        const int openResult = avcodec_open2(m_codecContext, codec, nullptr);
        if (openResult < 0) {
            LogError("open audio encoder failed", openResult);
            Close();
            return false;
        }

        m_swrContext = swr_alloc_set_opts(nullptr,
                                          m_codecContext->channel_layout,
                                          m_codecContext->sample_fmt,
                                          m_codecContext->sample_rate,
                                          av_get_default_channel_layout(config.audioChannels),
                                          AV_SAMPLE_FMT_S16,
                                          config.audioSampleRate,
                                          0,
                                          nullptr);
        if (!m_swrContext || swr_init(m_swrContext) < 0) {
            std::cerr << "[AudioEncoder] init resampler failed" << std::endl;
            Close();
            return false;
        }

        m_audioFifo = av_audio_fifo_alloc(m_codecContext->sample_fmt,
                                          m_codecContext->channels,
                                          std::max(m_codecContext->frame_size * 4, 4096));
        if (!m_audioFifo) {
            std::cerr << "[AudioEncoder] alloc audio fifo failed" << std::endl;
            Close();
            return false;
        }
        return true;
    }

    void AudioEncoder::Encode(const std::shared_ptr<IAudioSamples>& audioSamples, int64_t recordingStartUs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_codecContext || !m_swrContext || !m_audioFifo || !audioSamples || recordingStartUs == AV_NOPTS_VALUE) {
            return;
        }
        if (audioSamples->pcmData.empty() || audioSamples->channels == 0 || audioSamples->sampleRate == 0) {
            return;
        }

        const int64_t audioTimestampUs = GetTimestampUs(audioSamples->pts, audioSamples->timebaseNum, audioSamples->timebaseDen);
        int inputSamples = static_cast<int>(audioSamples->pcmData.size() / audioSamples->channels);
        int dropSamples = 0;
        if (audioTimestampUs != AV_NOPTS_VALUE && audioTimestampUs < recordingStartUs) {
            const int64_t leadingUs = recordingStartUs - audioTimestampUs;
            dropSamples = static_cast<int>(av_rescale_rnd(leadingUs, audioSamples->sampleRate, AV_TIME_BASE, AV_ROUND_UP));
            dropSamples = std::clamp(dropSamples, 0, inputSamples);
            inputSamples -= dropSamples;
        }
        if (inputSamples <= 0) {
            return;
        }

        const int16_t* inputPcm = audioSamples->pcmData.data() + static_cast<size_t>(dropSamples) * audioSamples->channels;
        const uint8_t* inputData[1] = {reinterpret_cast<const uint8_t*>(inputPcm)};
        const int maxOutputSamples = av_rescale_rnd(
                swr_get_delay(m_swrContext, audioSamples->sampleRate) + inputSamples,
                m_codecContext->sample_rate,
                audioSamples->sampleRate,
                AV_ROUND_UP);

        uint8_t** convertedData = nullptr;
        int lineSize = 0;
        if (av_samples_alloc_array_and_samples(&convertedData,
                                               &lineSize,
                                               m_codecContext->channels,
                                               maxOutputSamples,
                                               m_codecContext->sample_fmt,
                                               0) < 0) {
            return;
        }

        const int convertedSamples = swr_convert(m_swrContext,
                                                 convertedData,
                                                 maxOutputSamples,
                                                 inputData,
                                                 inputSamples);
        if (convertedSamples > 0) {
            if (!m_audioPtsInitialized) {
                const int64_t adjustedAudioUs = std::max<int64_t>(recordingStartUs,
                                                                  audioTimestampUs + av_rescale_q(dropSamples,
                                                                                                  AVRational{1, static_cast<int>(audioSamples->sampleRate)},
                                                                                                  AV_TIME_BASE_Q));
                const int64_t relativeAudioUs = std::max<int64_t>(0, adjustedAudioUs - recordingStartUs);
                m_nextAudioPts = av_rescale_q(relativeAudioUs, AV_TIME_BASE_Q, m_codecContext->time_base);
                m_audioPtsInitialized = true;
            }
            if (av_audio_fifo_realloc(m_audioFifo, av_audio_fifo_size(m_audioFifo) + convertedSamples) >= 0) {
                av_audio_fifo_write(m_audioFifo, reinterpret_cast<void**>(convertedData), convertedSamples);
                DrainAudioFifo(false);
            }
        }

        if (convertedData) {
            av_freep(&convertedData[0]);
            av_freep(&convertedData);
        }
    }

    void AudioEncoder::Flush() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_codecContext) {
            return;
        }
        DrainAudioFifo(true);
        EmitPackets(nullptr);
    }

    void AudioEncoder::Close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        CleanupLocked();
    }

    void AudioEncoder::CleanupLocked() {
        if (m_audioFifo) {
            av_audio_fifo_free(m_audioFifo);
            m_audioFifo = nullptr;
        }
        if (m_swrContext) {
            swr_free(&m_swrContext);
        }
        if (m_codecContext) {
            avcodec_free_context(&m_codecContext);
        }
        ResetState();
    }

    AVCodecContext* AudioEncoder::GetCodecContext() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_codecContext;
    }

    void AudioEncoder::DrainAudioFifo(bool flushAll) {
        if (!m_codecContext || !m_audioFifo) {
            return;
        }
        const int frameSize = m_codecContext->frame_size > 0 ? m_codecContext->frame_size : 1024;
        while (av_audio_fifo_size(m_audioFifo) >= frameSize || (flushAll && av_audio_fifo_size(m_audioFifo) > 0)) {
            const int samplesToWrite = flushAll ? std::min(av_audio_fifo_size(m_audioFifo), frameSize) : frameSize;
            AVFrame* frame = av_frame_alloc();
            if (!frame) {
                return;
            }
            frame->nb_samples = samplesToWrite;
            frame->format = m_codecContext->sample_fmt;
            frame->channel_layout = m_codecContext->channel_layout;
            frame->sample_rate = m_codecContext->sample_rate;
            frame->pts = m_nextAudioPts;
            if (av_frame_get_buffer(frame, 0) < 0) {
                av_frame_free(&frame);
                return;
            }
            av_audio_fifo_read(m_audioFifo, reinterpret_cast<void**>(frame->data), samplesToWrite);
            m_nextAudioPts += samplesToWrite;
            EmitPackets(frame);
            av_frame_free(&frame);
        }
    }

    bool AudioEncoder::EmitPackets(AVFrame* frame) {
        const int sendResult = avcodec_send_frame(m_codecContext, frame);
        if (sendResult < 0) {
            LogError("send audio frame failed", sendResult);
            return false;
        }

        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            return false;
        }

        while (true) {
            const int receiveResult = avcodec_receive_packet(m_codecContext, packet);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                LogError("receive audio packet failed", receiveResult);
                av_packet_free(&packet);
                return false;
            }
            auto packetHolder = std::make_shared<IAVPacket>(packet);
            packetHolder->timeBase = m_codecContext->time_base;
            if (m_listener) {
                m_listener->OnNotifyAudioPacket(packetHolder);
            }
            packet = av_packet_alloc();
            if (!packet) {
                return false;
            }
        }
        av_packet_free(&packet);
        return true;
    }

    int64_t AudioEncoder::GetTimestampUs(int64_t pts, int32_t timebaseNum, int32_t timebaseDen) const {
        if (pts == AV_NOPTS_VALUE || timebaseNum <= 0 || timebaseDen <= 0) {
            return AV_NOPTS_VALUE;
        }
        return av_rescale_q(pts, AVRational{timebaseNum, timebaseDen}, AV_TIME_BASE_Q);
    }

    void AudioEncoder::ResetState() {
        m_nextAudioPts = 0;
        m_audioPtsInitialized = false;
    }

    void AudioEncoder::LogError(const char* prefix, int errorCode) const {
        char errorBuffer[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(errorCode, errorBuffer, sizeof(errorBuffer));
        std::cerr << "[AudioEncoder] " << prefix << ": " << errorBuffer << std::endl;
    }
}
