//
// Created by Joe on 26-3-27.
//

#include "StickerFilter.h"

namespace av {
    StickerFilter::StickerFilter() : VideoFilter(VideoFilterType::kSticker) {
        SetInt("enabled", 0);
        SetInt("visible", 0);
        SetFloat("center_x", 0.5f);
        SetFloat("center_y", 0.16f);
        SetFloat("width", 0.24f);
        SetFloat("height", 0.16f);
        SetFloat("rotation", 0.0f);
        SetFloat("opacity", 1.0f);
    }
}
