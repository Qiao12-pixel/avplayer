#include "ClipExportService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUuid>

namespace av::agent {

ClipExportService::ClipExportService(QObject* parent) : QObject(parent) {
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errorOutput += m_process.readAllStandardError();
        if (m_errorOutput.size() > 4096) m_errorOutput = m_errorOutput.right(4096);
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        if (m_canceled) {
            QFile::remove(m_outputPath);
            return;
        }
        const QFileInfo output(m_outputPath);
        if (status == QProcess::NormalExit && code == 0 && output.isFile() && output.size() > 0) {
            emit completed({{"success", true}, {"path", m_outputPath},
                            {"start_ms", m_startMs}, {"duration_ms", m_durationMs}});
            return;
        }
        QFile::remove(m_outputPath);
        const QString detail = QString::fromUtf8(m_errorOutput).trimmed();
        emit completed({{"success", false}, {"error", detail.isEmpty()
                            ? QString("FFmpeg 导出失败（退出码 %1）。").arg(code)
                            : detail.right(800)}});
    });
}

bool ClipExportService::StartRandomClip(const QString& sourcePath, qint64 sourceDurationMs,
                                        qint64 clipDurationMs, const QString& outputDir,
                                        QString* error) {
    auto fail = [error](const QString& reason) {
        if (error) *error = reason;
        return false;
    };
    if (m_process.state() != QProcess::NotRunning) return fail("已有片段正在导出。");
    if (!QFileInfo(sourcePath).isFile()) return fail("随机导出只支持已打开的本地视频文件。");
    if (clipDurationMs <= 0 || clipDurationMs > 10 * 60 * 1000)
        return fail("片段时长必须在 1 毫秒到 10 分钟之间。");
    if (sourceDurationMs <= 0 || sourceDurationMs < clipDurationMs)
        return fail("视频时长不足，无法导出指定长度的片段。");

    QString directory = outputDir.trimmed();
    if (directory.isEmpty()) {
        const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        directory = QDir(movies.isEmpty() ? QDir::homePath() : movies).filePath("AVPlayer");
    }
    if (!QDir().mkpath(directory)) return fail("无法创建导出目录：" + directory);
    const qint64 lastStart = sourceDurationMs - clipDurationMs;
    m_startMs = lastStart == 0 ? 0 : static_cast<qint64>(
        QRandomGenerator::global()->generate64() % (static_cast<quint64>(lastStart) + 1));
    m_durationMs = clipDurationMs;
    m_outputPath = QDir(directory).filePath(
        "random_clip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") +
        "_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + ".mp4");
    m_errorOutput.clear();
    m_canceled = false;
    m_process.start("ffmpeg", {"-nostdin", "-y", "-ss", QString::number(m_startMs / 1000.0, 'f', 3),
                               "-i", sourcePath, "-t", QString::number(clipDurationMs / 1000.0, 'f', 3),
                               "-map", "0:v:0", "-map", "0:a:0?", "-c:v", "libx264",
                               "-c:a", "aac", "-movflags", "+faststart", m_outputPath});
    if (!m_process.waitForStarted(3000)) {
        QFile::remove(m_outputPath);
        return fail("无法启动 FFmpeg：" + m_process.errorString());
    }
    return true;
}

void ClipExportService::Cancel() {
    if (m_process.state() == QProcess::NotRunning) return;
    m_canceled = true;
    m_process.kill();
    m_process.waitForFinished(3000);
    QFile::remove(m_outputPath);
}

} // namespace av::agent
