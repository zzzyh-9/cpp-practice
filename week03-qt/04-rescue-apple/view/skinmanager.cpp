#include "skinmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

// 显式初始化静态库中的资源，避免链接器丢弃资源对象文件。
static void initializeAppleResources()
{
    Q_INIT_RESOURCE(apple_resources);
}

namespace RescueApple {

SkinManager::SkinManager(QObject *parent) : QObject(parent)
{
    initializeAppleResources();
    useBuiltin();
}

QPixmap SkinManager::frame(const QString &key, int state) const
{
    const QPixmap image = m_skin.images.value(key);
    const int count = qMax(1, m_skin.frameCounts.value(key, 1));
    if (image.isNull() || count == 1)
        return image;
    const int width = image.width() / count;
    return image.copy(qBound(0, state, count - 1) * width, 0, width, image.height());
}

bool SkinManager::loadResourcePack(const QString &directory, QString *error)
{
    const QDir base(directory);
    QFile manifest(base.filePath(QStringLiteral("config/resources.json")));
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!manifest.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("找不到资源包中的 config/resources.json。"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("资源配置不是有效的 JSON 对象。"));
    const QJsonObject resources = document.object().value(QStringLiteral("resources")).toObject();
    GameSkin skin;
    skin.name = QStringLiteral("拯救苹果 2026");
    // 原资源配置中的部分路径是小写，实际文件名为大写。
    const auto resolve = [&base](const QString &relative) {
        QString direct = base.filePath(relative);
        if (QFile::exists(direct))
            return direct;
        direct = base.filePath(relative.toLower()); // 内嵌资源使用小写别名。
        if (QFile::exists(direct))
            return direct;
        QDir parent(base.filePath(QFileInfo(relative).path()));
        for (const QString &name : parent.entryList(QDir::Files)) {
            if (name.compare(QFileInfo(relative).fileName(), Qt::CaseInsensitive) == 0)
                return parent.filePath(name);
        }
        return QString();
    };
    for (const QJsonValue &value : resources.value(QStringLiteral("images")).toArray()) {
        const QJsonObject item = value.toObject();
        const QString key = item.value(QStringLiteral("key")).toString();
        const QString path = resolve(item.value(QStringLiteral("path")).toString());
        QPixmap image(path);
        if (image.isNull())
            return fail(QStringLiteral("无法读取资源图片：%1").arg(key));
        const int frames = item.value(QStringLiteral("frames")).toInt(1);
        if (frames < 1 || image.width() % frames != 0)
            return fail(QStringLiteral("资源图片的帧数无效：%1").arg(key));
        skin.images.insert(key, image);
        skin.frameCounts.insert(key, frames);
    }
    skin.apple = skin.images.value(QStringLiteral("apple_normal"));
    skin.background = skin.images.value(QStringLiteral("apple_bg_max"));
    if (skin.apple.isNull() || skin.background.isNull())
        return fail(QStringLiteral("资源包缺少苹果或背景。"));
    for (const QJsonValue &value : resources.value(QStringLiteral("sounds")).toArray()) {
        const QJsonObject item = value.toObject();
        const QString key = item.value(QStringLiteral("key")).toString();
        const QString path = resolve(item.value(QStringLiteral("path")).toString());
        if (key == QStringLiteral("APPLE_IN"))
            skin.hitSoundPath = path;
        else if (key == QStringLiteral("APPLE_BG"))
            skin.backgroundSoundPath = path;
    }
    m_skin = skin;
    emit changed();
    return true;
}

QPixmap SkinManager::setupApple(const QColor &color) const
{
    QPixmap apple(128, 128);
    apple.fill(Qt::transparent);
    QPainter painter(&apple);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#704936")), 7, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(64, 30), QPointF(70, 12));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#4c8c4a")));
    painter.drawEllipse(QRectF(74, 9, 30, 15));
    QRadialGradient gradient(QPointF(45, 45), 85);
    gradient.setColorAt(0, color.lighter(125));
    gradient.setColorAt(1, color.darker(115));
    painter.setBrush(gradient);
    painter.drawEllipse(QRectF(16, 28, 96, 92));
    painter.setBrush(QColor(255, 255, 255, 65));
    painter.drawEllipse(QRectF(29, 40, 13, 22));
    return apple;
}

void SkinManager::useBuiltin(bool orchard)
{
    GameSkin skin;
    skin.orchard = orchard;
    if (!orchard) {
        skin.name = QStringLiteral("课堂简洁");
        skin.skyTop = QColor(QStringLiteral("#e4eff8"));
        skin.skyBottom = QColor(QStringLiteral("#f8fbff"));
        skin.ground = QColor(QStringLiteral("#adc6ba"));
    }
    skin.apple = setupApple(skin.appleColor);
    skin.hitSoundPath = QStringLiteral(":/builtin-audio/hit.wav");
    skin.backgroundSoundPath = QStringLiteral(":/builtin-audio/music.wav");
    m_skin = skin;
    emit changed();
}

bool SkinManager::loadDirectory(const QString &directory, QString *error)
{
    const QDir base(directory);
    if (QFile::exists(base.filePath(QStringLiteral("config/resources.json"))))
        return loadResourcePack(directory, error);
    QFile manifest(base.filePath(QStringLiteral("skin.json")));
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!manifest.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("所选文件夹中找不到可读取的 skin.json。"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("skin.json 不是有效的 JSON 对象。"));
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt(1) != 1)
        return fail(QStringLiteral("此皮肤的版本暂不支持，请使用 version: 1。"));

