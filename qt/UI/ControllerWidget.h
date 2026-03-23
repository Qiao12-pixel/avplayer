//
// Created by Joe on 26-3-15.
//

#ifndef CONTROLLERWIDGET_H
#define CONTROLLERWIDGET_H
#include <qtmetamacros.h>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QHBoxLayout>
#include <QMessageBox>
class ControllerWidget : public QWidget{
    Q_OBJECT

public:
    explicit ControllerWidget(QWidget* parent);
    ~ControllerWidget() override;
private slots:
    void onImportBtnClicked();
    // void onPlayBtnClicked();
    // void onExportBtnClicked();
    // void onVideoFilterFlipVerticalBtnClicked();
    // void onVideoFilterGrayBtnClicked();
    // void onVideoFilterInvertBtnClicked();
    // void onVideoFilterStickerBtnClicked();

private:
    QPushButton* m_btnImport{nullptr};
    QPushButton* m_btnPlay{nullptr};
    QPushButton* m_btnExport{nullptr};

    QPushButton* m_btnVideoFilterFlipVertical{nullptr};
    QPushButton* m_btnVideoFilterGray{nullptr};
    QPushButton* m_btnVideoFilterInvert{nullptr};
    QPushButton* m_btnVideoFilterSticker{nullptr};
};



#endif //CONTROLLERWIDGET_H
