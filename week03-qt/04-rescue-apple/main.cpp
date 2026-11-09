#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include "controller/gamecontroller.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RescueApple2026"));
    app.setOrganizationName(QStringLiteral("CppCourseworkPublic"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("拯救苹果 2026 · 练习4"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("fullscreen"), QStringLiteral("在选定显示器上全屏运行")});
    parser.addOption({QStringLiteral("screen"), QStringLiteral("选择显示器，序号从 1 开始"), QStringLiteral("number"), QStringLiteral("1")});
    parser.addOption({QStringLiteral("mute"), QStringLiteral("本次启动关闭音效")});
    parser.process(app);

    RescueApple::SkinManager skins;
    RescueApple::FallingGameModel model;
    RescueApple::GameWindow window(&skins);
    RescueApple::GameController controller(&model, &window, &skins);
    if (parser.isSet(QStringLiteral("mute"))) {
        auto settings = model.settings();
        settings.soundEnabled = false;
        settings.musicEnabled = false;
        model.applySettings(settings);
    }
    const auto screens = QGuiApplication::screens();
    bool valid = false;
    const int index = parser.value(QStringLiteral("screen")).toInt(&valid) - 1;
    if (valid && index >= 0 && index < screens.size()) {
        (void)window.winId();
        window.windowHandle()->setScreen(screens.at(index));
        const QRect geometry = screens.at(index)->availableGeometry();
        window.resize(qMin(1100, geometry.width()), qMin(760, geometry.height()));
        window.move(geometry.center() - QPoint(window.width() / 2, window.height() / 2));
    }
    if (parser.isSet(QStringLiteral("fullscreen")))
        window.showFullScreen();
    else
        window.show();
    window.gameView()->setFocus();
    return app.exec();
}
