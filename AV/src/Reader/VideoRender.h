//
// Created by Joe on 26-3-26.
//

#ifndef VIDEORENDER_H
#define VIDEORENDER_H


#include <memory>
#include <GLFW/glfw3.h>

#include "../Define/IVideoFrame.h"
#include <condition_variable>

/*
 * 视频渲染===消费者收到
 * 通过IVideoFrame拿到Frame,
 */
namespace av {
    class VideoRender {
    public:
        VideoRender();
        ~VideoRender();

        bool Init(int width, int height, const char* title);
        void PushFrame(std::shared_ptr<IVideoFrame> frame);
        void Render();
        void Update();
        bool ShouldClose();
        void StopWait();
    private:

        GLFWwindow* m_window{nullptr};
        GLuint m_shaderProgram{0};
        GLuint m_vao{0}, m_vbo{0}, m_ebo{0};//EBO用于绘制2个三角形为矩形的选择
        GLuint m_texture{0};

        int m_texWidth{0};
        int m_texHeight{0};

        //线程安全
        std::mutex m_frameMutex;
        std::condition_variable m_condv;
        std::atomic<bool> m_bExiting{false};//退出标志
        std::shared_ptr<IVideoFrame> m_currentFrame;
        GLuint CompileShader();
    };
}



#endif //VIDEORENDER_H
