#include "DoubaoSpeechClient.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
namespace av::ai {
DoubaoSpeechClient::DoubaoSpeechClient(QObject* parent) : QObject(parent) {
    connect(&m_ffmpeg, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            m_busy = false;
            QFile::remove(m_tempPath);
            const QString detail = QString::fromUtf8(m_ffmpeg.readAllStandardError()).trimmed().section('\n', -1);
            emit failed("FFmpeg 音轨提取失败：" + (detail.isEmpty() ? QString("请确认媒体包含音频流。") : detail));
            return;
        }
        QFile file(m_tempPath);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 20*1024*1024) { m_busy = false; QFile::remove(m_tempPath); emit failed("抽取音频失败或音频超过 20 MB；请使用较短视频。"); return; }
        const QByteArray encoded = file.readAll().toBase64(); file.close(); QFile::remove(m_tempPath);
        emit stageChanged("音轨提取完成，正在上传并等待豆包识别…");
        QNetworkRequest req(QUrl("https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(180000);
        req.setRawHeader("X-Api-Key", m_apiKey.toUtf8());
        req.setRawHeader("X-Api-Resource-Id", "volc.bigasr.auc_turbo");
        req.setRawHeader("X-Api-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
        req.setRawHeader("X-Api-Sequence", "-1");
        const QJsonObject body{{"user", QJsonObject{{"uid", "avplayer"}}}, {"audio", QJsonObject{{"data", QString::fromLatin1(encoded)}}}, {"request", QJsonObject{{"model_name", "bigmodel"}}}};
        auto* reply = m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::uploadProgress, this, [this, reply, uploaded = false](qint64 sent, qint64 total) mutable {
            if (!uploaded && total > 0 && sent >= total) {
                uploaded = true;
                emit stageChanged("音频上传完成，正在等待豆包识别结果…");
            }
        });
        QTimer::singleShot(180000, reply, [reply] {
            if (reply->isRunning()) {
                reply->setProperty("recognitionTimedOut", true);
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray bytes = reply->readAll();
            const QString networkError = reply->errorString();
            const auto networkCode = reply->error();
            const bool timedOut = reply->property("recognitionTimedOut").toBool();
            const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString statusCode = QString::fromLatin1(reply->rawHeader("X-Api-Status-Code"));
            const QString apiMessage = QString::fromUtf8(reply->rawHeader("X-Api-Message"));
            const QString logId = QString::fromLatin1(reply->rawHeader("X-Tt-Logid"));
            reply->deleteLater();
            m_busy = false;
            if (timedOut) {
                emit failed("豆包识别超过 3 分钟，已中止请求；请检查网络或稍后重试。");
                return;
            }
            QJsonParseError parseError{};
            const auto json = QJsonDocument::fromJson(bytes, &parseError).object();
            if (networkCode != QNetworkReply::NoError || statusCode != "20000000") {
                if (httpCode == 401 && statusCode == "45000010") {
                    emit failed("豆包语音拒绝了 X-Api-Key。请在豆包语音控制台的 API Key 管理中创建密钥，填写到 doubao_speech_api_key；方舟 API Key 不能代替语音密钥。"
                        + (logId.isEmpty() ? QString{} : " LogID " + logId));
                    return;
                }
                QString detail = apiMessage;
                if (detail.isEmpty()) detail = json.value("message").toString();
                if (detail.isEmpty()) detail = json.value("error").toObject().value("message").toString();
                if (detail.isEmpty() && networkCode != QNetworkReply::NoError) detail = networkError;
                if (detail.isEmpty()) detail = "服务未返回错误说明";
                emit failed(QString("豆包转写失败（HTTP %1，API %2）：%3%4")
                    .arg(httpCode).arg(statusCode.isEmpty() ? "无状态码" : statusCode, detail,
                         logId.isEmpty() ? QString{} : "；LogID " + logId));
                return;
            }
            const auto utterances = json.value("result").toObject().value("utterances").toArray();
            QVector<TranscriptSegment> segments;
            for (const auto& value : utterances) {
                const auto item = value.toObject();
                segments.append({item.value("start_time").toInteger(), item.value("end_time").toInteger(), item.value("text").toString()});
            }
            if (parseError.error != QJsonParseError::NoError || segments.isEmpty()) {
                emit failed("豆包识别完成，但没有返回可用的分句时间戳。" + (logId.isEmpty() ? QString{} : " LogID " + logId));
                return;
            }
            emit recognized(segments);
        });
    });
}
void DoubaoSpeechClient::Transcribe(const QString& mediaPath) {
    if (!IsConfigured()) { emit failed("请检查 config/secret_key.yaml 中的豆包语音密钥，并开通录音文件识别极速版。"); return; }
    if (m_busy || m_ffmpeg.state() != QProcess::NotRunning) { emit failed("字幕转写正在进行中。"); return; }
    if (!QFileInfo::exists(mediaPath)) { emit failed("字幕转写仅支持本地媒体文件。"); return; }
    m_busy = true;
    emit stageChanged("正在用 FFmpeg 提取音轨…");
    m_tempPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("avplayer-asr-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mp3");
    m_ffmpeg.start("ffmpeg", {"-nostdin", "-y", "-i", mediaPath, "-vn", "-ac", "1", "-ar", "16000", "-b:a", "32k", m_tempPath});
    if (!m_ffmpeg.waitForStarted(1000)) { m_busy = false; emit failed("无法启动 ffmpeg，请安装并将其加入 PATH。"); }
}
}
