//
// Created by Codex on 26-3-27.
//

#ifndef IFACELANDMARKDETECTOR_H
#define IFACELANDMARKDETECTOR_H

#include <cstdint>
#include <memory>
#include <vector>

namespace av {
    struct FacePoint2f {
        float x{0.0f};
        float y{0.0f};
    };

    struct FaceRect {
        float x{0.0f};
        float y{0.0f};
        float width{0.0f};
        float height{0.0f};
    };

    struct FaceLandmarkResult {
        bool valid{false};
        FaceRect faceRect;
        std::vector<FacePoint2f> landmarks;
    };

    struct IFaceLandmarkDetector {
        virtual ~IFaceLandmarkDetector() = default;

        virtual bool Initialize() = 0;
        virtual FaceLandmarkResult Detect(const uint8_t* rgbaData, int width, int height) = 0;
    };
}

#endif //IFACELANDMARKDETECTOR_H
