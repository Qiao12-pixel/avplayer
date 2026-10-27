#include "AgentPanel.h"

#include <QFrame>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
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
    m_planList->setFixedHeight(154);
    m_planList->addItems({"○ 读取当前媒体上下文", "○ 分析字幕与时间轴", "○ 调用媒体工具执行任务", "○ 汇总结果并返回文件"});
    contentLayout->addWidget(m_planList);

    contentLayout->addWidget(SectionTitle("工具调用", content));
    auto* tools = new QVBoxLayout();
    tools->setSpacing(6);
    for (const auto& tool : {QString("字幕转写"), QString("语义搜索"), QString("截图工具"), QString("剪辑导出"), QString("随机片段导出")}) {
        auto* row = ToolStatusRow(tool + "    ·    待调用", content);
        m_toolStatusLabels.insert(tool, row);
        tools->addWidget(row);
    }
    contentLayout->addLayout(tools);
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

void AgentPanel::SetAgentOutput(const QString& output) {
    if (m_resultEdit) m_resultEdit->setPlainText(output);
}

void AgentPanel::SetToolStatus(const QString& tool, const QString& status) {
    if (auto* label = m_toolStatusLabels.value(tool, nullptr))
        label->setText(tool + "    ·    " + status);
}
