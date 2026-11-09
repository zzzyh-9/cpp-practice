#include <QtTest>

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSlider>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>

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
        auto *rescued = window.findChild<QLabel *>(QStringLiteral("rescuedCount"));
        auto *missed = window.findChild<QLabel *>(QStringLiteral("missedCount"));
        QVERIFY(rescued);
        QVERIFY(missed);
        QTest::mouseClick(primary, Qt::LeftButton);
        QCOMPARE(model.state(), GameState::Playing);
        QCOMPARE(rescued->text(), QStringLiteral("救下 0/20"));
        QCOMPARE(missed->text(), QStringLiteral("漏接 0/50"));
        for (int step = 0; step < 100 && model.tokens().first().y < 0; ++step)
            model.advance(0.05);
        const QChar letter = model.tokens().first().symbol;
        QTest::keyClick(window.gameView(), static_cast<Qt::Key>(letter.unicode()));
        QCOMPARE(model.stats().success, 1);
        QCOMPARE(rescued->text(), QStringLiteral("救下 1/20"));
        QTest::keyClick(window.gameView(), static_cast<Qt::Key>(letter.unicode()));
        QCOMPARE(model.stats().wrong, 1);
        QCOMPARE(missed->text(), QStringLiteral("漏接 0/50"));
        model.advance(1.0); // 生成下一颗苹果。
        model.advance(20.0); // 让它越过漏接高度。
        QCOMPARE(missed->text(), QStringLiteral("漏接 1/50"));
        QTest::keyClick(window.gameView(), Qt::Key_Space);
        QCOMPARE(model.state(), GameState::Paused);
        QTest::qWait(35);
        QTest::keyClick(window.gameView(), Qt::Key_Space);
        QCOMPARE(model.state(), GameState::Playing);
        auto settings = model.settings();
        settings.missLimit = 2;
        model.applySettings(settings);
        QCOMPARE(missed->text(), QStringLiteral("漏接 1/2"));
        model.advance(20.0);
        QCOMPARE(model.state(), GameState::End);
        QCOMPARE(missed->text(), QStringLiteral("漏接 2/2"));
    }

    void fullscreenTitleBarWhilePaused()
    {
        SkinManager skins;
        FallingGameModel model(nullptr, 17);
        GameSettings settings;
        settings.soundEnabled = false;
        settings.musicEnabled = false;
        model.applySettings(settings);
        GameWindow window(&skins);
        GameController controller(&model, &window, &skins, false);
        window.showFullScreen();
        QCoreApplication::processEvents();

        auto *titleBar = window.findChild<QWidget *>(QStringLiteral("fullscreenTitleBar"));
        QVERIFY(titleBar);
        QVERIFY(titleBar->isHidden());
        model.start();
        QVERIFY(titleBar->isHidden());
        model.pause();
        QVERIFY(titleBar->isVisible());
        QCOMPARE(titleBar->width(), window.gameView()->viewport()->width());
        model.resume();
        QVERIFY(titleBar->isHidden());
        model.pause();

        auto *restore = titleBar->findChild<QAbstractButton *>(QStringLiteral("fullscreenRestore"));
        QVERIFY(restore);
        QTest::mouseClick(restore, Qt::LeftButton);
        QVERIFY(!window.isFullScreen());
        QVERIFY(titleBar->isHidden());
        QCOMPARE(model.state(), GameState::Paused);

        window.showFullScreen();
        QCoreApplication::processEvents();
        auto *close = titleBar->findChild<QAbstractButton *>(QStringLiteral("fullscreenClose"));
        QVERIFY(close && close->isVisible());
        const bool quitOnClose = QApplication::quitOnLastWindowClosed();
        QApplication::setQuitOnLastWindowClosed(false);
        QTest::mouseClick(close, Qt::LeftButton);
        QApplication::setQuitOnLastWindowClosed(quitOnClose);
        QVERIFY(!window.isVisible());
    }

    void settingsTabsAndApply_data()
    {
        QTest::addColumn<QSize>("windowSize");
        QTest::addColumn<int>("minimumDialogWidth");
        QTest::addColumn<int>("minimumFontSize");
        QTest::newRow("small window") << QSize(800, 600) << 468 << 15;
        QTest::newRow("full-size window") << QSize(2560, 1500) << 1000 << 30;
    }

    void settingsTabsAndApply()
    {
        QFETCH(QSize, windowSize);
        QFETCH(int, minimumDialogWidth);
        QFETCH(int, minimumFontSize);
        SkinManager skins;
        GameWindow window(&skins);
        window.resize(windowSize);
        window.show();
        QCoreApplication::processEvents();
        GameSettings result;
        QString error;
        QTimer::singleShot(0, &window, [&]() {
            auto *dialog = window.findChild<QDialog *>();
            if (!dialog)
                qFatal("Settings dialog did not open");
            auto *tabs = dialog->findChild<QStackedWidget *>(QStringLiteral("settingsPages"));
            auto *audioTab = dialog->findChild<QAbstractButton *>(QStringLiteral("audioTab"));
            auto *confirm = dialog->findChild<QAbstractButton *>(QStringLiteral("confirmSettings"));
            auto *limit = dialog->findChild<QSlider *>(QStringLiteral("missLimit"));
            auto *volume = dialog->findChild<QSlider *>(QStringLiteral("musicVolume"));
            auto *hit = dialog->findChild<QCheckBox *>(QStringLiteral("hitAudio"));
            if (!tabs || !audioTab || !confirm || !limit || !volume || !hit) {
                error = QStringLiteral("A settings control is missing");
                dialog->reject();
                return;
            }
            const auto *limitText = limit->parentWidget()->findChild<QLabel *>();
            const int fontSize = limitText ? limitText->font().pixelSize() : -1;
            if (dialog->width() < minimumDialogWidth || dialog->width() >= window.width() ||
                dialog->height() >= window.height() || fontSize < minimumFontSize) {
                error = QStringLiteral("Settings %1x%2 with %3 px text in window %4x%5")
                            .arg(dialog->width()).arg(dialog->height()).arg(fontSize)
                            .arg(window.width()).arg(window.height());
                dialog->reject();
                return;
            }
            const QString directory = QCoreApplication::applicationDirPath() + QStringLiteral("/screenshots");
            const QString suffix = QStringLiteral("-%1.png").arg(windowSize.width());
            QDir().mkpath(directory);
            dialog->grab().save(directory + QStringLiteral("/settings-gameplay") + suffix);
            limit->setValue(12);
            QTest::mouseClick(audioTab, Qt::LeftButton);
            if (tabs->currentIndex() != 1) {
                error = QStringLiteral("Audio tab did not open");
                dialog->reject();
                return;
            }
            dialog->grab().save(directory + QStringLiteral("/settings-audio") + suffix);
            volume->setValue(25);
            hit->setChecked(false);
            QTest::mouseClick(confirm, Qt::LeftButton);
        });
        const bool accepted = window.editSettings(GameSettings{}, &result);
        QVERIFY2(accepted, qPrintable(error));
        QCOMPARE(result.missLimit, 12);
        QCOMPARE(result.musicVolume, 25);
        QVERIFY(result.musicEnabled);
        QVERIFY(!result.soundEnabled);
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
        const QRectF basket = window.gameView()->basketRect();
        QVERIFY(window.gameView()->sceneRect().contains(basket));
        QVERIFY(basket.left() > size.width() / 2.0);
        const auto *rescued = window.findChild<QLabel *>(QStringLiteral("rescuedCount"));
        const auto *missed = window.findChild<QLabel *>(QStringLiteral("missedCount"));
        QVERIFY(rescued && missed);
        const QRect rescuedPlate = rescued->parentWidget()->geometry();
        const QRect missedPlate = missed->parentWidget()->geometry();
        QVERIFY(rescuedPlate.left() > size.width() / 2);
        QVERIFY(rescuedPlate.bottom() < missedPlate.top());
        QVERIFY(missedPlate.bottom() < basket.center().y());

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
