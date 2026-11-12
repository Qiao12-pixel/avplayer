//
// Created by Codex on 26-3-27.
//

#ifndef STICKERPLACEMENTESTIMATOR_H
#define STICKERPLACEMENTESTIMATOR_H

#include "IFaceLandmarkDetector.h"

namespace av {
    enum class StickerAnchorMode {
        kHeadTop,
        kEyeMask
    };

    struct StickerPlacement {
        bool visible{false};
        float centerX{0.5f};
        float centerY{0.2f};
        float width{0.35f};
        float height{0.22f};
        float rotation{0.0f};
        float opacity{1.0f};
    };

    class StickerPlacementEstimator {
    public:
        StickerPlacement Estimate(const FaceLandmarkResult& result,
                                  int frameWidth,
                                  int frameHeight,
                                  StickerAnchorMode mode) const;
    };
}

#endif //STICKERPLACEMENTESTIMATOR_H
