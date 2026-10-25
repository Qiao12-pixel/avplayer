#include "DoubaoApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace av::ai {
    DoubaoApiClient::DoubaoApiClient(QObject* parent) : QObject(parent) {}

    void DoubaoApiClient::SetApiKey(const QString& apiKey) { m_apiKey = apiKey.trimmed(); }
    void DoubaoApiClient::SetEndpoint(const QString& endpoint) {
        if (!endpoint.trimmed().isEmpty()) m_endpoint = endpoint.trimmed();
    }
    void DoubaoApiClient::SetModel(const QString& model) { m_model = model.trimmed(); }
    bool DoubaoApiClient::IsConfigured() const {
        return !m_apiKey.isEmpty() && !m_model.isEmpty() && !m_endpoint.isEmpty();
    }

    void DoubaoApiClient::Chat(const QString& systemPrompt, const QString& userPrompt) {
        if (!IsConfigured()) {
            emit requestFailed("豆包 API 未配置：请检查 config/secret_key.yaml 中的方舟密钥和模型 ID。");
            return;
        }
        QNetworkRequest request{QUrl(m_endpoint)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(90000);
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());

        QJsonArray messages;
        messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
        messages.append(QJsonObject{{"role", "user"}, {"content", userPrompt}});
        const QJsonObject body{
            {"model", m_model},
            {"messages", messages},
            {"temperature", 0.2},
            {"stream", false}
        };
        auto* reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray payload = reply->readAll();
            const auto error = reply->error();
            const QString networkMessage = reply->errorString();
            reply->deleteLater();
            if (error != QNetworkReply::NoError) {
                const QString apiError = QJsonDocument::fromJson(payload).object().value("error").toObject().value("message").toString();
                if (apiError.contains("does not support this api", Qt::CaseInsensitive)) {
                    emit requestFailed("当前 doubao_model 不支持 Chat API。摘要、语义搜索和智能剪辑需要已开通的文本对话模型，例如 doubao-seed-2-0-lite-260428；Seedream 是图片生成模型。");
                    return;
                }
                emit requestFailed(apiError.isEmpty() ? networkMessage : apiError);
                return;
            }
            QJsonParseError parseError{};
            const auto document = QJsonDocument::fromJson(payload, &parseError);
            const auto root = document.object();
            const auto choices = root.value("choices").toArray();
            if (parseError.error != QJsonParseError::NoError || choices.isEmpty()) {
                emit requestFailed("豆包 API 返回格式无法识别。");
                return;
            }
            const QString content = choices.first().toObject().value("message").toObject().value("content").toString();
            if (content.isEmpty()) {
                emit requestFailed("豆包 API 返回了空内容。");
                return;
            }
            emit responseReady(content);
        });
    }
}
