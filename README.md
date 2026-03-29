📋 项目简介
AVPlayer 是一款轻量级跨平台音视频播放器，核心基于 C++ + Qt 框架开发，依托 FFmpeg 实现音视频编解码与播放，结合 OpenCV 完成 AI 人脸关键点检测与滤镜特效。项目支持主流音视频格式播放、音视频录制、自定义滤镜特效，同时提供简洁的 Qt 交互界面，适配 macOS 等平台，可作为音视频开发学习、技术面试项目展示的优质案例。
✨ 核心功能
基础播放
支持 MP4、AVI、MKV、FLV 等主流音视频格式解码播放
跨平台兼容（基于 Qt，支持 macOS/Windows）
播放控制：播放 / 暂停、进度拖拽、音量调节、倍速播放
高级功能
🎥 音视频录制：支持将播放流 / 本地摄像头画面录制为 MP4 格式
🎨 自定义滤镜：集成基础滤镜特效（如灰度、美颜、边缘检测）
🧠 AI 人脸检测：基于 OpenCV 实现人脸关键点检测，叠加显示检测框 / 关键点
🔧 上传视频修复：兼容异常格式视频解析，优化上传视频播放兼容性
🏗️ 技术栈
表格
技术模块	选型 / 工具
开发语言	C++17
UI 框架	Qt 5.15+/6.x（Qt Widgets）
音视频引擎	FFmpeg（编解码、音视频同步）
AI 视觉	OpenCV（人脸关键点检测、图像处理）
构建工具	CMake 3.20+、Conan（依赖管理）
平台适配	macOS（适配 M 系列芯片）
🚀 快速开始
环境依赖
操作系统：macOS 10.15+ / Windows 10+
开发工具：Qt Creator / CLion、CMake 3.20+、Conan 2.0+
依赖库：FFmpeg 4.4+、OpenCV 4.5+、Qt Multimedia
编译步骤
克隆项目
bash
运行
git clone https://github.com/Qiao12-pixel/avplayer.git
cd avplayer
安装依赖（Conan）
bash
运行
conan install . --build=missing -s compiler.cppstd=17
CMake 构建
bash
运行
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
运行程序
bash
运行
./AVPlayer
📁 项目结构
plaintext
AVPlayer/
├── AV/             # 音视频核心模块（编解码、播放、录制）
├── qt/             # Qt 界面交互模块
├── resource/       # 资源文件（UI布局、图标、配置）
├── CMakeLists.txt  # CMake 构建配置
├── conanfile.txt   # Conan 依赖配置
├── README.md       # 项目说明
└── .gitignore      # Git 忽略配置
🎯 功能演示
1. 基础播放
支持本地视频 / 网络视频地址加载，实现音视频同步播放
精准控制播放进度，支持 seek 精准定位
2. 录制与滤镜
一键录制当前播放画面，输出 MP4 格式文件
实时切换滤镜特效，叠加人脸检测框，直观展示 AI 检测效果
3. 异常处理
自动修复上传视频的编码异常，提升视频播放兼容性
📝 开发计划
 新增更多滤镜特效（马赛克、复古、动态模糊）
 支持流媒体协议（RTSP/RTMP）播放
 优化硬件解码性能，适配更多编解码器
 移动端（Android/iOS）跨平台适配
🤝 贡献
欢迎提交 Issue 或 PR！提交 PR 前请确保：
代码符合 C++ 编码规范，注释清晰
新增功能附带测试用例
不破坏现有功能兼容性
📄 许可证
本项目基于 MIT License 开源，详见 LICENSE 文件。
📞 联系方式
作者：Qiao12-pixel
GitHub：https://github.com/Qiao12-pixel
模板适配说明
突出核心技术：明确标注 C++/Qt/FFmpeg/OpenCV 核心栈，适配技术面试 / 项目展示需求；
清晰编译流程：补充 Conan 依赖安装、CMake 构建步骤，降低他人搭建成本；
强调特色功能：重点写明人脸检测、录制、滤镜等差异化功能，提升项目竞争力；
适配平台特性：标注 macOS 适配，贴合你的开发环境。

<img width="1440" height="932" alt="image" src="https://github.com/user-attachments/assets/006e4dfa-5077-41b2-89b2-63b9c5bc5aa7" />


