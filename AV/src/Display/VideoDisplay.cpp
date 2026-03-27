//
// Created by Joe on 26-3-27.
//

#include "VideoDisplay.h"

#include <QImage>
#include <QString>

#include <algorithm>
#include <iostream>

#include "../VideoFilter/FlipVerticalFilter.h"
#include "../FaceDetect/OpenCVLbfFaceLandmarkDetector.h"
#include "../VideoFilter/GrayFilter.h"
#include "../VideoFilter/InvertFilter.h"
#include "../VideoFilter/StickerFilter.h"

namespace {
    constexpr const char* kVertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoord;
        out vec2 vTexCoord;
        uniform int uFlip;
        void main() {
            gl_Position = vec4(aPos.xy, 0.0, 1.0);
            vTexCoord = aTexCoord;
            if (uFlip == 1) {
                vTexCoord.y = 1.0 - vTexCoord.y;
            }
        }
    )";

    constexpr const char* kFragmentShaderSource = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uFrameTexture;
        uniform sampler2D uStickerTexture;
        uniform int uGray;
        uniform int uInvert;
        uniform int uSticker;
        uniform int uStickerVisible;
        uniform vec2 uStickerCenter;
        uniform vec2 uStickerSize;
        uniform float uStickerRotation;
        uniform float uStickerOpacity;
        void main() {
            vec4 color = texture(uFrameTexture, vTexCoord);
            if (uGray == 1) {
                float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
                color.rgb = vec3(gray);
            }
            if (uInvert == 1) {
                color.rgb = vec3(1.0) - color.rgb;
            }
            if (uSticker == 1 && uStickerVisible == 1) {
                vec2 relative = vTexCoord - uStickerCenter;
                float cosR = cos(-uStickerRotation);
                float sinR = sin(-uStickerRotation);
                vec2 rotated = vec2(
                    relative.x * cosR - relative.y * sinR,
                    relative.x * sinR + relative.y * cosR
                );
                vec2 stickerUv = rotated / uStickerSize + vec2(0.5, 0.5);
                if (all(greaterThanEqual(stickerUv, vec2(0.0))) &&
                    all(lessThanEqual(stickerUv, vec2(1.0)))) {
                    vec4 sticker = texture(uStickerTexture, stickerUv);
                    color = mix(color, sticker, sticker.a * uStickerOpacity);
                }
            }
            FragColor = color;
        }
    )";
}

namespace av {
    VideoDisplay::VideoDisplay() {
        m_filters[VideoFilterType::kFlipVertical] = std::make_shared<FlipVerticalFilter>();
        m_filters[VideoFilterType::kGray] = std::make_shared<GrayFilter>();
        m_filters[VideoFilterType::kInvert] = std::make_shared<InvertFilter>();
        m_filters[VideoFilterType::kSticker] = std::make_shared<StickerFilter>();
        m_faceDetector = std::make_unique<OpenCVLbfFaceLandmarkDetector>();
        if (m_faceDetector && !m_faceDetector->Initialize()) {
            std::cerr << "[VideoDisplay] face detector init failed, sticker will fallback" << std::endl;
            m_faceDetector.reset();
        }
        SetFilterEnabled(VideoFilterType::kFlipVertical, true);
    }

    VideoDisplay::~VideoDisplay() = default;

    void VideoDisplay::Initialize(QOpenGLFunctions_3_3_Core* gl) {
        if (!gl || m_initialized) {
            return;
        }

        gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        m_program = CreateProgram(gl);

        const float vertices[] = {
            -1.0f,  1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f
        };
        const unsigned int indices[] = {0, 1, 2, 0, 2, 3};

        gl->glGenVertexArrays(1, &m_vao);
        gl->glGenBuffers(1, &m_vbo);
        gl->glGenBuffers(1, &m_ebo);
        gl->glBindVertexArray(m_vao);

        gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        gl->glEnableVertexAttribArray(1);

        gl->glGenTextures(1, &m_frameTexture);
        gl->glBindTexture(GL_TEXTURE_2D, m_frameTexture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const unsigned char blackPixel[] = {0, 0, 0, 255};
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blackPixel);

        EnsureStickerTexture(gl);
        m_initialized = true;
    }

