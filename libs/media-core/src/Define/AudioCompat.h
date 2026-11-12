#ifndef AV_DEFINE_AUDIOCOMPAT_H
#define AV_DEFINE_AUDIOCOMPAT_H

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

namespace av {

// FFmpeg 6 replaced AVCodecContext::channels/channel_layout with the
// AVChannelLayout API. Keep the shared media core buildable against both the
// macOS Conan FFmpeg 4.4 package and the FFmpeg 6+ packages shipped by current
// Linux distributions.
#if LIBAVCODEC_VERSION_MAJOR >= 60

inline int ChannelCount(const AVCodecContext* context) {
    return context->ch_layout.nb_channels;
}

inline void SetChannelLayout(AVCodecContext* context, int channels) {
    av_channel_layout_default(&context->ch_layout, channels);
}

inline SwrContext* CreateResampler(int outChannels, AVSampleFormat outFormat, int outRate,
                                   int inChannels, AVSampleFormat inFormat, int inRate) {
    AVChannelLayout outLayout{};
    AVChannelLayout inLayout{};
    av_channel_layout_default(&outLayout, outChannels);
    av_channel_layout_default(&inLayout, inChannels);
    SwrContext* context = nullptr;
    if (swr_alloc_set_opts2(&context, &outLayout, outFormat, outRate,
                            &inLayout, inFormat, inRate, 0, nullptr) < 0) {
        return nullptr;
    }
    return context;
}

inline void SetFrameChannelLayout(AVFrame* frame, const AVCodecContext* context) {
    av_channel_layout_copy(&frame->ch_layout, &context->ch_layout);
}

#else

inline int ChannelCount(const AVCodecContext* context) {
    return context->channels;
}

inline void SetChannelLayout(AVCodecContext* context, int channels) {
    context->channels = channels;
    context->channel_layout = av_get_default_channel_layout(channels);
}

inline SwrContext* CreateResampler(int outChannels, AVSampleFormat outFormat, int outRate,
                                   int inChannels, AVSampleFormat inFormat, int inRate) {
    return swr_alloc_set_opts(nullptr,
                              av_get_default_channel_layout(outChannels), outFormat, outRate,
                              av_get_default_channel_layout(inChannels), inFormat, inRate,
                              0, nullptr);
}

inline void SetFrameChannelLayout(AVFrame* frame, const AVCodecContext* context) {
    frame->channel_layout = context->channel_layout;
}

#endif

} // namespace av

#endif // AV_DEFINE_AUDIOCOMPAT_H
