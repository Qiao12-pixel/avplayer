//
// Created by Joe on 26-3-27.
//

#include "GrayFilter.h"

namespace av {
    GrayFilter::GrayFilter() : VideoFilter(VideoFilterType::kGray) {
        SetInt("enabled", 0);
    }
}
