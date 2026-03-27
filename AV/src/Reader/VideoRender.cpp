//
// Created by Joe on 26-3-26.
//
#include <GL/glew.h>
#include "VideoRender.h"

#include <iostream>

namespace av {
    const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aTexCoord;
    out vec2 TexCoord;
    void main() {
        gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
        TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y); // 翻转 Y 轴，解决画面倒置
    }
    )";

    // 极简片段着色器：直接去纹理里采样 RGBA 颜色
    const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    in vec2 TexCoord;
    uniform sampler2D videoTexture;
    void main() {
        FragColor = texture(videoTexture, TexCoord);
    }
    )";
    VideoRender::VideoRender() {

    }
    VideoRender::~VideoRender() {
        if (m_window) {
            glfwDestroyWindow(m_window);
            glfwTerminate();
        }
    }
    bool VideoRender::Init(int width, int height, const char *title) {
        if (!glfwInit()) {
            std::cerr << "failed to init GLFW" << std::endl;
            return false;
        }
        // 告诉 GLFW 我们需要 OpenGL 3.3 Core 模式（macOS 必须设置）
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

        m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!m_window) {
            std::cerr << "failed to Create window" << std::endl;
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(1);//开启垂直同步，防止画面撕裂【1-开启，0-关闭】
        glewInit();
        m_shaderProgram = CompileShader();
        float vertices[] = {
            1.0f,  1.0f,  1.0f, 1.0f,
            1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f
        };
        unsigned int indices[] = {
            0, 1, 3,
            1, 2, 3
        };
        glGenVertexArrays(1, &m_vao);

        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);//EBO = 索引缓冲对象，用来「省显存、省算力」，避免重复定义顶点，用索引重复使用顶点。

        glBindVertexArray(m_vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        //申请一个空白纹理用于渲染
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        return  true;
    }
    void VideoRender::PushFrame(std::shared_ptr<IVideoFrame> frame) {
        // std::lock_guard<std::mutex> lock(m_frameMutex);
        // m_condv.wait(lock,[this]() {
        //    return m_currentFrame == nullptr;
        // });
        std::unique_lock<std::mutex> lock(m_frameMutex);
        //如果抽屉里还有画没被拿走，解码线程就乖乖阻塞在这里！
        m_condv.wait(lock, [this]() {
            //破除思索核心：如果正在退出，也立刻结束等待
           return m_currentFrame == nullptr || m_bExiting;
        });
        if (m_bExiting) {
            return;
        }
        m_currentFrame = frame;
        std::cout << "🎨 [Renderer] 已将一帧画面装入显卡抽屉, PTS: " << frame->pts << std::endl;
    }
    void VideoRender::StopWait() {
        m_bExiting = true;
        m_condv.notify_all();
    }

    GLuint VideoRender::CompileShader() {
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
        glCompileShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
        glCompileShader(fragmentShader);

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        return shaderProgram;
    }

    void VideoRender::Render() {
        std::shared_ptr<IVideoFrame> frameToRender;
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            frameToRender = m_currentFrame;
            m_currentFrame.reset();
        }
        //唤醒后台解码器，抽屉空了，可以塞下一帧
        m_condv.notify_one();

        // 🌟 解决 macOS 黑屏的关键：动态获取视网膜屏幕的真实像素大小
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);


        if (frameToRender && frameToRender->data) {
            glBindTexture(GL_TEXTURE_2D, m_texture);
            // 🌟 内存对齐护盾：告诉 OpenGL 紧凑读取内存，防止花屏
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, frameToRender->width);
            if (m_texWidth != frameToRender->width || m_texHeight != frameToRender->height) {
                //分辨率改变时，重新分配显存
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frameToRender->width, frameToRender->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameToRender->data.get());
                m_texWidth = frameToRender->width;
                m_texHeight = frameToRender->height;
            } else {
                //补充每次都分配，减少内存分配
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_texWidth, m_texHeight, GL_RGBA, GL_UNSIGNED_BYTE, frameToRender->data.get());
            }
        }
        //开始画图
        glUseProgram(m_shaderProgram);
        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        // 恢复默认对齐，防止污染全局状态
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    void VideoRender::Update() {
        glfwSwapBuffers(m_window);//交换前后缓冲
        glfwPollEvents();//处理鼠标键盘事件
    }
    bool VideoRender::ShouldClose() {
        return glfwWindowShouldClose(m_window);
    }
}