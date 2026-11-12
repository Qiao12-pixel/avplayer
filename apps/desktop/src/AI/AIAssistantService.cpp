#include "AIAssistantService.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include <utility>

namespace av::ai {

AIAssistantService::AIAssistantService(IPlayerControl& control, QObject* parent)
    : QObject(parent), m_playerControl(control) {
    m_configurationStatus = "Agent 服务尚未连接。";
    connect(&m_exportProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            emit taskFailed("智能剪辑导出", QString("导出第 %1 段失败，请检查 FFmpeg 与源文件。").arg(m_exportIndex + 1));
            return;
        }
        ++m_exportIndex;
        ExportNext();
    });
}

void AIAssistantService::SetConfiguration(bool modelReady, bool speechReady, const QString& status) {
    m_modelConfigured = modelReady;
    m_speechConfigured = speechReady;
    m_configurationStatus = status;
}

void AIAssistantService::UpdateMediaContext(MediaContext context) {
    m_mediaContext = std::move(context);
    const QString path = m_mediaContext.loaded ? QString::fromStdString(m_mediaContext.source) : QString{};
    if (path == m_mediaPath) return;
    m_mediaPath = path;
    m_editSegments.clear();
    emit editPlanReady(false);
    if (m_transcript.LoadSidecar(path)) emit transcriptLoaded(m_transcript.SourcePath());
    else emit transcriptLoaded({});
}

bool AIAssistantService::TogglePlayback() {
    if (!m_mediaContext.loaded) return false;
    m_playerControl.TogglePlayback();
    return true;
}

bool AIAssistantService::SeekToStart() {
    return m_mediaContext.loaded && m_playerControl.SeekToStart();
}

void AIAssistantService::RequestTranscription() {
    if (m_mediaPath.isEmpty() || !QFileInfo(m_mediaPath).isFile()) {
        emit taskFailed("字幕转写", "请先打开本地视频。");
        return;
    }
    emit taskStarted("字幕转写");
    emit modelTaskRequested("transcribe_subtitle", {{"source_path", m_mediaPath}});
}

void AIAssistantService::RequestSummary() {
    emit taskStarted("视频摘要");
    emit modelTaskRequested("summarize_video", {});
}

void AIAssistantService::RequestSemanticSearch(const QString& query) {
    if (query.trimmed().isEmpty()) { emit taskFailed("语义搜索", "请输入要查找的内容。"); return; }
    emit taskStarted("语义搜索");
    emit modelTaskRequested("semantic_search", {{"query", query}});
}

void AIAssistantService::RequestEditPlan(const QString& instruction) {
    if (instruction.trimmed().isEmpty()) { emit taskFailed("智能剪辑", "请输入剪辑目标。"); return; }
    m_editSegments.clear();
    emit editPlanReady(false);
    emit taskStarted("智能剪辑");
    emit modelTaskRequested("smart_edit", {{"instruction", instruction}});
}

bool AIAssistantService::SaveRecognition(const QJsonArray& segments, const QString& sourcePath, QString* error) {
    if (sourcePath != m_mediaPath || !QFileInfo(sourcePath).isFile()) {
        if (error) *error = "媒体已切换或不是本地文件。";
        return false;
    }
    QVector<TranscriptSegment> parsed;
    for (const auto& value : segments) {
        const auto item = value.toObject();
        const qint64 start = item.value("start_ms").toInteger(-1);
        const qint64 end = item.value("end_ms").toInteger(-1);
        if (start >= 0 && end > start && !item.value("text").toString().trimmed().isEmpty())
            parsed.append({start, end, item.value("text").toString()});
    }
    TranscriptStore saved;
    if (!saved.SaveRecognition(sourcePath, parsed, error)) return false;
    m_transcript.LoadSidecar(sourcePath);
    emit transcriptLoaded(m_transcript.SourcePath());
    return true;
}

