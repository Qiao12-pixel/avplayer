#include "AIAssistantPanel.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {
QLabel* MakeSectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet("color: #9fb0ca; font-size: 11px; font-weight: 700; letter-spacing: 1px;");
    return label;
}

QPushButton* MakeToolButton(const QString& title, const QString& detail, QWidget* parent) {
    auto* button = new QPushButton(title + "\n" + detail, parent);
    button->setMinimumHeight(64);
    button->setEnabled(false);
    button->setToolTip("功能模块接入后可用");
    button->setStyleSheet("QPushButton { text-align: left; background: rgba(26, 38, 58, 0.82); color: #718096; border: 1px solid rgba(110, 130, 160, 0.14); border-radius: 12px; padding: 9px 11px; font-size: 12px; }");
    return button;
}
}

AIAssistantPanel::AIAssistantPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("aiAssistantPanel");
    setStyleSheet("QWidget#aiAssistantPanel { background: transparent; color: #e7eefb; } QLabel { background: transparent; } QPushButton { color: #e8effa; background: rgba(37, 53, 77, 0.92); border: 1px solid rgba(132, 156, 188, 0.20); border-radius: 10px; padding: 9px 11px; font-size: 12px; } QPushButton:hover:enabled { background: rgba(54, 75, 106, 0.98); }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(13);

    auto* header = new QHBoxLayout();
    auto* avatar = new QLabel("✦", this);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(38, 38);
    avatar->setStyleSheet("background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #5b7cfa,stop:1 #a855f7); color: white; border-radius: 13px; font-size: 19px; font-weight: 700;");
    auto* titleColumn = new QVBoxLayout();
    auto* title = new QLabel("AI 助手", this);
    title->setStyleSheet("color: #f5f8ff; font-size: 17px; font-weight: 700;");
    auto* subtitle = new QLabel("媒体理解与播放控制", this);
    subtitle->setStyleSheet("color: #91a2bb; font-size: 11px;");
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    header->addWidget(avatar);
    header->addLayout(titleColumn, 1);
    auto* connection = new QLabel("未连接", this);
    connection->setStyleSheet("background: rgba(234, 179, 8, 0.12); color: #facc15; border: 1px solid rgba(234, 179, 8, 0.20); border-radius: 9px; padding: 5px 8px; font-size: 10px;");
    header->addWidget(connection, 0, Qt::AlignTop);
    root->addLayout(header);

    auto* contextCard = new QFrame(this);
    contextCard->setStyleSheet("QFrame { background: rgba(17, 28, 45, 0.94); border: 1px solid rgba(119, 148, 188, 0.19); border-radius: 15px; }");
    auto* contextLayout = new QVBoxLayout(contextCard);
    contextLayout->setContentsMargins(13, 12, 13, 12);
    contextLayout->setSpacing(7);
    auto* contextHeading = new QHBoxLayout();
    contextHeading->addWidget(MakeSectionTitle("当前媒体", contextCard));
    contextHeading->addStretch();
    m_recordLabel = new QLabel("未录制", contextCard);
    m_recordLabel->setStyleSheet("color: #9aa9bd; font-size: 10px;");
    contextHeading->addWidget(m_recordLabel);
    contextLayout->addLayout(contextHeading);
    m_sourceLabel = new QLabel("尚未打开媒体", contextCard);
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setStyleSheet("color: #e6edf8; font-size: 12px; font-weight: 600;");
    m_stateLabel = new QLabel("导入视频后，助手可以围绕当前媒体工作", contextCard);
    m_stateLabel->setWordWrap(true);
    m_stateLabel->setStyleSheet("color: #91a2bb; font-size: 11px;");
    m_resolutionLabel = new QLabel("分辨率 —", contextCard);
    m_resolutionLabel->setStyleSheet("color: #91a2bb; font-size: 10px;");
    contextLayout->addWidget(m_sourceLabel);
    contextLayout->addWidget(m_stateLabel);
    contextLayout->addWidget(m_resolutionLabel);
    root->addWidget(contextCard);

    root->addWidget(MakeSectionTitle("播放控制", this));
    auto* controls = new QHBoxLayout();
    controls->setSpacing(8);
    m_playPauseButton = new QPushButton("▶  播放", this);
    m_playPauseButton->setMinimumHeight(40);
    m_playPauseButton->setStyleSheet("QPushButton { background: #315efb; border: none; border-radius: 11px; color: white; font-weight: 700; padding: 9px; } QPushButton:hover:enabled { background: #4772ff; }");
    m_seekToStartButton = new QPushButton("↤  回到开头", this);
    m_seekToStartButton->setMinimumHeight(40);
    controls->addWidget(m_playPauseButton, 1);
    controls->addWidget(m_seekToStartButton, 1);
    root->addLayout(controls);
    connect(m_playPauseButton, &QPushButton::clicked, this, &AIAssistantPanel::playPauseRequested);
    connect(m_seekToStartButton, &QPushButton::clicked, this, &AIAssistantPanel::seekToStartRequested);

    root->addWidget(MakeSectionTitle("AI 工具", this));
    auto* toolsGrid = new QGridLayout();
    toolsGrid->setContentsMargins(0, 0, 0, 0);
    toolsGrid->setHorizontalSpacing(8);
    toolsGrid->setVerticalSpacing(8);
    toolsGrid->addWidget(MakeToolButton("字幕转写", "生成时间轴字幕", this), 0, 0);
    toolsGrid->addWidget(MakeToolButton("视频摘要", "提炼章节与重点", this), 0, 1);
    toolsGrid->addWidget(MakeToolButton("语义搜索", "按内容查找片段", this), 1, 0);
    toolsGrid->addWidget(MakeToolButton("智能剪辑", "选段并导出视频", this), 1, 1);
    root->addLayout(toolsGrid);

    auto* statusCard = new QFrame(this);
    statusCard->setStyleSheet("QFrame { background: rgba(25, 34, 53, 0.78); border: 1px solid rgba(119, 148, 188, 0.13); border-radius: 13px; }");
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(12, 10, 12, 10);
    statusLayout->setSpacing(5);
    auto* statusTitle = new QLabel("助手状态", statusCard);
    statusTitle->setStyleSheet("color: #dbe7fa; font-size: 11px; font-weight: 700;");
    auto* statusText = new QLabel("播放控制已就绪。字幕、摘要、搜索与剪辑将在功能模块接入后开放。", statusCard);
    statusText->setWordWrap(true);
    statusText->setStyleSheet("color: #8fa0b8; font-size: 10px;");
    statusLayout->addWidget(statusTitle);
    statusLayout->addWidget(statusText);
    root->addWidget(statusCard);
    root->addStretch(1);
}

