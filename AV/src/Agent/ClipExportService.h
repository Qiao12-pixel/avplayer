#ifndef AV_AGENT_CLIPEXPORTSERVICE_H
#define AV_AGENT_CLIPEXPORTSERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

namespace av::agent {

class ClipExportService final : public QObject {
    Q_OBJECT
public:
    explicit ClipExportService(QObject* parent = nullptr);
    bool StartRandomClip(const QString& sourcePath, qint64 sourceDurationMs,
                         qint64 clipDurationMs, const QString& outputDir, QString* error);
    void Cancel();

signals:
    void completed(const QJsonObject& result);

private:
    QProcess m_process;
    QString m_outputPath;
    QByteArray m_errorOutput;
    qint64 m_startMs{0};
    qint64 m_durationMs{0};
    bool m_canceled{false};
};

} // namespace av::agent

#endif // AV_AGENT_CLIPEXPORTSERVICE_H
