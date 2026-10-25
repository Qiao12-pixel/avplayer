#ifndef AV_AI_TRANSCRIPTSTORE_H
#define AV_AI_TRANSCRIPTSTORE_H
#include <QString>
#include <QVector>
namespace av::ai {
struct TranscriptSegment { qint64 startMs{0}; qint64 endMs{0}; QString text; };
class TranscriptStore final {
public:
    bool LoadSidecar(const QString& mediaPath);
    bool SaveRecognition(const QString& mediaPath, const QVector<TranscriptSegment>& segments, QString* error);
    void Clear();
    bool IsLoaded() const { return !m_text.isEmpty(); }
    bool HasTimestamps() const { return !m_segments.isEmpty(); }
    qint64 MaxEndMs() const;
    const QString& Text() const { return m_text; }
    const QString& SourcePath() const { return m_sourcePath; }
    const QVector<TranscriptSegment>& Segments() const { return m_segments; }
private:
    QString m_text;
    QString m_sourcePath;
    QVector<TranscriptSegment> m_segments;
};
}
#endif
