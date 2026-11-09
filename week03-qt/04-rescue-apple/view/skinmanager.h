#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QObject>
#include <QPixmap>

namespace RescueApple {

struct GameSkin {
    QString name = QStringLiteral("夏日果园");
    QColor skyTop{QStringLiteral("#ccece6")};
    QColor skyBottom{QStringLiteral("#f5f7dc")};
    QColor ground{QStringLiteral("#73985a")};
    QColor appleColor{QStringLiteral("#e95345")};
    QColor letterColor{Qt::white};
    QPixmap apple;
    QPixmap background;
    QString hitSoundPath;
    QString backgroundSoundPath;
    QHash<QString, QPixmap> images;
    QHash<QString, int> frameCounts;
    bool orchard = true;
};

// 皮肤只提供数据和图片，不执行外部代码。
class SkinManager : public QObject {
    Q_OBJECT
public:
    explicit SkinManager(QObject *parent = nullptr);
    const GameSkin &skin() const { return m_skin; }
    QPixmap frame(const QString &key, int state = 0) const;
    void useBuiltin(bool orchard = true);
    bool loadDirectory(const QString &directory, QString *error);

signals:
    void changed();

private:
    bool loadResourcePack(const QString &directory, QString *error);
    QPixmap setupApple(const QColor &color) const;
    GameSkin m_skin;
};

} // namespace RescueApple
