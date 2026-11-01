#ifndef AV_AI_DOUBAOSPEECHCLIENT_H
#define AV_AI_DOUBAOSPEECHCLIENT_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include "TranscriptStore.h"
namespace av::ai {
class DoubaoSpeechClient final : public QObject {
    Q_OBJECT
public:
    explicit DoubaoSpeechClient(QObject* parent = nullptr);
    void SetApiKey(const QString& key) { m_apiKey = key.trimmed(); }
    bool IsConfigured() const { return !m_apiKey.isEmpty(); }
    bool IsBusy() const { return m_busy; }
    void Transcribe(const QString& mediaPath);
signals:
    void stageChanged(const QString& message);
    void recognized(const QVector<TranscriptSegment>& segments);
    void failed(const QString& message);
private:
    QNetworkAccessManager m_network;
    QProcess m_ffmpeg;
    QString m_apiKey;
    QString m_tempPath;
    bool m_busy{false};
};
}
#endif
