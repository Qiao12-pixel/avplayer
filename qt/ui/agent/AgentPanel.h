#ifndef QT_UI_AGENT_AGENTPANEL_H
#define QT_UI_AGENT_AGENTPANEL_H

#include <QWidget>
#include <QJsonArray>
#include <QHash>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QVBoxLayout;
class QListWidgetItem;

class AgentPanel final : public QWidget {
    Q_OBJECT
public:
    explicit AgentPanel(QWidget* parent = nullptr);
    void SetRuntimeStatus(const QString& status, bool ready);
    void SetPlan(const QJsonArray& steps);
    void SetAgentOutput(const QString& output);
    void SetProviderMode(const QString& provider);
    void SetProviderStatus(const QString& provider, bool modelReady, const QString& status);
    void ResetExecution();
    void BeginToolExecution(const QString& tool, const QString& callId = {});
    void CompleteToolExecution(const QString& tool, const QString& callId, bool success,
                               const QString& detail = {});
    void SetToolStatus(const QString& tool, const QString& status);

signals:
    void goalSubmitted(const QString& goal);
    void stopRequested();
    void providerModeChanged(const QString& provider);

private:
    QLineEdit* m_goalEdit{nullptr};
    QPushButton* m_runButton{nullptr};
    QLabel* m_runtimeLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QListWidget* m_planList{nullptr};
    QPlainTextEdit* m_resultEdit{nullptr};
    QComboBox* m_providerCombo{nullptr};
    QLabel* m_providerHint{nullptr};
    QVBoxLayout* m_toolLayout{nullptr};
    QHash<QString, QLabel*> m_toolStatusLabels;
    QHash<QString, QListWidgetItem*> m_toolPlanItems;
};

#endif // QT_UI_AGENT_AGENTPANEL_H
