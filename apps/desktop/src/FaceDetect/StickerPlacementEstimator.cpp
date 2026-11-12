//
// Created by Codex on 26-3-27.
//

#include "StickerPlacementEstimator.h"

#include <algorithm>
#include <cmath>

namespace av {
    namespace {
        FacePoint2f MidPoint(const FacePoint2f& a, const FacePoint2f& b) {
            return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
        }
    }

    StickerPlacement StickerPlacementEstimator::Estimate(const FaceLandmarkResult& result,
                                                         int frameWidth,
                                                         int frameHeight,
                                                         StickerAnchorMode mode) const {
        StickerPlacement placement;
        if (!result.valid || frameWidth <= 0 || frameHeight <= 0) {
            return placement;
        }

        const float faceCenterX = result.faceRect.x + result.faceRect.width * 0.5f;
        const float faceTopY = result.faceRect.y;

        float eyeCenterY = result.faceRect.y + result.faceRect.height * 0.38f;
        float eyeLeftX = result.faceRect.x + result.faceRect.width * 0.35f;
        float eyeRightX = result.faceRect.x + result.faceRect.width * 0.65f;

        if (result.landmarks.size() >= 46) {
            const auto& leftEyeOuter = result.landmarks[36];
            const auto& leftEyeInner = result.landmarks[39];
            const auto& rightEyeInner = result.landmarks[42];
            const auto& rightEyeOuter = result.landmarks[45];
            eyeLeftX = (leftEyeOuter.x + leftEyeInner.x) * 0.5f;
            eyeRightX = (rightEyeInner.x + rightEyeOuter.x) * 0.5f;
            eyeCenterY = (leftEyeOuter.y + leftEyeInner.y + rightEyeInner.y + rightEyeOuter.y) * 0.25f;
        }

        placement.visible = true;
        float rotation = 0.0f;
        if (result.landmarks.size() >= 46) {
            const auto& leftEyeOuter = result.landmarks[36];
            const auto& rightEyeOuter = result.landmarks[45];
            rotation = std::atan2(rightEyeOuter.y - leftEyeOuter.y, eyeRightX - eyeLeftX);
        }

        if (mode == StickerAnchorMode::kEyeMask) {
            const float eyeDistance = std::max(eyeRightX - eyeLeftX, result.faceRect.width * 0.24f);
            const float eyesCenterX = (eyeLeftX + eyeRightX) * 0.5f;
            const float eyesCenterY = eyeCenterY;
            const float stickerWidthPx = eyeDistance * 1.95f;
            const float stickerHeightPx = stickerWidthPx * 0.52f;
            placement.centerX = std::clamp(eyesCenterX / static_cast<float>(frameWidth), 0.0f, 1.0f);
            placement.centerY = std::clamp(eyesCenterY / static_cast<float>(frameHeight), 0.0f, 1.0f);
            placement.width = std::clamp(stickerWidthPx / static_cast<float>(frameWidth), 0.06f, 0.75f);
            placement.height = std::clamp(stickerHeightPx / static_cast<float>(frameHeight), 0.04f, 0.45f);
        } else {
            const float eyesCenterX = (eyeLeftX + eyeRightX) * 0.5f;
            const float faceWidth = result.faceRect.width;
            const float stickerWidthPx = faceWidth * 1.15f;
            const float stickerHeightPx = stickerWidthPx * 0.60f;
            const float centerYPx = std::min(eyeCenterY - result.faceRect.height * 0.55f,
                                             faceTopY - stickerHeightPx * 0.15f);
            placement.centerX = std::clamp(eyesCenterX / static_cast<float>(frameWidth), 0.0f, 1.0f);
            placement.centerY = std::clamp(centerYPx / static_cast<float>(frameHeight), 0.0f, 1.0f);
            placement.width = std::clamp(stickerWidthPx / static_cast<float>(frameWidth), 0.08f, 0.85f);
            placement.height = std::clamp(stickerHeightPx / static_cast<float>(frameHeight), 0.06f, 0.65f);
        }

        placement.rotation = std::clamp(rotation, -0.6f, 0.6f);
        placement.opacity = 1.0f;
        return placement;
    }
}