    void VideoDisplay::Cleanup(QOpenGLFunctions_3_3_Core* gl) {
        if (!gl || !m_initialized) {
            return;
        }

        if (m_stickerTexture != 0) {
            gl->glDeleteTextures(1, &m_stickerTexture);
            m_stickerTexture = 0;
        }
        if (m_frameTexture != 0) {
            gl->glDeleteTextures(1, &m_frameTexture);
            m_frameTexture = 0;
        }
        if (m_ebo != 0) {
            gl->glDeleteBuffers(1, &m_ebo);
            m_ebo = 0;
        }
        if (m_vbo != 0) {
            gl->glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
        if (m_vao != 0) {
            gl->glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }
        if (m_program != 0) {
            gl->glDeleteProgram(m_program);
            m_program = 0;
        }

        m_initialized = false;
        m_stickerLoaded = false;
        m_frameWidth = 0;
        m_frameHeight = 0;
        m_hasUploadedFrame = false;
    }

    void VideoDisplay::Resize(QOpenGLFunctions_3_3_Core* gl, int width, int height) {
        if (!gl) {
            return;
        }
        gl->glViewport(0, 0, width, height);
    }

    void VideoDisplay::Render(QOpenGLFunctions_3_3_Core* gl) {
        if (!gl || !m_initialized) {
            return;
        }

        std::shared_ptr<IVideoFrame> frame;
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            frame = m_pendingFrame;
        }

        gl->glClear(GL_COLOR_BUFFER_BIT);

        if (frame && frame->data) {
            static int renderedFrameCount = 0;
            ++renderedFrameCount;
            if (renderedFrameCount <= 5 || renderedFrameCount % 120 == 0) {
                std::cout << "[VideoDisplay] rendering frame #" << renderedFrameCount
                          << " ts=" << frame->GetTimeStamp()
                          << " size=" << frame->width << "x" << frame->height << std::endl;
            }
            UpdateFrameTexture(gl, frame);
        }

        if (!m_hasUploadedFrame) {
            return;
        }

        if (m_stickerTextureDirty) {
            EnsureStickerTexture(gl);
        }

        gl->glUseProgram(m_program);
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uFlip"), IsFilterEnabled(VideoFilterType::kFlipVertical) ? 1 : 0);
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uGray"), IsFilterEnabled(VideoFilterType::kGray) ? 1 : 0);
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uInvert"), IsFilterEnabled(VideoFilterType::kInvert) ? 1 : 0);
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uSticker"),
                        (IsFilterEnabled(VideoFilterType::kSticker) && m_stickerLoaded) ? 1 : 0);
        const auto stickerFilter = m_filters.find(VideoFilterType::kSticker);
        const int stickerVisible = (stickerFilter != m_filters.end() && stickerFilter->second)
                                   ? stickerFilter->second->GetInt("visible") : 0;
        const float stickerCenterX = (stickerFilter != m_filters.end() && stickerFilter->second)
                                     ? stickerFilter->second->GetFloat("center_x") : 0.5f;
        const float stickerCenterY = (stickerFilter != m_filters.end() && stickerFilter->second)
                                     ? stickerFilter->second->GetFloat("center_y") : 0.16f;
        const float stickerWidth = (stickerFilter != m_filters.end() && stickerFilter->second)
                                   ? stickerFilter->second->GetFloat("width") : 0.24f;
        const float stickerHeight = (stickerFilter != m_filters.end() && stickerFilter->second)
                                    ? stickerFilter->second->GetFloat("height") : 0.16f;
        const float stickerRotation = (stickerFilter != m_filters.end() && stickerFilter->second)
                                      ? stickerFilter->second->GetFloat("rotation") : 0.0f;
        const float stickerOpacity = (stickerFilter != m_filters.end() && stickerFilter->second)
                                     ? stickerFilter->second->GetFloat("opacity") : 1.0f;
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uStickerVisible"), stickerVisible);
        gl->glUniform2f(gl->glGetUniformLocation(m_program, "uStickerCenter"), stickerCenterX, stickerCenterY);
        gl->glUniform2f(gl->glGetUniformLocation(m_program, "uStickerSize"), stickerWidth, stickerHeight);
        gl->glUniform1f(gl->glGetUniformLocation(m_program, "uStickerRotation"), stickerRotation);
        gl->glUniform1f(gl->glGetUniformLocation(m_program, "uStickerOpacity"), stickerOpacity);

        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, m_frameTexture);
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uFrameTexture"), 0);

        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, m_stickerTexture);
        gl->glUniform1i(gl->glGetUniformLocation(m_program, "uStickerTexture"), 1);

        gl->glBindVertexArray(m_vao);
        gl->glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }

    void VideoDisplay::PushFrame(const std::shared_ptr<IVideoFrame>& frame) {
        static int pushedFrameCount = 0;
        ++pushedFrameCount;
        if (pushedFrameCount <= 5 || pushedFrameCount % 120 == 0) {
            std::cout << "[VideoDisplay] frame pushed #" << pushedFrameCount
                      << " ts=" << (frame ? frame->GetTimeStamp() : -1.0f)
                      << " hasData=" << (frame && frame->data ? 1 : 0) << std::endl;
        }
        if (frame && frame->data && IsFilterEnabled(VideoFilterType::kSticker)) {
            UpdateStickerPlacement(frame);
        }
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingFrame = frame;
    }

    void VideoDisplay::ClearFrame() {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingFrame.reset();
    }

    void VideoDisplay::SetFilterEnabled(VideoFilterType type, bool enabled) {
        auto it = m_filters.find(type);
        if (it == m_filters.end() || !it->second) {
            return;
        }
        it->second->SetInt("enabled", enabled ? 1 : 0);
    }

    void VideoDisplay::CycleStickerResource() {
        if (m_stickerRelativePath == "sticker/Sticker0.png") {
            m_stickerRelativePath = "sticker/EyeMask.png";
        } else {
            m_stickerRelativePath = "sticker/Sticker0.png";
        }
        m_stickerTextureDirty = true;
    }

    bool VideoDisplay::IsFilterEnabled(VideoFilterType type) const {
        auto it = m_filters.find(type);
        return it != m_filters.end() && it->second && it->second->GetInt("enabled") != 0;
    }

    StickerAnchorMode VideoDisplay::GetStickerAnchorMode() const {
        if (m_stickerRelativePath.find("EyeMask") != std::string::npos) {
            return StickerAnchorMode::kEyeMask;
        }
        return StickerAnchorMode::kHeadTop;
    }

    void VideoDisplay::EnsureStickerTexture(QOpenGLFunctions_3_3_Core* gl) {
        if (!gl) {
            return;
        }
        if (m_stickerTexture == 0) {
            gl->glGenTextures(1, &m_stickerTexture);
        }

        QImage sticker(QString::fromUtf8(RESOURCE_DIR) + "/" + QString::fromStdString(m_stickerRelativePath));
        if (sticker.isNull()) {
            std::cerr << "failed to load sticker resource" << std::endl;
            return;
        }

        sticker = sticker.convertToFormat(QImage::Format_RGBA8888);
        gl->glBindTexture(GL_TEXTURE_2D, m_stickerTexture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sticker.width(), sticker.height(), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, sticker.constBits());
        m_stickerLoaded = true;
        m_stickerTextureDirty = false;
    }

    void VideoDisplay::UpdateFrameTexture(QOpenGLFunctions_3_3_Core* gl, const std::shared_ptr<IVideoFrame>& frame) {
        gl->glBindTexture(GL_TEXTURE_2D, m_frameTexture);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(frame->width));

        if (m_frameWidth != static_cast<int>(frame->width) || m_frameHeight != static_cast<int>(frame->height)) {
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(frame->width),
                             static_cast<GLsizei>(frame->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, frame->data.get());
            m_frameWidth = static_cast<int>(frame->width);
            m_frameHeight = static_cast<int>(frame->height);
        } else {
            gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_frameWidth, m_frameHeight,
                                GL_RGBA, GL_UNSIGNED_BYTE, frame->data.get());
        }
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_hasUploadedFrame = true;
    }

    void VideoDisplay::UpdateStickerPlacement(const std::shared_ptr<IVideoFrame>& frame) {
        auto it = m_filters.find(VideoFilterType::kSticker);
        if (it == m_filters.end() || !it->second) {
            return;
        }
        auto& stickerFilter = it->second;
        ++m_faceDetectFrameCounter;
        if (!m_faceDetector || (m_faceDetectFrameCounter % m_faceDetectFrameInterval != 0 &&
                                stickerFilter->GetInt("visible") != 0)) {
            return;
        }

        const auto faceResult = m_faceDetector->Detect(frame->data.get(),
                                                       static_cast<int>(frame->width),
                                                       static_cast<int>(frame->height));
        const auto placement = m_stickerPlacementEstimator.Estimate(
                faceResult,
                static_cast<int>(frame->width),
                static_cast<int>(frame->height),
                GetStickerAnchorMode());

        if (placement.visible) {
            constexpr float kSmoothingAlpha = 0.28f;
            if (!m_hasSmoothedPlacement) {
                m_smoothedPlacement = placement;
                m_hasSmoothedPlacement = true;
            } else {
                m_smoothedPlacement.centerX += (placement.centerX - m_smoothedPlacement.centerX) * kSmoothingAlpha;
                m_smoothedPlacement.centerY += (placement.centerY - m_smoothedPlacement.centerY) * kSmoothingAlpha;
                m_smoothedPlacement.width += (placement.width - m_smoothedPlacement.width) * kSmoothingAlpha;
                m_smoothedPlacement.height += (placement.height - m_smoothedPlacement.height) * kSmoothingAlpha;
                m_smoothedPlacement.rotation += (placement.rotation - m_smoothedPlacement.rotation) * kSmoothingAlpha;
                m_smoothedPlacement.opacity += (placement.opacity - m_smoothedPlacement.opacity) * kSmoothingAlpha;
                m_smoothedPlacement.visible = true;
            }
            m_faceMissingFrameCount = 0;
            m_stickerHoldFrameCount = 0;
        } else {
            ++m_faceMissingFrameCount;
            ++m_stickerHoldFrameCount;
            if (m_hasSmoothedPlacement) {
                if (m_stickerHoldFrameCount <= 8) {
                    m_smoothedPlacement.visible = true;
                    m_smoothedPlacement.opacity = 1.0f;
                } else if (m_stickerHoldFrameCount <= 18) {
                    const float fadeProgress = static_cast<float>(m_stickerHoldFrameCount - 8) / 10.0f;
                    m_smoothedPlacement.visible = true;
                    m_smoothedPlacement.opacity = std::clamp(1.0f - fadeProgress, 0.0f, 1.0f);
                }
            }
            if (m_faceMissingFrameCount > 18) {
                m_hasSmoothedPlacement = false;
                m_smoothedPlacement.visible = false;
                m_smoothedPlacement.opacity = 0.0f;
            }
        }

        const auto& outputPlacement = m_hasSmoothedPlacement ? m_smoothedPlacement : placement;
        stickerFilter->SetInt("visible", outputPlacement.visible ? 1 : 0);
        stickerFilter->SetFloat("center_x", outputPlacement.centerX);
        stickerFilter->SetFloat("center_y", outputPlacement.centerY);
        stickerFilter->SetFloat("width", outputPlacement.width);
        stickerFilter->SetFloat("height", outputPlacement.height);
        stickerFilter->SetFloat("rotation", outputPlacement.rotation);
        stickerFilter->SetFloat("opacity", outputPlacement.opacity);
    }

    GLuint VideoDisplay::CompileShader(QOpenGLFunctions_3_3_Core* gl, GLenum shaderType, const char* source) {
        const GLuint shader = gl->glCreateShader(shaderType);
        gl->glShaderSource(shader, 1, &source, nullptr);
        gl->glCompileShader(shader);

        GLint compiled = 0;
        gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_FALSE) {
            GLchar log[512] = {0};
            gl->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "shader compile failed: " << log << std::endl;
        }
        return shader;
    }

    GLuint VideoDisplay::CreateProgram(QOpenGLFunctions_3_3_Core* gl) {
        const GLuint vertexShader = CompileShader(gl, GL_VERTEX_SHADER, kVertexShaderSource);
        const GLuint fragmentShader = CompileShader(gl, GL_FRAGMENT_SHADER, kFragmentShaderSource);

        const GLuint program = gl->glCreateProgram();
        gl->glAttachShader(program, vertexShader);
        gl->glAttachShader(program, fragmentShader);
        gl->glLinkProgram(program);

        GLint linked = 0;
        gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) {
            GLchar log[512] = {0};
            gl->glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "program link failed: " << log << std::endl;
        }

        gl->glDeleteShader(vertexShader);
        gl->glDeleteShader(fragmentShader);
        return program;
    }
}
