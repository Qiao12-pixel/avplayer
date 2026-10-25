#include "AIConfig.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
namespace av::ai {
namespace {
// The secret file intentionally accepts a small, explicit YAML subset: flat string mappings.
// This keeps credential loading independent of another library and rejects ambiguous syntax.
QString ParseScalar(const QString& raw, bool* ok) {
    QString value = raw.trimmed();
    if (value.isEmpty() || value.startsWith('#')) return {};
    if (value.startsWith('"') || value.startsWith('\'')) {
        const QChar quote = value[0];
        QString parsed;
        int i = 1;
        for (; i < value.size(); ++i) {
            const QChar ch = value[i];
            if (ch == quote) { ++i; break; }
            if (quote == '"' && ch == '\\' && i + 1 < value.size()) {
                const QChar next = value[++i];
                if (next == '"' || next == '\\') parsed += next;
                else { *ok = false; return {}; }
            } else if (quote == '\'' && ch == '\'' && i + 1 < value.size() && value[i+1] == '\'') {
                parsed += '\''; ++i;
            } else parsed += ch;
        }
        if (i > value.size() || value[i-1] != quote) { *ok = false; return {}; }
        const QString tail = value.mid(i).trimmed();
        if (!tail.isEmpty() && !tail.startsWith('#')) { *ok = false; return {}; }
        return parsed;
    }
    const int comment = value.indexOf(" #");
    if (comment >= 0) value = value.left(comment).trimmed();
    if (value.contains('[') || value.contains('{') || value.contains('\n')) { *ok = false; return {}; }
    return value;
}
}
AIConfig AIConfig::Load() {
    AIConfig config;
    const QString filename = "config/secret_key.yaml";
    QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(filename),
        QDir::current().filePath(filename)
    };
#ifdef AVPLAYER_PROJECT_ROOT
    candidates.append(QDir(QStringLiteral(AVPLAYER_PROJECT_ROOT)).filePath(filename));
#endif
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isFile()) { config.sourcePath = QFileInfo(candidate).absoluteFilePath(); break; }
    }
    if (config.sourcePath.isEmpty()) { config.error = "未找到 config/secret_key.yaml；请参考 config/secret_key.example.yaml。"; return config; }
    QFile file(config.sourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { config.error = "无法读取 config/secret_key.yaml。"; return config; }
    const QString contents = QString::fromUtf8(file.readAll());
    QHash<QString, QString> values;
    const QSet<QString> allowed{"doubao_api_key", "doubao_model", "doubao_speech_api_key", "doubao_chat_endpoint"};
    const QStringList lines = contents.split('\n');
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        QString line = lines[lineNumber];
        if (lineNumber == 0 && line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
        if (line.trimmed().isEmpty() || line.trimmed().startsWith('#')) continue;
        if (line.startsWith(' ') || line.startsWith('\t')) { config.error = QString("配置文件第 %1 行：仅支持顶层键值对。").arg(lineNumber+1); return config; }
        const int colon = line.indexOf(':');
        if (colon < 0) { config.error = QString("配置文件第 %1 行：缺少冒号。").arg(lineNumber+1); return config; }
        const QString key = line.left(colon).trimmed();
        if (!allowed.contains(key) || values.contains(key)) { config.error = QString("配置文件第 %1 行：字段未知或重复。").arg(lineNumber+1); return config; }
        bool ok = true;
        const QString value = ParseScalar(line.mid(colon+1), &ok);
        if (!ok) { config.error = QString("配置文件第 %1 行：值格式无效。").arg(lineNumber+1); return config; }
        values.insert(key, value);
    }
    if (values.isEmpty()) { config.error = "config/secret_key.yaml 为空；请参考示例文件填写。"; return config; }
    config.chatApiKey = values.value("doubao_api_key");
    config.chatModel = values.value("doubao_model");
    config.speechApiKey = values.value("doubao_speech_api_key");
    if (!values.value("doubao_chat_endpoint").isEmpty()) config.chatEndpoint = values.value("doubao_chat_endpoint");
    QStringList missing;
    if (config.chatApiKey.isEmpty()) missing.append("doubao_api_key");
    if (config.chatModel.isEmpty()) missing.append("doubao_model");
    if (config.speechApiKey.isEmpty()) missing.append("doubao_speech_api_key");
    if (!missing.isEmpty()) config.error = "配置未完成：请填写 " + missing.join("、") + "。";
    else if (config.chatApiKey == config.speechApiKey)
        config.error = "方舟和语音密钥填写了相同的值；请核对 doubao_speech_api_key 是否来自豆包语音控制台的 API Key 管理。";
    return config;
}
}
