#include "AgentPanel.h"

#include <QFrame>
#include <QComboBox>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QLabel* SectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet("color: #94b9bd; font-size: 11px; font-weight: 700;");
    return label;
}

QFrame* Divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet("background: #30485a; border: none;");
    return line;
}

QPushButton* QuickGoalButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setFixedHeight(38);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet("QPushButton { color: #c5d8e1; background: #203b50; border: 1px solid #41627a; border-bottom: 3px solid #142635; border-radius: 7px; padding: 8px 10px; } QPushButton:hover { background: #2b5368; border-color: #63a7aa; } QPushButton:pressed { background: #176f75; padding-top: 10px; padding-bottom: 6px; }");
    return button;
}

QLabel* ToolStatusRow(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setMinimumHeight(38);
    label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    label->setStyleSheet("color: #a9bdc9; background: #172a3b; border: 1px solid #304b61; border-radius: 7px; padding: 0 10px;");
    return label;
}

QString ToolDisplayName(const QString& tool) {
    static const QHash<QString, QString> names{
        {"get_media_context", "读取媒体上下文"}, {"get_transcript", "读取字幕"},
        {"player_control", "播放控制"}, {"capture_screenshot", "截图"},
        {"extract_audio", "提取音轨"}, {"save_transcript", "保存字幕"},
        {"set_edit_plan", "保存剪辑建议"}, {"export_clip", "剪辑导出"},
        {"export_random_clip", "随机片段导出"}, {"transcribe_subtitle", "字幕转写"},
        {"summarize_video", "视频摘要"}, {"semantic_search", "语义搜索"},
        {"smart_edit", "智能剪辑"}
    };
    return names.value(tool, tool);
}
}