bool AIAssistantService::ApplyServerTranscript(const QJsonArray& segments, const QString& sourcePath, QString* error) {
    if (sourcePath != m_mediaPath || !QFileInfo(sourcePath).isFile()) {
        if (error) *error = "媒体已切换或不是本地文件。";
        return false;
    }
    QVector<TranscriptSegment> parsed;
    for (const auto& value : segments) {
        const auto item = value.toObject();
        const qint64 start = item.value("start_ms").toInteger(-1);
        const qint64 end = item.value("end_ms").toInteger(-1);
        const QString text = item.value("text").toString().trimmed();
        if (start >= 0 && end > start && !text.isEmpty())
            parsed.append({start, end, text});
    }
    if (parsed.isEmpty()) {
        if (error) *error = "服务端没有返回有效的字幕片段。";
        return false;
    }
    m_editSegments.clear();
    emit editPlanReady(false);
    m_transcript.SetSegments(parsed, sourcePath);
    emit transcriptLoaded(m_transcript.SourcePath());
    return true;
}

bool AIAssistantService::ApplyServerEditPlan(const QJsonArray& segments, const QString& sourcePath, QString* error) {
    return ApplyEditPlan(segments, sourcePath, error);
}

bool AIAssistantService::ApplyEditPlan(const QJsonArray& segments, const QString& sourcePath, QString* error) {
    if (sourcePath != m_mediaPath || !QFileInfo(sourcePath).isFile() || !m_transcript.HasTimestamps()) {
        if (error) *error = "媒体已切换，或当前媒体没有带时间戳的字幕。";
        return false;
    }
    QVector<EditSegment> parsed;
    for (const auto& value : segments) {
        const auto item = value.toObject();
        const qint64 start = item.value("start_ms").toInteger(-1);
        const qint64 end = item.value("end_ms").toInteger(-1);
        if (start >= 0 && end > start && end <= m_transcript.MaxEndMs() && end - start <= 10 * 60 * 1000)
            parsed.append({start, end, item.value("reason").toString()});
    }
    if (parsed.isEmpty() || parsed.size() > 5) {
        if (error) *error = "模型没有返回 1 至 5 个有效的毫秒时间段。";
        return false;
    }
    m_editSegments = std::move(parsed);
    emit editPlanReady(true);
    return true;
}

void AIAssistantService::ExportEditPlan(const QString& outputDir) {
    if (m_editSegments.isEmpty() || !QFileInfo(m_mediaPath).isFile() || !QDir(outputDir).exists()) {
        emit taskFailed("智能剪辑导出", "没有可导出的剪辑建议或输出目录无效。");
        return;
    }
    if (m_exportProcess.state() != QProcess::NotRunning) {
        emit taskFailed("智能剪辑导出", "正在导出片段。");
        return;
    }
    m_exportDir = outputDir;
    m_exportMediaPath = m_mediaPath;
    m_exportIndex = 0;
    emit taskStarted("智能剪辑导出");
    ExportNext();
}

void AIAssistantService::ExportNext() {
    if (m_exportIndex >= m_editSegments.size()) {
        emit taskFinished("智能剪辑导出", QString("已导出 %1 段至 %2").arg(m_exportIndex).arg(m_exportDir));
        return;
    }
    const auto& segment = m_editSegments[m_exportIndex];
    const QString base = QString("clip_%1").arg(m_exportIndex + 1, 2, 10, QChar('0'));
    QString path = QDir(m_exportDir).filePath(base + ".mp4");
    for (int suffix = 2; QFileInfo::exists(path); ++suffix)
        path = QDir(m_exportDir).filePath(base + "_" + QString::number(suffix) + ".mp4");
    m_exportProcess.start("ffmpeg", {"-nostdin", "-n", "-ss", QString::number(segment.startMs / 1000.0, 'f', 3),
                                     "-i", m_exportMediaPath, "-t", QString::number((segment.endMs - segment.startMs) / 1000.0, 'f', 3),
                                     "-c:v", "libx264", "-c:a", "aac", "-movflags", "+faststart", path});
    if (!m_exportProcess.waitForStarted(1000)) emit taskFailed("智能剪辑导出", "无法启动 ffmpeg，请安装并将其加入 PATH。");
}

} // namespace av::ai
