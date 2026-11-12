#include "WebRtcAudioSink.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>

#include <opus/opus.h>

extern "C" {
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "Define/AudioCompat.h"

namespace av {

namespace {
constexpr int kOpusSampleRate = 48000;
constexpr int kOpusChannels = 2;
constexpr int kOpusFrameSamples = 960; // 20 ms at 48 kHz
constexpr int kOpusBitrate = 64000;
}

WebRtcAudioSink::WebRtcAudioSink(std::shared_ptr<rtc::Track> audioTrack)
    : m_audioTrack(std::move(audioTrack)) {
    int error = 0;
    m_encoder = opus_encoder_create(kOpusSampleRate, kOpusChannels, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK || !m_encoder) {
        std::cerr << "[WebRtcAudioSink] failed to create Opus encoder: "
                  << opus_strerror(error) << std::endl;
        m_encoder = nullptr;
        return;
    }
    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(kOpusBitrate));
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
}

WebRtcAudioSink::~WebRtcAudioSink() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ResetLocked();
    if (m_encoder) {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }
}

void WebRtcAudioSink::PushSamples(const std::shared_ptr<IAudioSamples>& samples) {
    if (!samples) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (samples->flags & AVFrameFlag::KFlush) {
        ResetLocked();
        return;
    }
    if (!m_encoder || samples->pcmData.empty() || samples->channels == 0 || samples->sampleRate == 0) {
        return;
    }
    if (!m_audioTrack || !m_audioTrack->isOpen()) {
        // Drop until the peer connection has negotiated the track; otherwise
        // the Opus timestamps would run ahead of the first audible frame.
        return;
    }

    if (m_inputChannels != samples->channels || m_inputSampleRate != samples->sampleRate) {
        if (m_resampler) {
            swr_free(&m_resampler);
            m_resampler = nullptr;
        }
        m_resampler = CreateResampler(kOpusChannels, AV_SAMPLE_FMT_S16, kOpusSampleRate,
                                      static_cast<int>(samples->channels), AV_SAMPLE_FMT_S16,
                                      static_cast<int>(samples->sampleRate));
        if (!m_resampler || swr_init(m_resampler) < 0) {
            std::cerr << "[WebRtcAudioSink] failed to init resampler" << std::endl;
            if (m_resampler) swr_free(&m_resampler);
            return;
        }
        m_inputChannels = samples->channels;
        m_inputSampleRate = samples->sampleRate;
    }

    if (!m_ptsInitialized) {
        m_nextPtsSeconds = samples->GetTimeStamp();
        m_ptsInitialized = true;
    }

    const int inputSamples = static_cast<int>(samples->pcmData.size() / samples->channels);
    if (inputSamples <= 0) return;

    const int maxOutputSamples = static_cast<int>(av_rescale_rnd(
            swr_get_delay(m_resampler, samples->sampleRate) + inputSamples,
            kOpusSampleRate, samples->sampleRate, AV_ROUND_UP)) + 64;
    std::vector<int16_t> converted(static_cast<size_t>(maxOutputSamples) * kOpusChannels);
    uint8_t* outputData[1] = {reinterpret_cast<uint8_t*>(converted.data())};
    const uint8_t* inputData[1] = {reinterpret_cast<const uint8_t*>(samples->pcmData.data())};

    const int convertedSamples = swr_convert(m_resampler, outputData, maxOutputSamples,
                                             inputData, inputSamples);
    if (convertedSamples <= 0) return;

    m_pcm48k.insert(m_pcm48k.end(), converted.begin(),
                    converted.begin() + static_cast<size_t>(convertedSamples) * kOpusChannels);
    EncodeBufferedFramesLocked();
}

void WebRtcAudioSink::EncodeBufferedFramesLocked() {
    const size_t frameValues = static_cast<size_t>(kOpusFrameSamples) * kOpusChannels;
    unsigned char packet[4000];
    while (m_pcm48k.size() >= frameValues) {
        const int encoded = opus_encode(m_encoder, m_pcm48k.data(), kOpusFrameSamples,
                                        packet, static_cast<opus_int32>(sizeof(packet)));
        if (encoded < 0) {
            std::cerr << "[WebRtcAudioSink] opus_encode failed: " << opus_strerror(encoded) << std::endl;
            m_pcm48k.erase(m_pcm48k.begin(), m_pcm48k.begin() + static_cast<ptrdiff_t>(frameValues));
            continue;
        }
        if (encoded > 0 && m_audioTrack && m_audioTrack->isOpen()) {
            try {
                rtc::binary payload(reinterpret_cast<std::byte*>(packet),
                                    reinterpret_cast<std::byte*>(packet) + encoded);
                m_audioTrack->sendFrame(std::move(payload),
                                        rtc::FrameInfo(std::chrono::duration<double>(m_nextPtsSeconds)));
            } catch (const std::exception& error) {
                std::cerr << "[WebRtcAudioSink] send failed: " << error.what() << std::endl;
            }
        }
        m_pcm48k.erase(m_pcm48k.begin(), m_pcm48k.begin() + static_cast<ptrdiff_t>(frameValues));
        m_nextPtsSeconds += static_cast<double>(kOpusFrameSamples) / kOpusSampleRate;
    }
}

void WebRtcAudioSink::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ResetLocked();
}

void WebRtcAudioSink::ResetLocked() {
    m_pcm48k.clear();
    m_ptsInitialized = false;
    m_nextPtsSeconds = 0.0;
}

}
