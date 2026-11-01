#ifndef AV_AGENT_AUDIOEXTRACTIONSERVICE_H
#define AV_AGENT_AUDIOEXTRACTIONSERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QProcess>

namespace av::agent {

class AudioExtractionService final : public QObject {
    Q_OBJECT
public:
    explicit AudioExtractionService(QObject* parent = nullptr);
    bool Start(const QString& sourcePath, QString* error);
    void Cancel();

signals:
    void completed(const QJsonObject& result);

private:
    QProcess m_process;
    QString m_outputPath;
    QByteArray m_errorOutput;
    bool m_canceled{false};
};

} // namespace av::agent

#endif
