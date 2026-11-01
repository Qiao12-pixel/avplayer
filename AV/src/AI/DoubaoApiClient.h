#ifndef AV_AI_DOUBAOAPICLIENT_H
#define AV_AI_DOUBAOAPICLIENT_H

#include <QObject>
#include <QNetworkAccessManager>

namespace av::ai {
    class DoubaoApiClient final : public QObject {
        Q_OBJECT
    public:
        explicit DoubaoApiClient(QObject* parent = nullptr);
        void SetApiKey(const QString& apiKey);
        void SetEndpoint(const QString& endpoint);
        void SetModel(const QString& model);
        bool IsConfigured() const;
        void Chat(const QString& systemPrompt, const QString& userPrompt);

    signals:
        void responseReady(const QString& text);
        void requestFailed(const QString& message);

    private:
        QNetworkAccessManager m_network;
        QString m_apiKey;
        QString m_endpoint{"https://ark.cn-beijing.volces.com/api/v3/chat/completions"};
        QString m_model;
    };
}

#endif // AV_AI_DOUBAOAPICLIENT_H
