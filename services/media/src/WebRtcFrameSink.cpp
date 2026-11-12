#include "WebRtcFrameSink.h"

#include <chrono>
#include <iostream>

#include "Writer/IRecorder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace av {

WebRtcFrameSink::WebRtcFrameSink(std::shared_ptr<rtc::Track> videoTrack)
    : m_videoTrack(std::move(videoTrack)), m_encoder(IVideoEncoder::Create()) {
    m_encoder->SetListener(this);
}

WebRtcFrameSink::~WebRtcFrameSink() {
    if (m_encoder) m_encoder->Close();
}

void WebRtcFrameSink::PushFrame(const std::shared_ptr<IVideoFrame>& frame) {
    if (!frame || !frame->data) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_opened) {
        RecorderConfig config;
        config.width = static_cast<int>(frame->width);
        config.height = static_cast<int>(frame->height);
        // globalHeader=false: SPS/PPS are embedded in-band (Annex-B) before
        // each IDR, which is what the RTP H264 depacketizer on the receiving
        // end expects rather than out-of-band extradata.
        m_opened = m_encoder->Open(config, /*globalHeader=*/false);
        if (!m_opened) {
            std::cerr << "[WebRtcFrameSink] failed to open video encoder" << std::endl;
            return;
        }
    }
    // recordingStartUs=0: we stream live relative to the frame's own
    // timestamps, there is no "recording start" wall-clock baseline here.
    m_encoder->Encode(frame, 0);
}

void WebRtcFrameSink::ClearFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_opened) m_encoder->Flush();
}

void WebRtcFrameSink::OnNotifyVideoPacket(std::shared_ptr<IAVPacket> packet) {
    if (!packet || !packet->avPacket || !m_videoTrack) return;
    if (!m_videoTrack->isOpen()) {
        std::cerr << "[WebRtcFrameSink] track not open yet, dropping packet" << std::endl;
        return;
    }
    const auto nalUnits = AnnexBToLengthPrefixed(packet->avPacket->data, packet->avPacket->size);
    if (nalUnits.empty()) return;
    try {
        // sendFrame (not send) advances the RTP timestamp from the encoded
        // frame's PTS, which the client uses for subtitle timing and A/V sync.
        const float packetTs = packet->GetTimeStamp();
        m_videoTrack->sendFrame(nalUnits,
                                rtc::FrameInfo(std::chrono::duration<double>(packetTs > 0.0f ? packetTs : 0.0)));
        std::cerr << "[WebRtcFrameSink] sent " << nalUnits.size() << " bytes" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "[WebRtcFrameSink] send failed: " << error.what() << std::endl;
    }
}

std::vector<std::byte> WebRtcFrameSink::AnnexBToLengthPrefixed(const uint8_t* data, int size) {
    std::vector<std::byte> output;
    output.reserve(static_cast<size_t>(size) + 16);

    auto findStartCode = [&](int from, int* codeLength) -> int {
        for (int i = from; i + 2 < size; ++i) {
            if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
                *codeLength = 3;
                return i;
            }
            if (i + 3 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
                *codeLength = 4;
                return i;
            }
        }
        return -1;
    };

    int codeLength = 0;
    int firstStart = findStartCode(0, &codeLength);
    if (firstStart < 0) return output;
    int nalStart = firstStart + codeLength;

    while (nalStart < size) {
        int nextCodeLength = 0;
        int nextStart = findStartCode(nalStart, &nextCodeLength);
        const int nalEnd = nextStart < 0 ? size : nextStart;
        const uint32_t nalLength = static_cast<uint32_t>(nalEnd - nalStart);
        if (nalLength > 0) {
            const uint32_t lengthBe = (nalLength << 24) | ((nalLength & 0xFF00) << 8) |
                                       ((nalLength & 0xFF0000) >> 8) | (nalLength >> 24);
            const auto* lengthBytes = reinterpret_cast<const std::byte*>(&lengthBe);
            output.insert(output.end(), lengthBytes, lengthBytes + 4);
            const auto* nalBytes = reinterpret_cast<const std::byte*>(data + nalStart);
            output.insert(output.end(), nalBytes, nalBytes + nalLength);
        }
        if (nextStart < 0) break;
        nalStart = nextStart + nextCodeLength;
    }
    return output;
}

}
