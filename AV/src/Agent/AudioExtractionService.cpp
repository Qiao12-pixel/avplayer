#include "AudioExtractionService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

namespace av::agent {

AudioExtractionService::AudioExtractionService(QObject* parent) : QObject(parent) {
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errorOutput += m_process.readAllStandardError();
        if (m_errorOutput.size() > 4096) m_errorOutput = m_errorOutput.right(4096);
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        if (m_canceled) { QFile::remove(m_outputPath); return; }
        const QFileInfo output(m_outputPath);
        if (status == QProcess::NormalExit && code == 0 && output.isFile() &&
            output.size() > 0 && output.size() <= 20 * 1024 * 1024) {
            emit completed({{"success", true}, {"path", m_outputPath}});
            return;
        }
        QFile::remove(m_outputPath);
        emit completed({{"success", false}, {"error", output.size() > 20 * 1024 * 1024
            ? QString("音轨超过 20 MB；请使用较短视频。")
            : QString("音轨提取失败：%1").arg(QString::fromUtf8(m_errorOutput).right(400))}});
    });
}

bool AudioExtractionService::Start(const QString& sourcePath, QString* error) {
    if (m_process.state() != QProcess::NotRunning) { if (error) *error = "已有音轨正在提取。"; return false; }
    if (!QFileInfo(sourcePath).isFile()) { if (error) *error = "音轨提取需要本地视频。"; return false; }
    m_outputPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(
        "avplayer-asr-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mp3");
    m_errorOutput.clear();
    m_canceled = false;
    m_process.start("ffmpeg", {"-nostdin", "-y", "-i", sourcePath, "-vn", "-ac", "1",
                               "-ar", "16000", "-b:a", "32k", m_outputPath});
    if (!m_process.waitForStarted(3000)) {
        QFile::remove(m_outputPath);
        if (error) *error = "无法启动 FFmpeg：" + m_process.errorString();
        return false;
    }
    return true;
}

void AudioExtractionService::Cancel() {
    if (m_process.state() == QProcess::NotRunning) return;
    m_canceled = true;
    m_process.kill();
    m_process.waitForFinished(3000);
    QFile::remove(m_outputPath);
}

} // namespace av::agent
