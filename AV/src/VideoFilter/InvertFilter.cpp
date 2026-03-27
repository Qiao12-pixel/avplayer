//
// Created by Joe on 26-3-27.
//

#include "InvertFilter.h"

namespace av {
    InvertFilter::InvertFilter() : VideoFilter(VideoFilterType::kInvert) {
        SetInt("enabled", 0);
    }
}
