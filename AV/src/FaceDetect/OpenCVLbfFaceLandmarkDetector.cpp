//
// Created by Codex on 26-3-27.
//

#include "OpenCVLbfFaceLandmarkDetector.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <iostream>

#include <opencv2/imgproc.hpp>

namespace av {
    namespace {
        constexpr const char* kHaarCascadeFallbackPath = "/opt/homebrew/Cellar/opencv/4.13.0_6/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";
    }

    OpenCVLbfFaceLandmarkDetector::OpenCVLbfFaceLandmarkDetector() = default;

    bool OpenCVLbfFaceLandmarkDetector::Initialize() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) {
            return true;
        }

        std::string cascadePath;
        if (!FindCascadePath(cascadePath)) {
            std::cerr << "[FaceDetect] failed to find haarcascade_frontalface_default.xml" << std::endl;
            return false;
        }
        if (!m_faceCascade.load(cascadePath)) {
            std::cerr << "[FaceDetect] failed to load cascade: " << cascadePath << std::endl;
            return false;
        }

        const QString modelPath = QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("lbfmodel.yaml");
        if (!QFileInfo::exists(modelPath)) {
            std::cerr << "[FaceDetect] lbf model not found: " << modelPath.toStdString() << std::endl;
            return false;
        }

        auto facemark = cv::face::FacemarkLBF::create();
        facemark->loadModel(modelPath.toStdString());
        m_facemark = facemark;
        m_initialized = true;
        std::cout << "[FaceDetect] initialized with model: " << modelPath.toStdString() << std::endl;
        return true;
    }

    FaceLandmarkResult OpenCVLbfFaceLandmarkDetector::Detect(const uint8_t* rgbaData, int width, int height) {
        std::lock_guard<std::mutex> lock(m_mutex);
        FaceLandmarkResult result;
        if (!m_initialized || !rgbaData || width <= 0 || height <= 0) {
            return result;
        }

        cv::Mat rgba(height, width, CV_8UC4, const_cast<uint8_t*>(rgbaData));
        cv::Mat bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);

        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

        std::vector<cv::Rect> faces;
        m_faceCascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(80, 80));
        if (faces.empty()) {
            return result;
        }

        auto largestFace = faces.front();
        for (const auto& face : faces) {
            if (face.area() > largestFace.area()) {
                largestFace = face;
            }
        }

        std::vector<cv::Rect> faceRects{largestFace};
        std::vector<std::vector<cv::Point2f>> landmarks;
        if (!m_facemark->fit(bgr, faceRects, landmarks) || landmarks.empty() || landmarks.front().empty()) {
            return result;
        }

        result.valid = true;
        result.faceRect.x = static_cast<float>(largestFace.x);
        result.faceRect.y = static_cast<float>(largestFace.y);
        result.faceRect.width = static_cast<float>(largestFace.width);
        result.faceRect.height = static_cast<float>(largestFace.height);
        result.landmarks.reserve(landmarks.front().size());
        for (const auto& point : landmarks.front()) {
            result.landmarks.push_back(FacePoint2f{point.x, point.y});
        }
        return result;
    }

    bool OpenCVLbfFaceLandmarkDetector::FindCascadePath(std::string& cascadePath) const {
        const QString fallbackPath = QString::fromUtf8(kHaarCascadeFallbackPath);
        if (QFileInfo::exists(fallbackPath)) {
            cascadePath = fallbackPath.toStdString();
            return true;
        }

        try {
            const auto resolved = cv::samples::findFile("haarcascade_frontalface_default.xml", false, false);
            if (!resolved.empty()) {
                cascadePath = resolved;
                return true;
            }
        } catch (...) {
        }
        return false;
    }
}
