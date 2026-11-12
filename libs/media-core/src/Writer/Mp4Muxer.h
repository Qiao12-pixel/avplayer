//
// Created by Codex on 26-3-28.
//

#ifndef MP4MUXER_H
#define MP4MUXER_H

#include "Muxer.h"

namespace av {
    class Mp4Muxer final : public Muxer {
    protected:
        bool AllocateOutputContext(const std::string& filePath) override;
    };
}

#endif //MP4MUXER_H