    GameSkin skin;
    skin.name = object.value(QStringLiteral("name")).toString(base.dirName());
    skin.orchard = object.value(QStringLiteral("orchard")).toBool(false);
    const auto readColor = [&object](const char *key, QColor &color) {
        const QString value = object.value(QLatin1String(key)).toString();
        if (value.isEmpty())
            return true;
        const QColor parsed(value);
        if (!parsed.isValid())
            return false;
        color = parsed;
        return true;
    };
    if (!readColor("skyTop", skin.skyTop) || !readColor("skyBottom", skin.skyBottom) ||
        !readColor("ground", skin.ground) || !readColor("appleColor", skin.appleColor) ||
        !readColor("letterColor", skin.letterColor))
        return fail(QStringLiteral("皮肤中存在无效的颜色值。"));

    skin.apple = setupApple(skin.appleColor);
    for (const QString &key : {QStringLiteral("apple"), QStringLiteral("background")}) {
        const QString path = object.value(key).toString();
        if (path.isEmpty())
            continue;
        QPixmap image(base.filePath(path));
        if (image.isNull())
            return fail(QStringLiteral("无法读取皮肤图片：%1").arg(path));
        if (key == QStringLiteral("apple"))
            skin.apple = image;
        else
            skin.background = image;
    }
    for (const QString &key : {QStringLiteral("hitSound"), QStringLiteral("backgroundSound")}) {
        const QString path = object.value(key).toString();
        if (path.isEmpty())
            continue;
        QFile audio(base.filePath(path));
        if (!audio.open(QIODevice::ReadOnly) || audio.size() > 10 * 1024 * 1024)
            return fail(QStringLiteral("无法读取 WAV 音效：%1").arg(path));
        const QByteArray data = audio.readAll();
        if (!data.startsWith("RIFF") || data.mid(8, 4) != "WAVE")
            return fail(QStringLiteral("音效需要使用 WAV 格式：%1").arg(path));
        if (key == QStringLiteral("hitSound"))
            skin.hitSoundPath = base.filePath(path);
        else
            skin.backgroundSoundPath = base.filePath(path);
    }
    m_skin = skin; // 全部验证成功后再替换，失败时保留原皮肤。
    emit changed();
    return true;
}

} // namespace RescueApple
