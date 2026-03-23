//
// Created by Joe on 26-3-15.
//

#ifndef PLAYERWIDGET_H
#define PLAYERWIDGET_H

#include <QWidget>

class PlayerWidget final : public QWidget{
    Q_OBJECT
public:
    explicit PlayerWidget(QWidget* parent = nullptr);
    ~PlayerWidget() override;
};



#endif //PLAYERWIDGET_H
