#pragma once

#include <QGraphicsView>
#include <QHash>

#include "model/fallinggamemodel.h"
#include "skinmanager.h"

namespace RescueApple {

class FallingItem;

class GameView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GameView(SkinManager *skins, QWidget *parent = nullptr);
    void display(const FallingGameModel &model);
    QRectF basketRect() const;

signals:
    void textTyped(const QString &text);
    void pauseRequested();
    void fieldResized(const QSizeF &size);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    void setupScene();
    SkinManager *m_skins;
    QGraphicsScene *m_scene;
    QHash<quint64, FallingItem *> m_items;
    bool m_hasRescuedApple = false;
};

} // namespace RescueApple
