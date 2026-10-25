#ifndef AV_AI_AICONFIG_H
#define AV_AI_AICONFIG_H
#include <QString>
namespace av::ai {
struct AIConfig {
    QString chatApiKey;
    QString chatModel;
    QString speechApiKey;
    QString chatEndpoint{"https://ark.cn-beijing.volces.com/api/v3/chat/completions"};
    QString sourcePath;
    QString error;
    static AIConfig Load();
};
}
#endif
