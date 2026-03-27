//
// Created by Joe on 26-3-24.
//

#ifndef IGLCONTEXT_H
#define IGLCONTEXT_H
// 定义不同平台的OpenGL上下文，这里只定义了QT的OpenGL上下文

// #include <QOffscreenSurface>
// #include <QOpenGLContext>
//不使用qt的OpenGL上下文，使用原声的OpenGL上下文

#include <GLFW/glfw3.h>

namespace av {
    class GLContext {
    public:
        /*
         * 构造函数传入主窗口的GLFWwindow指针，如果传入nullptr，表示是一个完全独立的新上下文
         * 如果传入已有的主窗口，表示要与主窗口共享纹理，VBO等资源
         */
        explicit GLContext(GLFWwindow* sharedContext = nullptr);
        virtual ~GLContext();

        bool Initialize();   // 初始化OpenGL上下文
        void Destroy();      // 销毁OpenGL上下文
        void MakeCurrent();  // 将该上下文绑定到当前线程
        void DoneCurrent();  // 将该上下文从当前线程解绑
        GLFWwindow* GetWindow() const {
            return m_window;// 提供接口获取内部的窗口指针（方便后续的共享操作）
        }
    private:
        GLFWwindow* m_sharedWindow{nullptr};
        GLFWwindow* m_window{nullptr};//GLFW中，window本身就是context的载体
    };

}  // namespace av
#endif //IGLCONTEXT_H
