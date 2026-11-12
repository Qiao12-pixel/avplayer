//
// Created by Joe on 26-3-24.
//


#include <iostream>


#include "IGLContext.h"

namespace av {

    GLContext::GLContext(GLFWwindow *sharedContext) : m_sharedWindow(sharedContext) {

    }
    GLContext::~GLContext() {
        Destroy();
    }


    bool GLContext::Initialize() {
        // 确保 GLFW 已初始化 (如果主线程已初始化过，这里调用也是安全的)
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW in GLContext" << std::endl;
            return false;
        }
        // 🌟 核心魔法：将接下来要创建的窗口设为“不可见”！
        // 这就是 GLFW 中实现 QOffscreenSurface 的方法
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        // macOS 必须显式配置为 OpenGL 3.3 Core 模式
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

        // 创建一个 1x1 像素大小的隐藏窗口，纯粹为了挂载 OpenGL Context
        // 第 5 个参数传入 m_sharedWindow，即可实现后台线程与主窗口的【显存共享】！
        m_window = glfwCreateWindow(1, 1, "Offscreen Context", nullptr, m_sharedWindow);

        // 恢复窗口可见性的默认状态，以免影响程序里其他正常窗口的创建
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

        if (!m_window) {
            std::cerr << "Failed to create shared offscreen context window." << std::endl;
            return false;
        }
        return true;
    }

    void GLContext::Destroy() {
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    void GLContext::MakeCurrent() {
        if (m_window) {
            // 将此上下文绑定到调用该函数的那个线程（通常是后台解码/上传线程）
            glfwMakeContextCurrent(m_window);

            // 注意：如果你要在子线程执行具体的 glGenTextures 等函数，
            // 确保你的 GLEW 在主线程中已经通过 glewInit() 初始化过了。
            // 只要有了 Shared Context，函数指针就是通用的。
        }
    }

    void GLContext::DoneCurrent() {
        // 从当前线程拔出上下文（传入 nullptr 即为解绑）
        glfwMakeContextCurrent(nullptr);
    }

}  // namespace av