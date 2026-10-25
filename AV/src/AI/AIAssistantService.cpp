#include "AIAssistantService.h"
#include "AIConfig.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <utility>
namespace av::ai {
AIAssistantService::AIAssistantService(IPlayerControl& control, QObject* parent)
    : QObject(parent), m_playerControl(control), m_doubao(this), m_speech(this) {
    const AIConfig config = AIConfig::Load();
    m_configurationStatus = config.error.isEmpty() ? "已从 " + config.sourcePath + " 加载豆包配置（密钥尚未验证）。" : config.error;
    m_doubao.SetApiKey(config.chatApiKey);
    m_doubao.SetEndpoint(config.chatEndpoint);
    m_doubao.SetModel(config.chatModel);
    m_speech.SetApiKey(config.speechApiKey);
    connect(&m_doubao, &DoubaoApiClient::responseReady, this, [this](const QString& text) {
        const QString task = std::exchange(m_activeTask, QString{});
        if (m_chatMediaPath != m_mediaPath) { emit taskFailed(task, "媒体已切换，请针对当前视频重新操作。"); return; }
        if (task == "智能剪辑") {
            QString jsonText = text.trimmed();
            if (jsonText.startsWith("```")) { jsonText = jsonText.mid(jsonText.indexOf('\n') + 1); jsonText = jsonText.left(jsonText.lastIndexOf("```")).trimmed(); }
            const auto document = QJsonDocument::fromJson(jsonText.toUtf8());
            const auto list = document.object().value("segments").toArray();
            m_editSegments.clear();
            for (const auto& value : list) {
                const auto segment = value.toObject();
                const qint64 start = segment.value("start_ms").toInteger(-1);
                const qint64 end = segment.value("end_ms").toInteger(-1);
                if (start >= 0 && end > start && end <= m_transcript.MaxEndMs() && end - start <= 10*60*1000) m_editSegments.append({start, end, segment.value("reason").toString()});
            }
            emit editPlanReady(!m_editSegments.isEmpty());
            if (m_editSegments.isEmpty()) { emit taskFailed(task, "模型没有返回有效的毫秒时间段，请重试或调整要求。"); return; }
            QString preview = "建议片段（点击“导出建议片段”保存）：\n";
            for (int i=0; i<m_editSegments.size(); ++i) {
                const auto& s = m_editSegments[i];
                preview += QString("%1. %2s–%3s  %4\n").arg(i+1).arg(s.startMs/1000.0,0,'f',1).arg(s.endMs/1000.0,0,'f',1).arg(s.reason);
            }
            emit taskFinished(task, preview);
        } else emit taskFinished(task, text);
    });
    connect(&m_doubao, &DoubaoApiClient::requestFailed, this, [this](const QString& message) { emit taskFailed(std::exchange(m_activeTask, QString{}), message); });
    connect(&m_speech, &DoubaoSpeechClient::recognized, this, [this](const QVector<TranscriptSegment>& segments) {
        QString error;
        TranscriptStore saved;
        if (!saved.SaveRecognition(m_transcribePath, segments, &error)) { emit taskFailed("字幕转写", error); return; }
        if (m_mediaPath != m_transcribePath) { emit taskFinished("字幕转写", "字幕已保存至原视频旁：" + saved.SourcePath()); return; }
        m_transcript.LoadSidecar(m_mediaPath);
        emit transcriptLoaded(m_transcript.SourcePath());
        emit taskFinished("字幕转写", QString("已生成 %1 条字幕：%2").arg(segments.size()).arg(m_transcript.SourcePath()));
    });
    connect(&m_speech, &DoubaoSpeechClient::stageChanged, this, [this](const QString& message) {
        emit taskProgress("字幕转写", message);
    });
    connect(&m_speech, &DoubaoSpeechClient::failed, this, [this](const QString& error) { emit taskFailed("字幕转写", error); });
    connect(&m_exportProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) { emit taskFailed("智能剪辑", QString("导出第 %1 段失败，请检查 FFmpeg 与源文件。").arg(m_exportIndex+1)); return; }
        ++m_exportIndex; ExportNext();
    });
}
void AIAssistantService::UpdateMediaContext(MediaContext context) {
    m_mediaContext = std::move(context);
    const QString path = m_mediaContext.loaded ? QString::fromStdString(m_mediaContext.source) : QString{};
    if (path == m_mediaPath) return;
    m_mediaPath = path; m_editSegments.clear(); emit editPlanReady(false);
    if (m_transcript.LoadSidecar(path)) emit transcriptLoaded(m_transcript.SourcePath());
    else emit transcriptLoaded({});
}
bool AIAssistantService::TogglePlayback() { if (!m_mediaContext.loaded) return false; m_playerControl.TogglePlayback(); return true; }
bool AIAssistantService::SeekToStart() { if (!m_mediaContext.loaded) return false; return m_playerControl.SeekToStart(); }
void AIAssistantService::RequestTranscription() {
    if (m_speech.IsBusy()) return;
    if (m_mediaPath.isEmpty()) { emit taskFailed("字幕转写", "请先打开本地视频。"); return; }
    m_transcribePath = m_mediaPath; emit taskStarted("字幕转写"); m_speech.Transcribe(m_mediaPath);
}
bool AIAssistantService::BeginChat(const QString& name) {
    if (!m_activeTask.isEmpty()) { emit taskFailed(name, "另一个豆包请求正在执行，请稍后重试。"); return false; }
    if (!m_doubao.IsConfigured()) { emit taskFailed(name, m_configurationStatus); return false; }
    if (!m_transcript.IsLoaded()) { emit taskFailed(name, "请先转写字幕，或在视频旁放置同名 .srt/.txt 文件。"); return false; }
    if (m_transcript.Text().size() > 20000) { emit taskFailed(name, "字幕超过 2 万字；当前版本不支持长视频分段分析。"); return false; }
    m_activeTask = name; m_chatMediaPath = m_mediaPath; emit taskStarted(name); return true;
}
void AIAssistantService::RequestSummary() {
    if (!BeginChat("视频摘要")) return;
    m_doubao.Chat("你是视频内容分析助手。仅依据提供的字幕生成中文摘要，按主题列重点。不要编造画面信息。", "字幕：\n" + m_transcript.Text());
}
void AIAssistantService::RequestSemanticSearch(const QString& query) {
    if (query.trimmed().isEmpty()) { emit taskFailed("语义搜索", "请输入要查找的内容。"); return; }
    if (!BeginChat("语义搜索")) return;
    m_doubao.Chat("你是字幕语义搜索助手。找出与问题最相关的原文句子，保留字幕里的时间戳；找不到则明确说明。不要编造内容。", "查询：" + query + "\n字幕：\n" + m_transcript.Text());
}
void AIAssistantService::RequestEditPlan(const QString& instruction) {
    if (instruction.trimmed().isEmpty()) { emit taskFailed("智能剪辑", "请输入剪辑目标。"); return; }
    if (!m_transcript.HasTimestamps()) { emit taskFailed("智能剪辑", "剪辑需要带时间戳的 .srt 字幕，请先转写视频。"); return; }
    if (!QFileInfo::exists(m_mediaPath)) { emit taskFailed("智能剪辑", "剪辑导出仅支持本地媒体。"); return; }
    if (!BeginChat("智能剪辑")) return;
    m_editSegments.clear(); emit editPlanReady(false);
    m_doubao.Chat("你是视频剪辑助手。只根据字幕时间轴选取最多5段与目标相关的连续片段。仅返回严格 JSON：{\"segments\":[{\"start_ms\":整数,\"end_ms\":整数,\"reason\":字符串}]}。单位毫秒；时间必须位于字幕覆盖范围内，不要推测画面内容。", "目标：" + instruction + "\n字幕：\n" + m_transcript.Text());
}
void AIAssistantService::ExportEditPlan(const QString& outputDir) {
    if (m_editSegments.isEmpty() || !QFileInfo::exists(m_mediaPath) || !QDir(outputDir).exists()) { emit taskFailed("智能剪辑", "没有可导出的剪辑建议或输出目录无效。"); return; }
    if (m_exportProcess.state() != QProcess::NotRunning) { emit taskFailed("智能剪辑", "正在导出片段。"); return; }
    m_exportDir = outputDir; m_exportMediaPath = m_mediaPath; m_exportIndex = 0; emit taskStarted("智能剪辑导出"); ExportNext();
}
void AIAssistantService::ExportNext() {
    if (m_exportIndex >= m_editSegments.size()) { emit taskFinished("智能剪辑导出", QString("已导出 %1 段至 %2").arg(m_exportIndex).arg(m_exportDir)); return; }
    const auto& s = m_editSegments[m_exportIndex];
    const QString base = QString("clip_%1").arg(m_exportIndex+1, 2, 10, QChar('0'));
    QString path = QDir(m_exportDir).filePath(base + ".mp4");
    for (int suffix=2; QFileInfo::exists(path); ++suffix) path = QDir(m_exportDir).filePath(base + "_" + QString::number(suffix) + ".mp4");
    m_exportProcess.start("ffmpeg", {"-nostdin", "-n", "-ss", QString::number(s.startMs/1000.0,'f',3), "-i", m_exportMediaPath, "-t", QString::number((s.endMs-s.startMs)/1000.0,'f',3), "-c:v", "libx264", "-c:a", "aac", "-movflags", "+faststart", path});
    if (!m_exportProcess.waitForStarted(1000)) emit taskFailed("智能剪辑", "无法启动 ffmpeg，请安装并将其加入 PATH。");
}
}
