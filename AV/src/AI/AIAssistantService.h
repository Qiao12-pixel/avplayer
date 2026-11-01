#ifndef AV_AI_AIASSISTANTSERVICE_H
#define AV_AI_AIASSISTANTSERVICE_H
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>
#include <string>
#include "IPlayerControl.h"
#include "TranscriptStore.h"
#include <QJsonArray>
namespace av::ai {
struct MediaContext {
    std::string source;
    bool loaded{false}; bool playing{false}; bool recording{false};
    int width{0}; int height{0};
};
struct EditSegment { qint64 startMs{0}; qint64 endMs{0}; QString reason; };
class AIAssistantService final : public QObject {
    Q_OBJECT
public:
    explicit AIAssistantService(IPlayerControl& playerControl, QObject* parent = nullptr);
    void SetConfiguration(bool modelReady, bool speechReady, const QString& status);
    void UpdateMediaContext(MediaContext context);
    const MediaContext& GetMediaContext() const { return m_mediaContext; }
    bool TogglePlayback();
    bool SeekToStart();
    bool IsModelConfigured() const { return m_modelConfigured; }
    bool IsSpeechConfigured() const { return m_speechConfigured; }
    const QString& ConfigurationStatus() const { return m_configurationStatus; }
    bool HasTranscript() const { return m_transcript.IsLoaded(); }
    const QVector<TranscriptSegment>& TranscriptSegments() const { return m_transcript.Segments(); }
    bool HasEditPlan() const { return !m_editSegments.isEmpty(); }
    QString TranscriptText() const { return m_transcript.Text(); }
    qint64 TranscriptMaxEndMs() const { return m_transcript.MaxEndMs(); }
    bool SaveRecognition(const QJsonArray& segments, const QString& sourcePath, QString* error);
    bool ApplyEditPlan(const QJsonArray& segments, const QString& sourcePath, QString* error);
    void RequestTranscription();
    void RequestSummary();
    void RequestSemanticSearch(const QString& query);
    void RequestEditPlan(const QString& instruction);
    void ExportEditPlan(const QString& outputDir);
signals:
    void modelTaskRequested(const QString& task, const QJsonObject& arguments);
    void transcriptLoaded(const QString& sourcePath);
    void taskStarted(const QString& taskName);
    void taskProgress(const QString& taskName, const QString& message);
    void taskFinished(const QString& taskName, const QString& result);
    void taskFailed(const QString& taskName, const QString& message);
    void editPlanReady(bool available);
private:
    void ExportNext();
    IPlayerControl& m_playerControl;
    MediaContext m_mediaContext;
    TranscriptStore m_transcript;
    bool m_modelConfigured{false};
    bool m_speechConfigured{false};
    QString m_configurationStatus;
    QString m_mediaPath;
    QString m_exportMediaPath;
    QVector<EditSegment> m_editSegments;
    QProcess m_exportProcess;
    QString m_exportDir;
    int m_exportIndex{0};
};
}
#endif
