//
// Created by Codex on 26-3-28.
//

#include "Mp4Muxer.h"

namespace av {
    bool Mp4Muxer::AllocateOutputContext(const std::string& filePath) {
        return avformat_alloc_output_context2(&m_formatContext, nullptr, "mp4", filePath.c_str()) >= 0 &&
               m_formatContext != nullptr;
    }
}
