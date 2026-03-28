//
// Created by Joe on 26-3-15.
//

#ifndef CONTROLLERWIDGET_H
#define CONTROLLERWIDGET_H

#include <qtmetamacros.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QWidget>

class ControllerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ControllerWidget(QWidget* parent);
    ~ControllerWidget() override;

    void SetPlaying(bool playing);
    void SetRecording(bool recording);
    void SetRecordEnabled(bool enabled);

signals:
    void importRequested(const QString& filePath);
    void playToggled(bool playing);
    void recordToggled(bool recording);
    void stickerAssetSwitchRequested();
    void flipFilterToggled(bool enabled);
    void grayFilterToggled(bool enabled);
    void invertFilterToggled(bool enabled);
    void stickerFilterToggled(bool enabled);

private slots:
    void onImportBtnClicked();
    void onPlayBtnClicked();
    void onRecordBtnClicked();

private:
    QPushButton* m_btnImport{nullptr};
    QPushButton* m_btnPlay{nullptr};
    QPushButton* m_btnExport{nullptr};

    QPushButton* m_btnVideoFilterFlipVertical{nullptr};
    QPushButton* m_btnVideoFilterGray{nullptr};
    QPushButton* m_btnVideoFilterInvert{nullptr};
    QPushButton* m_btnVideoFilterSticker{nullptr};
    QPushButton* m_btnStickerAssetSwitch{nullptr};
};

#endif //CONTROLLERWIDGET_H
