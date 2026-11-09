#include "gameview.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSet>

namespace RescueApple {

QRectF GameView::basketRect() const
{
    const QSize field = viewport()->size();
    const qreal width = qBound(120.0, qMin(field.width() * 0.19, field.height() * 0.34), 280.0);
    const qreal height = width * 656.0 / 746.0;
    return QRectF(field.width() - width - 8, field.height() - height - 3, width, height);
}

class FallingItem : public QGraphicsItem {
public:
    explicit FallingItem(SkinManager *skins) : m_skins(skins) {}
    QRectF boundingRect() const override { return QRectF(-m_size / 2, -m_size / 2, m_size, m_size); }
    void setToken(const FallingToken &token, qreal size, const QSizeF &field, bool hideLetter)
    {
        if (!qFuzzyCompare(size, m_size)) {
            prepareGeometryChange();
            m_size = size;
        }
        m_symbol = hideLetter ? QLatin1Char('?') : token.symbol;
        setPos(token.x * field.width(), token.y * field.height());
        update();
    }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->drawPixmap(boundingRect(), m_skins->skin().apple, m_skins->skin().apple.rect());
        QFont font(QStringLiteral("Segoe UI"));
        font.setPixelSize(qRound(m_size * 0.40));
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(95, 37, 27, 100));
        const QRectF letters(-m_size / 2, -m_size * 0.29, m_size, m_size * 0.76);
        painter->drawText(letters.translated(1, 2), Qt::AlignCenter, QString(m_symbol));
        painter->setPen(m_skins->skin().letterColor);
        painter->drawText(letters, Qt::AlignCenter, QString(m_symbol));
    }
private:
    SkinManager *m_skins;
    qreal m_size = 64;
    QChar m_symbol;
};

GameView::GameView(SkinManager *skins, QWidget *parent)
    : QGraphicsView(parent), m_skins(skins), m_scene(new QGraphicsScene(this))
{
    setupScene();
    connect(skins, &SkinManager::changed, this, [this]() {
        for (auto *item : m_items)
            item->update();
        resetCachedContent();
        viewport()->update();
    });
}

void GameView::setupScene()
{
    setScene(m_scene);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setCacheMode(QGraphicsView::CacheBackground);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    m_scene->setSceneRect(QRectF(QPointF(0, 0), viewport()->size()));
}

