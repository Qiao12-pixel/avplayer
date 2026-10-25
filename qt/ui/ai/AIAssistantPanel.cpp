#include "AIAssistantPanel.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
QLabel* MakeSectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet("color: #94b9bd; font-size: 11px; font-weight: 700;");
    return label;
}

QPushButton* MakeToolButton(const QString& title, const QString& detail, QWidget* parent) {
    auto* button = new QPushButton(title + "\n" + detail, parent);
    button->setFixedHeight(70);
    button->setCursor(Qt::PointingHandCursor);
    button->setEnabled(false);
    button->setStyleSheet(R"(
        QPushButton {
            text-align: left; background: #20364b; color: #eef5fa;
            border: 1px solid #456079; border-bottom: 3px solid #102030;
            border-radius: 7px; padding: 9px 12px; font-size: 12px;
        }
        QPushButton:hover:enabled { background: #2c4c62; border-color: #70aeb4; }
        QPushButton:pressed:enabled {
            background: #16777b; color: #ffffff; border-color: #78c9c3;
            border-top: 3px solid #0b474c; border-bottom: 1px solid #0b474c;
            padding-top: 11px; padding-bottom: 7px;
        }
        QPushButton:disabled {
            background: #172638; color: #899bae;
            border-color: #2d4053; border-bottom: 3px solid #101b29;
        }
    )");
    return button;
}
}

