#include "TranscriptStore.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
namespace av::ai {
namespace {
QString Stamp(qint64 ms) {
    const qint64 hours = ms / 3600000; ms %= 3600000;
    return QString("%1:%2:%3,%4").arg(hours, 2, 10, QChar('0')).arg(ms / 60000, 2, 10, QChar('0'))
        .arg((ms / 1000) % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}
}
qint64 TranscriptStore::MaxEndMs() const {
    qint64 latest = 0;
    for (const auto& segment : m_segments)
        latest = qMax(latest, segment.endMs);
    return latest;
}
void TranscriptStore::Clear() {
    m_text.clear();
    m_sourcePath.clear();
    m_segments.clear();
}
bool TranscriptStore::LoadSidecar(const QString& mediaPath) {
    Clear();
    const QFileInfo info(mediaPath);
    if (!info.exists() || !info.isFile()) return false;
    const QString base = info.path() + "/" + info.completeBaseName();
    for (const auto& path : {base + ".ai.srt", base + ".srt", base + ".txt"}) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString value = QString::fromUtf8(file.readAll()).trimmed();
        if (value.isEmpty()) continue;
        m_text = value; m_sourcePath = path;
        if (path.endsWith(".srt")) {
            static const QRegularExpression re(R"((\d\d:\d\d:\d\d,\d\d\d)\s*-->\s*(\d\d:\d\d:\d\d,\d\d\d))");
            auto iter = re.globalMatch(value);
            while (iter.hasNext()) {
                const auto match = iter.next();
                const int textStart = match.capturedEnd();
                const int textEnd = value.indexOf(QRegularExpression("\\n\\s*\\n"), textStart);
                const QString spoken = value.mid(textStart, (textEnd < 0 ? value.size() : textEnd) - textStart).trimmed();
                auto toMs = [](const QString& s) -> qint64 { const auto p = s.split(QRegularExpression("[:,]")); return (((p[0].toLongLong()*60 + p[1].toLongLong())*60 + p[2].toLongLong())*1000 + p[3].toLongLong()); };
                if (!spoken.isEmpty()) m_segments.append({toMs(match.captured(1)), toMs(match.captured(2)), spoken});
            }
        }
        return true;
    }
    return false;
}
bool TranscriptStore::SaveRecognition(const QString& mediaPath, const QVector<TranscriptSegment>& segments, QString* error) {
    if (segments.isEmpty()) { if (error) *error = "转写结果没有可用的时间轴分句。"; return false; }
    const QFileInfo info(mediaPath);
    const QString path = info.path() + "/" + info.completeBaseName() + ".ai.srt";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { if (error) *error = file.errorString(); return false; }
    QString content;
    int index = 1;
    for (const auto& segment : segments) {
        if (segment.text.trimmed().isEmpty() || segment.endMs <= segment.startMs) continue;
        content += QString("%1\n%2 --> %3\n%4\n\n").arg(index++).arg(Stamp(segment.startMs), Stamp(segment.endMs), segment.text.trimmed());
    }
    if (content.isEmpty() || file.write(content.toUtf8()) < 0 || !file.commit()) { if (error) *error = file.errorString(); return false; }
    return LoadSidecar(mediaPath);
}
}
