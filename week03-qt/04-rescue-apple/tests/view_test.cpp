#include <QtTest>

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "controller/gamecontroller.h"

using namespace RescueApple;

class ViewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        // offscreen 插件不自动读取 Windows 字体，截图测试需显式加载。
        const QDir fonts(qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) + QStringLiteral("/Fonts"));
        for (const QString &name : {QStringLiteral("msyh.ttc"), QStringLiteral("msyhbd.ttc"),
                                    QStringLiteral("segoeui.ttf"), QStringLiteral("segoeuib.ttf")})
            if (fonts.exists(name))
                QFontDatabase::addApplicationFont(fonts.filePath(name));
        QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei"), 10));
#endif
    }

    void builtinSkinAndSounds()
    {
        SkinManager skins;
        QVERIFY(skins.skin().orchard);
        QCOMPARE(skins.skin().apple.size(), QSize(128, 128));
        QVERIFY(QFile::exists(skins.skin().hitSoundPath));
        QVERIFY(QFile::exists(skins.skin().backgroundSoundPath));
    }

    void skinSwitchAndFailureRollback()
    {
        SkinManager skins;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile manifest(directory.filePath(QStringLiteral("skin.json")));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        QJsonObject configuration;
        configuration.insert(QStringLiteral("version"), 1);
        configuration.insert(QStringLiteral("name"), QStringLiteral("Test skin"));
        configuration.insert(QStringLiteral("appleColor"), QStringLiteral("#00a050"));
        configuration.insert(QStringLiteral("skyTop"), QStringLiteral("#abcdef"));
        manifest.write(QJsonDocument(configuration).toJson());
        manifest.close();
        QString error;
        QVERIFY(skins.loadDirectory(directory.path(), &error));
        QCOMPARE(skins.skin().name, QStringLiteral("Test skin"));
        QVERIFY(!skins.skin().apple.isNull());
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        configuration.insert(QStringLiteral("apple"), QStringLiteral("missing.png"));
        manifest.write(QJsonDocument(configuration).toJson());
        manifest.close();
        QVERIFY(!skins.loadDirectory(directory.path(), &error));
        QCOMPARE(skins.skin().name, QStringLiteral("Test skin"));
        skins.useBuiltin();
        QVERIFY(skins.skin().orchard);
        QVERIFY(!skins.skin().apple.isNull());
    }

    void keyboardConnectionAndPause()
    {
        SkinManager skins;
        FallingGameModel model(nullptr, 11);
        model.applySettings({1, 1, 20, false});
        GameWindow window(&skins);
        GameController controller(&model, &window, &skins, false);
        window.show();
        QCoreApplication::processEvents();
        auto *primary = window.findChild<QAbstractButton *>(QStringLiteral("primaryAction"));
        QVERIFY(primary);
        QTest::mouseClick(primary, Qt::LeftButton);
        QCOMPARE(model.state(), GameState::Playing);
        for (int step = 0; step < 100 && model.tokens().first().y < 0; ++step)
            model.advance(0.05);
        const QChar letter = model.tokens().first().symbol;
        QTest::keyClick(window.gameView(), static_cast<Qt::Key>(letter.unicode()));
        QCOMPARE(model.stats().success, 1);
        QTest::keyClick(window.gameView(), Qt::Key_Space);
        QCOMPARE(model.state(), GameState::Paused);
        QTest::qWait(35);
        QTest::keyClick(window.gameView(), Qt::Key_Space);
        QCOMPARE(model.state(), GameState::Playing);
        model.stop();
    }

    void responsiveRendering_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("800x600") << QSize(800, 600);
        QTest::newRow("1920x1080") << QSize(1920, 1080);
    }

    void responsiveRendering()
    {
        QFETCH(QSize, size);
        SkinManager skins;
        FallingGameModel model(nullptr, 12);
        model.applySettings({1, 3, 20, false});
        GameWindow window(&skins);
        GameController controller(&model, &window, &skins, false);
        window.show();
        window.resize(size);
        QCoreApplication::processEvents();
        QCOMPARE(window.size(), size);
        QCOMPARE(window.gameView()->sceneRect().size(), QSizeF(window.gameView()->viewport()->size()));
        QCOMPARE(model.fieldSize(), window.gameView()->sceneRect().size());
        auto *footer = window.findChild<QWidget *>(QStringLiteral("footer"));
        QVERIFY(footer);
        QVERIFY(window.gameView()->viewport()->rect().contains(footer->geometry()));

        const QString directory = QCoreApplication::applicationDirPath() + QStringLiteral("/screenshots");
        QVERIFY(QDir().mkpath(directory));
        const QString suffix = QStringLiteral("%1x%2.png").arg(size.width()).arg(size.height());
        QVERIFY(window.grab().save(directory + QStringLiteral("/idle-") + suffix));
        model.start();
        for (int step = 0; step < 80; ++step)
            model.advance(0.05);
        QVERIFY(window.grab().save(directory + QStringLiteral("/playing-") + suffix));
        model.pause();
        QVERIFY(window.grab().save(directory + QStringLiteral("/paused-") + suffix));
        model.stop();
    }
};

QTEST_MAIN(ViewTests)
#include "view_test.moc"
