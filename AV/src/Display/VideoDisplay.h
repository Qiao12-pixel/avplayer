//
// Created by Joe on 26-3-27.
//

#ifndef VIDEODISPLAY_H
#define VIDEODISPLAY_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <QOpenGLFunctions_3_3_Core>

#include "../../include/IVideoFilter.h"
#include "../Define/IVideoFrame.h"
#include "../FaceDetect/IFaceLandmarkDetector.h"
#include "../FaceDetect/StickerPlacementEstimator.h"

namespace av {
    class VideoDisplay {
    public:
        VideoDisplay();
        ~VideoDisplay();

        void Initialize(QOpenGLFunctions_3_3_Core* gl);
        void Cleanup(QOpenGLFunctions_3_3_Core* gl);
        void Resize(QOpenGLFunctions_3_3_Core* gl, int width, int height);
        void Render(QOpenGLFunctions_3_3_Core* gl);

        void PushFrame(const std::shared_ptr<IVideoFrame>& frame);
        void ClearFrame();
        void SetFilterEnabled(VideoFilterType type, bool enabled);
        void CycleStickerResource();

    private:
        StickerAnchorMode GetStickerAnchorMode() const;
        bool IsFilterEnabled(VideoFilterType type) const;
        void EnsureStickerTexture(QOpenGLFunctions_3_3_Core* gl);
        void UpdateFrameTexture(QOpenGLFunctions_3_3_Core* gl, const std::shared_ptr<IVideoFrame>& frame);
        void UpdateStickerPlacement(const std::shared_ptr<IVideoFrame>& frame);
        GLuint CompileShader(QOpenGLFunctions_3_3_Core* gl, GLenum shaderType, const char* source);
        GLuint CreateProgram(QOpenGLFunctions_3_3_Core* gl);

        mutable std::mutex m_frameMutex;
        std::shared_ptr<IVideoFrame> m_pendingFrame;
        std::unordered_map<VideoFilterType, std::shared_ptr<IVideoFilter>> m_filters;

        GLuint m_program{0};
        GLuint m_vao{0};
        GLuint m_vbo{0};
        GLuint m_ebo{0};
        GLuint m_frameTexture{0};
        GLuint m_stickerTexture{0};
        int m_frameWidth{0};
        int m_frameHeight{0};
        bool m_initialized{false};
        bool m_stickerLoaded{false};
        bool m_hasUploadedFrame{false};
        int m_faceDetectFrameInterval{6};
        int m_faceDetectFrameCounter{0};
        int m_faceMissingFrameCount{0};
        int m_stickerHoldFrameCount{0};
        StickerPlacement m_smoothedPlacement;
        bool m_hasSmoothedPlacement{false};
        std::unique_ptr<IFaceLandmarkDetector> m_faceDetector;
        StickerPlacementEstimator m_stickerPlacementEstimator;
        std::string m_stickerRelativePath{"sticker/Sticker0.png"};
        bool m_stickerTextureDirty{true};
    };
}

#endif //VIDEODISPLAY_H