AgentPanel::AgentPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("agentPanel");
    setStyleSheet(R"(
        QWidget#agentPanel { background: transparent; color: #e7eefb; }
        QLabel { background: transparent; }
        QLineEdit { color: #edf6fb; background: #111f30; border: 1px solid #3b586d;
                    border-radius: 8px; padding: 9px 10px; }
        QLineEdit:focus { border-color: #5db8b3; }
        QPlainTextEdit { color: #dceaf2; background: #111f30; border: 1px solid #30485a;
                         border-radius: 8px; padding: 8px; }
        QListWidget { color: #b9cad7; background: transparent; border: none; outline: none; }
        QListWidget::item { padding: 7px 8px; border-radius: 6px; }
        QListWidget::item:selected { color: #e7ffff; background: #1b555c; }
        QScrollBar:vertical { width: 7px; background: transparent; }
        QScrollBar::handle:vertical { background: #36566b; border-radius: 3px; min-height: 28px; }
        QComboBox { color: #e8effa; background: #20364b; border: 1px solid #456079;
                    border-radius: 7px; padding: 6px 10px; min-width: 112px; }
        QComboBox:hover { background: #2c4c62; border-color: #70aeb4; }
        QComboBox::drop-down { border: none; width: 20px; }
        QComboBox QAbstractItemView { color: #e8effa; background: #172638;
                                      selection-background-color: #176f75; }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 8, 4, 4);
    root->setSpacing(8);

    auto* header = new QHBoxLayout();
    auto* avatar = new QLabel("◎", this);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(38, 38);
    avatar->setStyleSheet("background: #14777b; color: #d8ffff; border-radius: 12px; font-size: 20px; font-weight: 700;");
    auto* heading = new QVBoxLayout();
    auto* title = new QLabel("Agent 工作台", this);
    title->setStyleSheet("color: #f5f8ff; font-size: 17px; font-weight: 700;");
    auto* subtitle = new QLabel("目标拆解与工具编排", this);
    subtitle->setStyleSheet("color: #91a2bb; font-size: 11px;");
    heading->addWidget(title);
    heading->addWidget(subtitle);
    header->addWidget(avatar);
    header->addLayout(heading, 1);
    m_runtimeLabel = new QLabel("运行时待接入", this);
    m_runtimeLabel->setStyleSheet("color: #f6c453; background: rgba(234,179,8,0.12); border: 1px solid rgba(234,179,8,0.28); border-radius: 8px; padding: 5px 8px; font-size: 10px;");
    header->addWidget(m_runtimeLabel, 0, Qt::AlignTop);
    root->addLayout(header);
    root->addWidget(Divider(this));

    auto* providerRow = new QHBoxLayout();
    providerRow->addWidget(SectionTitle("模型方式", this));
    providerRow->addStretch();
    m_providerCombo = new QComboBox(this);
    m_providerCombo->addItem("本地模型", "local");
    m_providerCombo->addItem("豆包 API", "cloud");
    providerRow->addWidget(m_providerCombo);
    root->addLayout(providerRow);
    m_providerHint = new QLabel("本地模型：任务内容留在本机。", this);
    m_providerHint->setWordWrap(true);
    m_providerHint->setStyleSheet("color: #8fa8bd; font-size: 10px;");
    root->addWidget(m_providerHint);
    connect(m_providerCombo, &QComboBox::currentIndexChanged, this, [this] {
        emit providerModeChanged(m_providerCombo->currentData().toString());
    });

    root->addWidget(SectionTitle("任务目标", this));
    m_goalEdit = new QLineEdit(this);
    m_goalEdit->setPlaceholderText("例如：找出人物讨论危险的片段并导出");
    root->addWidget(m_goalEdit);
    auto* runRow = new QHBoxLayout();
    m_runButton = new QPushButton("开始执行", this);
    m_runButton->setFixedHeight(40);
    m_runButton->setStyleSheet("QPushButton { color: #c6d5df; background: #225b60; border: 1px solid #438f8d; border-bottom: 3px solid #123b40; border-radius: 7px; font-weight: 700; } QPushButton:hover { background: #2e7778; } QPushButton:disabled { color: #7e909e; background: #1b303d; border-color: #2c4657; }");
    connect(m_runButton, &QPushButton::clicked, this, [this] { const QString goal = m_goalEdit->text().trimmed(); if (!goal.isEmpty()) emit goalSubmitted(goal); });
    runRow->addWidget(m_runButton, 2);
    auto* stopButton = new QPushButton("停止任务", this);
    stopButton->setFixedHeight(40);
    stopButton->setCursor(Qt::PointingHandCursor);
    stopButton->setStyleSheet("QPushButton { color: #c6d5df; background: #172a3b; border: 1px solid #3b586d; border-bottom: 3px solid #0e1c29; border-radius: 7px; font-weight: 700; } QPushButton:hover { background: #28465a; border-color: #63a7aa; } QPushButton:pressed { background: #1b555c; padding-top: 2px; }");
    connect(stopButton, &QPushButton::clicked, this, &AgentPanel::stopRequested);
    runRow->addWidget(stopButton, 1);
    root->addLayout(runRow);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 8, 0);
    contentLayout->setSpacing(9);

    contentLayout->addWidget(SectionTitle("快捷目标", content));
    auto* quickRow = new QHBoxLayout();
    quickRow->setSpacing(7);
    auto* summarizeGoal = QuickGoalButton("整理视频内容", content);
    auto* searchGoal = QuickGoalButton("查找关键片段", content);
    connect(summarizeGoal, &QPushButton::clicked, this, [this] { emit goalSubmitted("整理当前视频内容并给出摘要"); });
    connect(searchGoal, &QPushButton::clicked, this, [this] { emit goalSubmitted("查找当前视频中的关键片段"); });
    quickRow->addWidget(summarizeGoal);
    quickRow->addWidget(searchGoal);
    contentLayout->addLayout(quickRow);

    contentLayout->addWidget(SectionTitle("执行计划", content));
    m_planList = new QListWidget(content);
    m_planList->setMinimumHeight(48);
    m_planList->setMaximumHeight(120);
    contentLayout->addWidget(m_planList);

    contentLayout->addWidget(SectionTitle("工具调用", content));
    m_toolLayout = new QVBoxLayout();
    m_toolLayout->setSpacing(6);
    auto* emptyTools = new QLabel("工具尚未调用", content);
    emptyTools->setObjectName("emptyToolHint");
    emptyTools->setStyleSheet("color: #8296a8; font-size: 10px; padding: 3px 2px;");
    m_toolLayout->addWidget(emptyTools);
    contentLayout->addLayout(m_toolLayout);
    contentLayout->addWidget(Divider(content));
    contentLayout->addWidget(SectionTitle("Agent 输出", content));
    m_resultEdit = new QPlainTextEdit(content);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setMinimumHeight(130);
    m_resultEdit->setPlaceholderText("任务完成后，执行摘要、工具结果和输出文件会显示在这里");
    contentLayout->addWidget(m_resultEdit);
    contentLayout->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* status = new QLabel("Agent Runtime 尚未连接。", this);
    m_statusLabel = status;
    status->setWordWrap(true);
    status->setMaximumHeight(34);
    status->setStyleSheet("color: #8fa8b5; font-size: 10px;");
    root->addWidget(status);
}

void AgentPanel::SetRuntimeStatus(const QString& status, bool ready) {
    m_runtimeLabel->setText(ready ? "运行中" : "运行时错误");
    m_runtimeLabel->setStyleSheet(ready
        ? "color: #9ff0d0; background: rgba(16,185,129,0.13); border: 1px solid rgba(16,185,129,0.3); border-radius: 8px; padding: 5px 8px; font-size: 10px;"
        : "color: #f6c453; background: rgba(234,179,8,0.12); border: 1px solid rgba(234,179,8,0.28); border-radius: 8px; padding: 5px 8px; font-size: 10px;");
    if (m_statusLabel) m_statusLabel->setText(status);
}

void AgentPanel::SetPlan(const QJsonArray& steps) {
    if (!m_planList) return;
    m_planList->clear();
    for (const auto& value : steps) {
        const auto step = value.toObject();
        const QString marker = step.value("status").toString() == "done" ? "●" : "○";
        m_planList->addItem(marker + "  " + step.value("title").toString());
    }
}

void AgentPanel::ResetExecution() {
    if (m_planList) m_planList->clear();
    m_toolPlanItems.clear();
    m_toolStatusLabels.clear();
    if (m_toolLayout) {
        while (auto* item = m_toolLayout->takeAt(0)) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        auto* hint = new QLabel("工具尚未调用", this);
        hint->setObjectName("emptyToolHint");
        hint->setStyleSheet("color: #8296a8; font-size: 10px; padding: 3px 2px;");
        m_toolLayout->addWidget(hint);
    }
    SetAgentOutput({});
}

void AgentPanel::BeginToolExecution(const QString& tool, const QString& callId) {
    const QString name = ToolDisplayName(tool);
    auto* hint = findChild<QLabel*>("emptyToolHint");
    if (hint) {
        m_toolLayout->removeWidget(hint);
        hint->deleteLater();
    }
    if (!m_toolStatusLabels.contains(tool)) {
        auto* row = ToolStatusRow(name + "    ·    执行中", this);
        m_toolStatusLabels.insert(tool, row);
        m_toolLayout->addWidget(row);
    } else {
        SetToolStatus(tool, "执行中");
    }
    auto* item = new QListWidgetItem("◌  " + name + " · 执行中", m_planList);
    item->setData(Qt::UserRole, tool);
    m_toolPlanItems.insert(callId.isEmpty() ? QUuid::createUuid().toString() : callId, item);
    m_planList->scrollToBottom();
}

void AgentPanel::CompleteToolExecution(const QString& tool, const QString& callId, bool success, const QString& detail) {
    const QString status = success ? "已完成" : "失败";
    SetToolStatus(tool, detail.isEmpty() ? status : status + " · " + detail);
    if (auto* item = m_toolPlanItems.value(callId, nullptr))
        item->setText((success ? "✓  " : "✗  ") + ToolDisplayName(tool) + " · " + status);
}

void AgentPanel::SetAgentOutput(const QString& output) {
    if (m_resultEdit) m_resultEdit->setPlainText(output);
}

void AgentPanel::SetProviderMode(const QString& provider) {
    if (!m_providerCombo) return;
    const int index = m_providerCombo->findData(provider);
    if (index >= 0 && index != m_providerCombo->currentIndex()) {
        const QSignalBlocker blocker(m_providerCombo);
        m_providerCombo->setCurrentIndex(index);
    }
    if (m_providerHint) m_providerHint->setText(provider == "cloud"
        ? "豆包 API：任务文本和工具结果会发送；转写时上传音频。"
        : "本地模型：任务内容留在本机。");
}

void AgentPanel::SetProviderStatus(const QString& provider, bool modelReady, const QString& status) {
    m_runtimeLabel->setText(modelReady ? (provider == "cloud" ? "豆包就绪" : "本地就绪") : "模型待配置");
    m_runtimeLabel->setStyleSheet(modelReady
        ? "color: #9ff0d0; background: rgba(16,185,129,0.13); border: 1px solid rgba(16,185,129,0.3); border-radius: 8px; padding: 5px 8px; font-size: 10px;"
        : "color: #f6c453; background: rgba(234,179,8,0.12); border: 1px solid rgba(234,179,8,0.28); border-radius: 8px; padding: 5px 8px; font-size: 10px;");
    if (m_statusLabel) m_statusLabel->setText(status);
}

void AgentPanel::SetToolStatus(const QString& tool, const QString& status) {
    if (auto* label = m_toolStatusLabels.value(tool, nullptr))
        label->setText(ToolDisplayName(tool) + "    ·    " + status);
}