void GameView::display(const FallingGameModel &model)
{
    const bool hasRescuedApple = model.stats().success > 0;
    if (m_hasRescuedApple != hasRescuedApple) {
        m_hasRescuedApple = hasRescuedApple;
        viewport()->update(basketRect().toAlignedRect());
    }
    QSet<quint64> active;
    for (const auto &token : model.tokens()) {
        active.insert(token.id);
        auto *item = m_items.value(token.id, nullptr);
        if (!item) {
            item = new FallingItem(m_skins);
            m_items.insert(token.id, item);
            m_scene->addItem(item);
        }
        item->setToken(token, model.tokenDiameter(), m_scene->sceneRect().size(), model.state() == GameState::Paused);
    }
    for (auto iterator = m_items.begin(); iterator != m_items.end();) {
        if (!active.contains(iterator.key())) {
            delete iterator.value(); // QGraphicsItem 析构时自动从场景移除。
            iterator = m_items.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void GameView::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space) {
        emit pauseRequested();
        event->accept();
        return;
    }
    const QString text = event->text().toUpper();
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        text.size() == 1 && text.at(0) >= QLatin1Char('A') && text.at(0) <= QLatin1Char('Z')) {
        emit textTyped(text);
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void GameView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    // 场景直接铺满当前视口，避免固定比例缩放产生黑边。
    m_scene->setSceneRect(QRectF(QPointF(0, 0), viewport()->size()));
    emit fieldResized(viewport()->size());
}

void GameView::drawBackground(QPainter *painter, const QRectF &)
{
    const QRectF field = sceneRect();
    const auto &skin = m_skins->skin();
    painter->save();
    if (!skin.background.isNull()) {
        // 等比铺满并裁剪超出部分，不拉伸，也不留黑边。
        const qreal scale = qMax(field.width() / skin.background.width(), field.height() / skin.background.height());
        const QSizeF size = skin.background.size() * scale;
        const QRectF target((field.width() - size.width()) / 2, (field.height() - size.height()) / 2,
                            size.width(), size.height());
        painter->setClipRect(field);
        painter->drawPixmap(target, skin.background, skin.background.rect());
    } else {
        QLinearGradient sky(field.topLeft(), field.bottomLeft());
        sky.setColorAt(0, skin.skyTop);
        sky.setColorAt(1, skin.skyBottom);
        painter->fillRect(field, sky);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 125));
        const qreal unit = qMin(field.width(), field.height());
        for (int index = 0; index < 4; ++index) {
            const qreal x = field.width() * (0.14 + 0.23 * index);
            const qreal y = field.height() * (0.09 + 0.035 * (index % 2));
            painter->drawEllipse(QRectF(x, y, unit * 0.16, unit * 0.045));
        }
        painter->setBrush(skin.ground.lighter(130));
        painter->drawEllipse(QRectF(-field.width() * 0.15, field.height() * 0.77,
                                    field.width() * 0.90, field.height() * 0.65));
        painter->setBrush(skin.ground);
        painter->drawEllipse(QRectF(field.width() * 0.30, field.height() * 0.83,
                                    field.width() * 0.95, field.height() * 0.60));
        if (skin.orchard) {
            for (int side : {0, 1}) {
                const qreal x = side == 0 ? field.width() * 0.045 : field.width() * 0.95;
                const qreal base = field.height() * 0.94;
                painter->setBrush(QColor(QStringLiteral("#906b4c")));
                painter->drawRoundedRect(QRectF(x - unit * 0.012, base - unit * 0.22,
                                                unit * 0.024, unit * 0.23), 4, 4);
                painter->setBrush(QColor(QStringLiteral("#6e9b67")));
                painter->drawEllipse(QRectF(x - unit * 0.11, base - unit * 0.36,
                                            unit * 0.22, unit * 0.21));
                painter->setBrush(QColor(QStringLiteral("#82af71")));
                painter->drawEllipse(QRectF(x - unit * 0.08, base - unit * 0.42,
                                            unit * 0.16, unit * 0.20));
            }
        }
    }
    const QPixmap basket = m_skins->frame(QStringLiteral("apple_basket_back"));
    if (!basket.isNull())
        painter->drawPixmap(basketRect(), basket, basket.rect());
    painter->restore();
}

void GameView::drawForeground(QPainter *painter, const QRectF &)
{
    painter->save();
    const QRectF area = basketRect();
    const QPixmap basket = m_skins->frame(QStringLiteral("apple_basket_front"));
    const QRectF rim(area.left() + area.width() * 0.10, area.top() + area.height() * 0.57,
                     area.width() * 0.80, area.height() * 0.22);
    if (basket.isNull()) {
        QPainterPath handle;
        handle.moveTo(rim.left() + rim.width() * 0.10, rim.center().y());
        handle.cubicTo(area.left() + area.width() * 0.24, area.top(),
                       area.right() - area.width() * 0.24, area.top(),
                       rim.right() - rim.width() * 0.10, rim.center().y());
        painter->setPen(QPen(QColor(QStringLiteral("#a36333")), area.width() * 0.05,
                             Qt::SolidLine, Qt::RoundCap));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(handle);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#a36333")));
        painter->drawEllipse(rim);
    }
    if (m_hasRescuedApple) {
        const qreal size = area.width() * 0.27;
        const QRectF apple(area.left() + area.width() * 0.56, rim.center().y() - size * 0.8, size, size);
        const auto &image = m_skins->skin().apple;
        painter->drawPixmap(apple, image, image.rect());
    }
    if (!basket.isNull()) {
        painter->drawPixmap(area, basket, basket.rect());
    } else {
        QPainterPath body;
        body.moveTo(rim.left(), rim.center().y());
        body.quadTo(rim.center().x(), rim.bottom() + area.height() * 0.08, rim.right(), rim.center().y());
        body.quadTo(rim.right() - rim.width() * 0.09, area.bottom() - 6, rim.center().x(), area.bottom() - 6);
        body.quadTo(rim.left() + rim.width() * 0.09, area.bottom() - 6, rim.left(), rim.center().y());
        painter->setPen(QPen(QColor(QStringLiteral("#a36333")), 2));
        painter->setBrush(QColor(QStringLiteral("#d89955")));
        painter->drawPath(body);
    }
    painter->restore();
}

} // namespace RescueApple
