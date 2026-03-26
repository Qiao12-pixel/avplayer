//
// Created by Joe on 26-3-25.
//

#include "AudioDecoder.h"

#include <iostream>


namespace av {
    IAudioDecoder *IAudioDecoder::Create(unsigned int channels, unsigned int sampleRate) {
        return new AudioDecoder(channels, sampleRate);
    }
    AudioDecoder::AudioDecoder(unsigned int channels, unsigned int sampleRate) : m_targetChannels(channels), m_targetSampleRate(sampleRate){
        m_pipelineReleaseCallback = std::make_shared<std::function<void()>>([&]() {
           ReleaseAudioPipelineResource();
        });
        m_thread = std::thread(&AudioDecoder::ThreadLoop, this);
    }
    AudioDecoder::~AudioDecoder() {
        Stop();
        std::lock_guard<std::mutex> lock(m_codecContextMutex);
        CleanupContext();
    }
    void AudioDecoder::SetListener(Listener *listener) {
        std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
        m_listener = listener;
    }
    void AudioDecoder::SetStream(struct AVStream *stream) {
        //找编码器
        if (!stream) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            m_packetQueue.clear();
        }
        std::lock_guard<std::mutex> lock(m_codecContextMutex);
        CleanupContext();
        m_codecContext = avcodec_alloc_context3(nullptr);
        if (avcodec_parameters_to_context(m_codecContext, stream->codecpar) < 0) {
            std::cerr << "failed to give paramerters to context." << std::endl;
            return;
        }
        AVCodec* codec = avcodec_find_decoder(m_codecContext->codec_id);
        if (!codec) {
            std::cerr << "failed to find decoder " << std::endl;
            return;
        }
        if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
            std::cerr << "failed to open codec.," << std::endl;
            return;
        }
        //重采样
        m_swrContext = swr_alloc_set_opts(nullptr, av_get_default_channel_layout(m_targetChannels), AV_SAMPLE_FMT_S16, m_targetSampleRate,
                        av_get_default_channel_layout(m_codecContext->channels), m_codecContext->sample_fmt, m_codecContext->sample_rate,0, nullptr);

        if (!m_swrContext || swr_init(m_swrContext) < 0) {
            std::cerr << "failed to init swrcontext" << std::endl;
            return;
        }
        m_timeBase = stream->time_base;
    }
    //拿到packet=》入队
    void AudioDecoder::Decode(std::shared_ptr<IAVPacket> packet) {
        if (!packet) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_packetQueueMutex);
        if (packet->flags & AVFrameFlag::KFlush) {
            m_packetQueue.clear();
        }
        m_packetQueue.push_back(packet);//packet已经入对，此时唤醒解码线程
        m_notifier.Notify();
    }
    void AudioDecoder::Start() {
        m_paused = false;
        m_notifier.Notify();
    }
    void AudioDecoder::Pause() {
        m_paused = true;
    }
    void AudioDecoder::Stop() {
        m_abort = true;
        m_notifier.Notify();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void AudioDecoder::ThreadLoop() {
        for (;;) {
            m_notifier.Wait(100);
            if (m_abort == true) {
                break;
            }
            CheckFlushPacket();
            if (!m_paused && m_pipelineResourceCount > 0) {
                DecodeAVPacket();
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            m_packetQueue.clear();
        }
    }
    void AudioDecoder::CheckFlushPacket() {
        std::lock_guard<std::mutex> lock(m_packetQueueMutex);
        if (m_packetQueue.empty()) {
            return;
        }
        auto packet = m_packetQueue.front();
        if (packet->flags & AVFrameFlag::KFlush) {
            m_packetQueue.pop_front();
            avcodec_flush_buffers(m_codecContext);

            auto audioSamples = std::make_shared<IAudioSamples>();
            audioSamples->flags |= AVFrameFlag::KFlush;//此时为刷新帧
            std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
            if (m_listener) {
                m_listener->OnNotifyAudioSamples(audioSamples);
            }
        }
    }
    void AudioDecoder::DecodeAVPacket() {
        std::shared_ptr<IAVPacket> packet;
        {
            std::lock_guard<std::mutex> lock(m_packetQueueMutex);
            if (m_packetQueue.empty()) {
                return;
            }
            packet = m_packetQueue.front();
            m_packetQueue.pop_front();
        }
        if (packet->avPacket && avcodec_send_packet(m_codecContext, packet->avPacket) < 0) {
            std::cerr << "failed to send packet" << std::endl;
            return;
        }
        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            std::cerr << "failed to allocate frame" << std::endl;
            return;
        }
        while (avcodec_receive_frame(m_codecContext, frame)  >= 0) {
            //需要输出多少个重采样后的样本 =  (重采样器缓存的样本数 + 当前帧样本数) × 目标采样率 ÷ 源采样率     //向上取整
            int dst_nb_sample = av_rescale_rnd(
                swr_get_delay(m_swrContext, m_codecContext->sample_rate) + frame->nb_samples,
                m_targetSampleRate, m_codecContext->sample_rate, AV_ROUND_UP);
            int buffer_size = av_samples_get_buffer_size(nullptr, m_targetChannels, dst_nb_sample, AV_SAMPLE_FMT_S16, 1);
            uint8_t* buffer = (uint8_t*)av_malloc(buffer_size);
            int ret = swr_convert(m_swrContext, &buffer, dst_nb_sample, (const uint8_t**)frame->data, frame->nb_samples);
            if (ret < 0) {
                std::cerr << "failed to convert sampleRate" << std::endl;
                av_free(buffer);
                av_frame_free(&frame);
                return;
            }

            std::shared_ptr<IAudioSamples> samples = std::make_shared<IAudioSamples>();
            samples->channels = m_targetChannels;
            samples->sampleRate = m_targetSampleRate;
            samples->pts = frame->pts;
            samples->duration = frame->pkt_duration;
            samples->timebaseNum = m_timeBase.num;
            samples->timebaseDen = m_timeBase.den;
            samples->pcmData.assign((int16_t*)buffer, (int16_t*)(buffer + buffer_size));
            samples->releaseCallback = m_pipelineReleaseCallback;
            av_free(buffer);
            --m_pipelineResourceCount;
            std::lock_guard<std::recursive_mutex> lock(m_listenerMutex);
            if (m_listener) {
                m_listener->OnNotifyAudioSamples(samples);
            }
        }
        av_frame_free(&frame);
    }
    void AudioDecoder::ReleaseAudioPipelineResource() {
        ++m_pipelineResourceCount;
        m_notifier.Notify();
    }
    void AudioDecoder::CleanupContext() {
        if (m_codecContext) {
            avcodec_free_context(&m_codecContext);
        } if (m_swrContext) {
            swr_free(&m_swrContext);
        }
    }
















}
