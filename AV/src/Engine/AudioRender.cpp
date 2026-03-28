//
// Created by Joe on 26-3-27.
//

#include "AudioRender.h"

#include <QAudioDevice>
#include <QMediaDevices>

#include <iostream>

namespace av {
    AudioRender::AudioRender() = default;

    AudioRender::~AudioRender() {
        Stop();
    }

    bool AudioRender::Init(unsigned int channels, unsigned int sampleRate) {
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull()) {
            std::cerr << "未找到默认音频输出设备，将退化为纯视频时钟" << std::endl;
            return false;
        }
        std::cout << "[AudioRender] using default audio device" << std::endl;

        QAudioFormat requestedFormat;
        requestedFormat.setChannelCount(static_cast<int>(channels));
        requestedFormat.setSampleRate(static_cast<int>(sampleRate));
        requestedFormat.setSampleFormat(QAudioFormat::Int16);

        if (device.isFormatSupported(requestedFormat)) {
            m_format = requestedFormat;
        } else {
            m_format = device.preferredFormat();
            std::cerr << "默认音频设备不支持 2ch/44100Hz/Int16，回退到设备首选格式: "
                      << m_format.channelCount() << "ch/"
                      << m_format.sampleRate() << "Hz" << std::endl;
        }

        m_audioSink = std::make_unique<QAudioSink>(device, m_format);
        m_audioSink->setBufferSize(64 * 1024);
        m_outputDevice = m_audioSink->start();
        if (!m_outputDevice) {
            std::cerr << "failed to start audio sink output device" << std::endl;
            return false;
        }
        std::cout << "[AudioRender] audio sink started format="
                  << m_format.channelCount() << "ch/"
                  << m_format.sampleRate() << "Hz" << std::endl;
        m_started = true;
        ResetClockState();
        return true;
    }

    void AudioRender::Reset() {
        if (!m_audioSink) {
            ResetClockState();
            return;
        }

        m_audioSink->stop();
        m_audioSink->reset();
        m_outputDevice = m_audioSink->start();
        m_started = (m_outputDevice != nullptr);
        ResetClockState();
    }

    void AudioRender::PushSamples(const std::shared_ptr<IAudioSamples>& samples) {
        static int unavailableCount = 0;
        if (!samples) {
            std::cout << "[AudioRender] received null audio samples" << std::endl;
            return;
        }
        if (samples->flags & AVFrameFlag::KFlush) {
            std::cout << "[AudioRender] received flush samples" << std::endl;
            Flush();
            return;
        }
        if (!m_audioSink) {
            std::cout << "[AudioRender] audio sink unavailable" << std::endl;
            return;
        }
        if (!m_outputDevice) {
            ++unavailableCount;
            if (unavailableCount <= 5 || unavailableCount % 200 == 0) {
                std::cout << "[AudioRender] output device unavailable, try restart #" << unavailableCount << std::endl;
            }
            m_outputDevice = m_audioSink->start();
            if (!m_outputDevice) {
                return;
            }
            m_started = true;
        }
        if (samples->pcmData.empty()) {
            std::cout << "[AudioRender] pcmData empty for ts=" << samples->GetTimeStamp() << std::endl;
            return;
        }
        if (!m_started.load()) {
            m_outputDevice = m_audioSink->start();
            if (!m_outputDevice) {
                std::cout << "[AudioRender] failed to restart output device" << std::endl;
                return;
            }
            m_started = true;
        }

        {
            std::lock_guard<std::mutex> lock(m_clockMutex);
            if (m_clockBasePts < 0.0) {
                m_clockBasePts = samples->GetTimeStamp();
            }
        }

        const char* rawData = reinterpret_cast<const char*>(samples->pcmData.data());
        const qsizetype rawSize = static_cast<qsizetype>(samples->pcmData.size() * sizeof(int16_t));
        static int audioWriteCount = 0;
        ++audioWriteCount;
        if (audioWriteCount <= 3 || audioWriteCount % 200 == 0) {
            std::cout << "[AudioRender] writing audio #" << audioWriteCount
                      << " bytes=" << rawSize
                      << " ts=" << samples->GetTimeStamp() << std::endl;
        }
        qsizetype written = 0;
        while (written < rawSize) {
            const qint64 current = m_outputDevice->write(rawData + written, rawSize - written);
            if (current <= 0) {
                std::cout << "[AudioRender] output device write stalled after "
                          << written << " bytes" << std::endl;
                break;
            }
            written += static_cast<qsizetype>(current);
        }
    }

    void AudioRender::Flush() {
        Reset();
    }

    void AudioRender::Stop() {
        if (m_audioSink) {
            m_audioSink->stop();
        }
        m_outputDevice = nullptr;
        m_started = false;
        ResetClockState();
    }

    double AudioRender::GetClockInSeconds() const {
        if (!m_audioSink || !m_started.load()) {
            return -1.0;
        }

        std::lock_guard<std::mutex> lock(m_clockMutex);
        if (m_clockBasePts < 0.0) {
            return -1.0;
        }
        return m_clockBasePts + static_cast<double>(m_audioSink->processedUSecs()) / 1000000.0;
    }

    bool AudioRender::HasValidClock() const {
        return GetClockInSeconds() >= 0.0;
    }

    void AudioRender::ResetClockState() {
        std::lock_guard<std::mutex> lock(m_clockMutex);
        m_clockBasePts = -1.0;
    }
}
