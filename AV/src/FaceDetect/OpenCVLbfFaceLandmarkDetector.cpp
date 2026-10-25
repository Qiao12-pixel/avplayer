//
// Created by Codex on 26-3-27.
//

#include "OpenCVLbfFaceLandmarkDetector.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QProcessEnvironment>

#include <iostream>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace av {
    namespace {
        constexpr const char* kCascadeName = "haarcascade_frontalface_default.xml";
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
        result.valid = true;
        result.faceRect.x = static_cast<float>(largestFace.x);
        result.faceRect.y = static_cast<float>(largestFace.y);
        result.faceRect.width = static_cast<float>(largestFace.width);
        result.faceRect.height = static_cast<float>(largestFace.height);
        if (m_facemark->fit(bgr, faceRects, landmarks) && !landmarks.empty() &&
            landmarks.front().size() == 68) {
            const auto& points = landmarks.front();
            const auto& leftEye = points[36];
            const auto& rightEye = points[45];
            const bool plausible = std::isfinite(leftEye.x) && std::isfinite(leftEye.y) &&
                std::isfinite(rightEye.x) && std::isfinite(rightEye.y) &&
                leftEye.x >= largestFace.x && rightEye.x <= largestFace.x + largestFace.width &&
                rightEye.x - leftEye.x >= largestFace.width * 0.1f &&
                leftEye.y >= largestFace.y && leftEye.y <= largestFace.y + largestFace.height &&
                rightEye.y >= largestFace.y && rightEye.y <= largestFace.y + largestFace.height;
            if (plausible) {
                result.landmarks.reserve(points.size());
                for (const auto& point : points) {
                    result.landmarks.push_back(FacePoint2f{point.x, point.y});
                }
            }
        }
        return result;
    }

    bool OpenCVLbfFaceLandmarkDetector::FindCascadePath(std::string& cascadePath) const {
        const QStringList paths{
            QProcessEnvironment::systemEnvironment().value("AVPLAYER_HAAR_CASCADE_PATH"),
            QString("/opt/homebrew/share/opencv5/haarcascades/") + kCascadeName,
            QString("/opt/homebrew/share/opencv4/haarcascades/") + kCascadeName,
            QString("/usr/local/share/opencv4/haarcascades/") + kCascadeName
        };
        for (const auto& path : paths) {
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                cascadePath = path.toStdString();
                return true;
            }
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
