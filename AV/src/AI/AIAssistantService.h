#ifndef AV_AI_AIASSISTANTSERVICE_H
#define AV_AI_AIASSISTANTSERVICE_H
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>
#include <string>
#include "IPlayerControl.h"
#include "DoubaoApiClient.h"
#include "DoubaoSpeechClient.h"
#include "TranscriptStore.h"
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
    void UpdateMediaContext(MediaContext context);
    const MediaContext& GetMediaContext() const { return m_mediaContext; }
    bool TogglePlayback();
    bool SeekToStart();
    bool IsModelConfigured() const { return m_doubao.IsConfigured(); }
    bool IsSpeechConfigured() const { return m_speech.IsConfigured(); }
    const QString& ConfigurationStatus() const { return m_configurationStatus; }
    bool HasTranscript() const { return m_transcript.IsLoaded(); }
    const QVector<TranscriptSegment>& TranscriptSegments() const { return m_transcript.Segments(); }
    bool HasEditPlan() const { return !m_editSegments.isEmpty(); }
    void RequestTranscription();
    void RequestSummary();
    void RequestSemanticSearch(const QString& query);
    void RequestEditPlan(const QString& instruction);
    void ExportEditPlan(const QString& outputDir);
signals:
    void transcriptLoaded(const QString& sourcePath);
    void taskStarted(const QString& taskName);
    void taskProgress(const QString& taskName, const QString& message);
    void taskFinished(const QString& taskName, const QString& result);
    void taskFailed(const QString& taskName, const QString& message);
    void editPlanReady(bool available);
private:
    bool BeginChat(const QString& name);
    void ExportNext();
    IPlayerControl& m_playerControl;
    MediaContext m_mediaContext;
    DoubaoApiClient m_doubao;
    DoubaoSpeechClient m_speech;
    TranscriptStore m_transcript;
    QString m_configurationStatus;
    QString m_mediaPath;
    QString m_transcribePath;
    QString m_chatMediaPath;
    QString m_exportMediaPath;
    QString m_activeTask;
    QVector<EditSegment> m_editSegments;
    QProcess m_exportProcess;
    QString m_exportDir;
    int m_exportIndex{0};
};
}
#endif