void AIAssistantPanel::SetMediaContext(const QString& source, bool playing, int width, int height) {
    const bool hasMedia = !source.isEmpty();
    if (hasMedia) {
        m_sourceLabel->setText(source);
        m_stateLabel->setText(playing ? "正在播放 · 可控制当前会话" : "已加载 · 当前处于暂停状态");
        m_resolutionLabel->setText(width > 0 && height > 0
            ? QString("分辨率 %1 × %2").arg(width).arg(height)
            : QStringLiteral("媒体信息读取中"));
    } else {
        m_sourceLabel->setText("尚未打开媒体");
        m_stateLabel->setText("导入视频后，助手可以围绕当前媒体工作");
        m_resolutionLabel->setText("分辨率 —");
    }
    m_playPauseButton->setEnabled(hasMedia);
    m_seekToStartButton->setEnabled(hasMedia);
    m_playPauseButton->setText(playing ? "Ⅱ  暂停" : "▶  播放");
}

void AIAssistantPanel::SetRecording(bool recording) {
    m_recordLabel->setText(recording ? "● 正在录制" : "未录制");
    m_recordLabel->setStyleSheet(recording ? "color: #fb7185; font-size: 10px; font-weight: 700;"
                                            : "color: #9aa9bd; font-size: 10px;");
}
