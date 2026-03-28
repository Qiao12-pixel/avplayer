//
// Created by Codex on 26-3-27.
//

#ifndef OPENCVLBFFACELANDMARKDETECTOR_H
#define OPENCVLBFFACELANDMARKDETECTOR_H

#include "IFaceLandmarkDetector.h"

#include <mutex>

#include <opencv2/core.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>

namespace av {
    class OpenCVLbfFaceLandmarkDetector final : public IFaceLandmarkDetector {
    public:
        OpenCVLbfFaceLandmarkDetector();
        ~OpenCVLbfFaceLandmarkDetector() override = default;

        bool Initialize() override;
        FaceLandmarkResult Detect(const uint8_t* rgbaData, int width, int height) override;

    private:
        bool FindCascadePath(std::string& cascadePath) const;

        mutable std::mutex m_mutex;
        cv::CascadeClassifier m_faceCascade;
        cv::Ptr<cv::face::Facemark> m_facemark;
        bool m_initialized{false};
    };
}

#endif //OPENCVLBFFACELANDMARKDETECTOR_H
