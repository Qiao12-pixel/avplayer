//
// Created by Joe on 26-3-27.
//

#include "FlipVerticalFilter.h"

namespace av {
    FlipVerticalFilter::FlipVerticalFilter() : VideoFilter(VideoFilterType::kFlipVertical) {
        SetInt("enabled", 0);
    }
}