AIAssistantPanel::AIAssistantPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("aiAssistantPanel");
    setStyleSheet(R"(
        QWidget#aiAssistantPanel { background: transparent; color: #e7eefb; }
        QLabel { background: transparent; }
        QPushButton {
            color: #e8effa; background: #263e56; border: 1px solid #48637c;
            border-bottom: 3px solid #152638; border-radius: 7px;
            padding: 8px 11px; font-size: 12px;
        }
        QPushButton:hover:enabled { background: #355570; border-color: #69a8af; }
        QPushButton:pressed:enabled {
            background: #176f75; border-top: 3px solid #0c4247;
            border-bottom: 1px solid #0c4247; padding-top: 10px; padding-bottom: 6px;
        }
        QPushButton:disabled { color: #8496a9; background: #172638; border-color: #2d4053; }
        QCheckBox { color: #bfd1df; spacing: 8px; font-size: 12px; }
        QCheckBox::indicator { width: 16px; height: 16px; }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 8, 4, 4);
    root->setSpacing(8);

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
    m_connectionLabel = new QLabel("未配置", this);
    m_connectionLabel->setStyleSheet("background: rgba(234, 179, 8, 0.12); color: #facc15; border: 1px solid rgba(234, 179, 8, 0.20); border-radius: 9px; padding: 5px 8px; font-size: 10px;");
    header->addWidget(m_connectionLabel, 0, Qt::AlignTop);
    root->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; } QScrollBar:vertical { width: 7px; background: transparent; } QScrollBar::handle:vertical { background: #36566b; border-radius: 3px; min-height: 28px; }");
    auto* content = new QWidget(scroll);
    content->setStyleSheet("background: transparent;");
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 8, 0);
    contentLayout->setSpacing(9);
    contentLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    auto* contextCard = new QFrame(content);
    contextCard->setObjectName("mediaContext");
    contextCard->setStyleSheet("QFrame#mediaContext { background: transparent; border-bottom: 1px solid #30485a; }");
    auto* contextLayout = new QVBoxLayout(contextCard);
    contextLayout->setContentsMargins(0, 6, 0, 9);
    contextLayout->setSpacing(4);
    auto* contextHeading = new QHBoxLayout();
    contextHeading->addWidget(MakeSectionTitle("当前媒体", contextCard));
    contextHeading->addStretch();
    m_recordLabel = new QLabel("未录制", contextCard);
    m_recordLabel->setStyleSheet("color: #9aa9bd; font-size: 10px;");
    contextHeading->addWidget(m_recordLabel);
    contextLayout->addLayout(contextHeading);
    m_sourceLabel = new QLabel("尚未打开媒体", contextCard);
    m_sourceLabel->setWordWrap(false);
    m_sourceLabel->setStyleSheet("color: #e6edf8; font-size: 12px; font-weight: 600;");
    m_stateLabel = new QLabel("导入视频后，助手可以围绕当前媒体工作", contextCard);
    m_stateLabel->setWordWrap(true);
    m_stateLabel->setStyleSheet("color: #91a2bb; font-size: 11px;");
    m_resolutionLabel = new QLabel("分辨率 —", contextCard);
    m_resolutionLabel->setStyleSheet("color: #91a2bb; font-size: 10px;");
    contextLayout->addWidget(m_sourceLabel);
    contextLayout->addWidget(m_stateLabel);
    contextLayout->addWidget(m_resolutionLabel);
    contentLayout->addWidget(contextCard);

    contentLayout->addWidget(MakeSectionTitle("播放控制", content));
    auto* controls = new QHBoxLayout();
    controls->setSpacing(8);
    m_playPauseButton = new QPushButton("▶  播放", this);
    m_playPauseButton->setMinimumHeight(40);
    m_playPauseButton->setStyleSheet(R"(
        QPushButton { background: #245bd5; border: 1px solid #4579e4; border-bottom: 3px solid #173f9c;
                      border-radius: 7px; color: white; font-weight: 700; padding: 8px; }
        QPushButton:hover:enabled { background: #3672e6; }
        QPushButton:pressed:enabled { background: #174aa9; border-top: 3px solid #102f74;
                                      border-bottom: 1px solid #102f74; padding-top: 10px; padding-bottom: 6px; }
        QPushButton:disabled { background: #1c345e; color: #8fa8ce; }
    )");
    m_seekToStartButton = new QPushButton("↤  回到开头", this);
    m_seekToStartButton->setMinimumHeight(40);
    controls->addWidget(m_playPauseButton, 1);
    controls->addWidget(m_seekToStartButton, 1);
    contentLayout->addLayout(controls);
    connect(m_playPauseButton, &QPushButton::clicked, this, &AIAssistantPanel::playPauseRequested);
    connect(m_seekToStartButton, &QPushButton::clicked, this, &AIAssistantPanel::seekToStartRequested);
    m_subtitleToggle = new QCheckBox("显示字幕", this);
    m_subtitleToggle->setChecked(true);
    m_subtitleToggle->setEnabled(false);
    contentLayout->addWidget(m_subtitleToggle);
    connect(m_subtitleToggle, &QCheckBox::toggled, this, &AIAssistantPanel::subtitleVisibilityChanged);

    contentLayout->addWidget(MakeSectionTitle("AI 工具", content));
    auto* toolsGrid = new QGridLayout();
    toolsGrid->setContentsMargins(0, 0, 0, 0);
    toolsGrid->setHorizontalSpacing(8);
    toolsGrid->setVerticalSpacing(8);
    m_transcribeButton = MakeToolButton("字幕转写", "生成时间轴字幕", this);
    m_summaryButton = MakeToolButton("视频摘要", "提炼章节与重点", this);
    m_searchButton = MakeToolButton("语义搜索", "按内容查找片段", this);
    m_editButton = MakeToolButton("智能剪辑", "选段并导出视频", this);
    toolsGrid->addWidget(m_transcribeButton, 0, 0);
    toolsGrid->addWidget(m_summaryButton, 0, 1);
    toolsGrid->addWidget(m_searchButton, 1, 0);
    toolsGrid->addWidget(m_editButton, 1, 1);
    contentLayout->addLayout(toolsGrid);
    contentLayout->addWidget(MakeSectionTitle("搜索 / 剪辑目标", content));
    m_promptEdit = new QLineEdit(this);
    m_promptEdit->setPlaceholderText("输入搜索问题或剪辑目标");
    m_promptEdit->setMinimumHeight(38);
    contentLayout->addWidget(m_promptEdit);
    connect(m_transcribeButton, &QPushButton::clicked, this, &AIAssistantPanel::transcriptionRequested);
    connect(m_summaryButton, &QPushButton::clicked, this, &AIAssistantPanel::summaryRequested);
    connect(m_searchButton, &QPushButton::clicked, this, [this] { emit searchRequested(m_promptEdit->text()); });
    connect(m_editButton, &QPushButton::clicked, this, [this] { emit editPlanRequested(m_promptEdit->text()); });
    m_exportButton = new QPushButton("导出建议片段", this);
    m_exportButton->setEnabled(false);
    contentLayout->addWidget(m_exportButton);
    connect(m_exportButton, &QPushButton::clicked, this, &AIAssistantPanel::exportEditPlanRequested);
    scroll->setWidget(content);
    root->addWidget(scroll, 3);

    root->addWidget(MakeSectionTitle("处理结果", this));
    m_resultEdit = new QPlainTextEdit(this);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setMinimumHeight(130);
    m_resultEdit->setPlaceholderText("AI 处理结果将在这里显示");
    m_resultEdit->setStyleSheet("background: #111c2d; color: #e7eefb; border: 1px solid #334155; border-radius: 10px; padding: 8px;");
    root->addWidget(m_resultEdit, 2);

    auto* statusCard = new QFrame(this);
    statusCard->setObjectName("statusStrip");
    statusCard->setStyleSheet("QFrame#statusStrip { background: transparent; border-top: 1px solid #30485a; }");
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(0, 7, 0, 2);
    statusLayout->setSpacing(3);
    auto* statusTitle = new QLabel("状态", statusCard);
    statusTitle->setStyleSheet("color: #94b9bd; font-size: 11px; font-weight: 700;");
    m_statusLabel = new QLabel("请配置豆包 API，并打开本地视频。", statusCard);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMaximumHeight(34);
    m_statusLabel->setStyleSheet("color: #8fa0b8; font-size: 10px;");
    statusLayout->addWidget(statusTitle);
    statusLayout->addWidget(m_statusLabel);
    root->addWidget(statusCard);
    RefreshTools();
}

void AIAssistantPanel::SetMediaContext(const QString& source, bool playing, int width, int height) {
    const bool hasMedia = !source.isEmpty();
    if (hasMedia) {
        m_sourceLabel->setText(QFontMetrics(m_sourceLabel->font()).elidedText(
            QFileInfo(source).fileName(), Qt::ElideMiddle, 280));
        m_sourceLabel->setToolTip(source);
        m_stateLabel->setText(playing ? "正在播放" : "已暂停");
        m_resolutionLabel->setText(width > 0 && height > 0
            ? QString("分辨率 %1 × %2").arg(width).arg(height)
            : QStringLiteral("媒体信息读取中"));
    } else {
        m_sourceLabel->setText("尚未打开媒体");
        m_sourceLabel->setToolTip({});
        m_stateLabel->setText("导入视频后，助手可以围绕当前媒体工作");
        m_resolutionLabel->setText("分辨率 —");
    }
    m_hasMedia = hasMedia;
    RefreshTools();
    m_playPauseButton->setEnabled(hasMedia);
    m_seekToStartButton->setEnabled(hasMedia);
    m_playPauseButton->setText(playing ? "Ⅱ  暂停" : "▶  播放");
}

void AIAssistantPanel::SetRecording(bool recording) {
    m_recordLabel->setText(recording ? "● 正在录制" : "未录制");
    m_recordLabel->setStyleSheet(recording ? "color: #fb7185; font-size: 10px; font-weight: 700;"
                                            : "color: #9aa9bd; font-size: 10px;");
}

void AIAssistantPanel::SetConfiguration(bool chatReady, bool speechReady, const QString& status) {
    m_chatReady = chatReady; m_speechReady = speechReady; m_configStatus = status;
    m_connectionLabel->setText(chatReady && speechReady ? "凭据已填写" : "配置待完成");
    m_statusLabel->setText(chatReady && speechReady ? "豆包 API 已配置" : status);
    m_statusLabel->setToolTip(status);
    RefreshTools();
}
void AIAssistantPanel::SetTranscript(const QString& sourcePath) {
    m_hasTranscript = !sourcePath.isEmpty();
    if (m_hasTranscript) m_statusLabel->setText("已加载字幕 · " + QFileInfo(sourcePath).fileName());
    else m_statusLabel->setText(m_chatReady && m_speechReady ? "豆包 API 已配置" : m_configStatus);
    m_statusLabel->setToolTip(m_hasTranscript ? sourcePath : m_configStatus);
    RefreshTools();
}
void AIAssistantPanel::SetSubtitleAvailable(bool available) {
    m_subtitleToggle->setEnabled(available);
}
void AIAssistantPanel::SetTaskStarted(const QString& name) {
    m_taskBusy = true;
    RefreshTools();
    m_statusLabel->setText(name + "处理中…");
    m_resultEdit->setPlainText(name + "处理中，请稍候…");
}
void AIAssistantPanel::SetTaskProgress(const QString& name, const QString& message) {
    m_statusLabel->setText(name + "处理中…");
    m_resultEdit->setPlainText(message);
}
void AIAssistantPanel::SetTaskResult(const QString& name, const QString& result) {
    m_taskBusy = false;
    RefreshTools();
    m_statusLabel->setText(name + "完成"); m_resultEdit->setPlainText(result);
}
void AIAssistantPanel::SetTaskError(const QString& name, const QString& error) {
    m_taskBusy = false;
    RefreshTools();
    m_statusLabel->setText(name + "失败"); m_resultEdit->setPlainText(error);
}
void AIAssistantPanel::SetEditPlanReady(bool ready) { m_hasPlan = ready; RefreshTools(); }
void AIAssistantPanel::RefreshTools() {
    m_transcribeButton->setEnabled(!m_taskBusy && m_hasMedia && m_speechReady);
    m_summaryButton->setEnabled(!m_taskBusy && m_hasTranscript && m_chatReady);
    m_searchButton->setEnabled(!m_taskBusy && m_hasTranscript && m_chatReady);
    m_editButton->setEnabled(!m_taskBusy && m_hasTranscript && m_chatReady);
    m_exportButton->setEnabled(!m_taskBusy && m_hasPlan && m_hasMedia);
}
