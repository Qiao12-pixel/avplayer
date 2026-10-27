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

class AgentPanel final : public QWidget {
    Q_OBJECT
public:
    explicit AgentPanel(QWidget* parent = nullptr);
    void SetRuntimeStatus(const QString& status, bool ready);
    void SetPlan(const QJsonArray& steps);
    void SetAgentOutput(const QString& output);
    void SetToolStatus(const QString& tool, const QString& status);

signals:
    void goalSubmitted(const QString& goal);
    void stopRequested();

private:
    QLineEdit* m_goalEdit{nullptr};
    QPushButton* m_runButton{nullptr};
    QLabel* m_runtimeLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QListWidget* m_planList{nullptr};
    QPlainTextEdit* m_resultEdit{nullptr};
    QHash<QString, QLabel*> m_toolStatusLabels;
};

#endif // QT_UI_AGENT_AGENTPANEL_H
