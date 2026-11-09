#include <QtTest>

#include <QSet>
#include <limits>

#include "model/fallinggamemodel.h"

using namespace RescueApple;

static void advanceFor(FallingGameModel &model, double seconds)
{
    while (seconds > 0.000001) {
        const double step = qMin(0.05, seconds);
        model.advance(step);
        seconds -= step;
    }
}

static QChar visibleLetter(FallingGameModel &model)
{
    for (int step = 0; step < 200; ++step) {
        for (const auto &token : model.tokens()) {
            if (token.y >= 0)
                return token.symbol;
        }
        model.advance(0.05);
    }
    return {};
}

class ModelTests : public QObject {
    Q_OBJECT
private slots:
    void initialAndSettings()
    {
        FallingGameModel model(nullptr, 1);
        QCOMPARE(model.state(), GameState::Idle);
        QCOMPARE(model.stats().accuracy(), 100.0);
        QVERIFY(model.tokens().isEmpty());
        model.applySettings({99, 99, 0, false});
        QCOMPARE(model.settings().level, 10);
        QCOMPARE(model.settings().maxApples, 5);
        QCOMPARE(model.settings().target, 1);
        QVERIFY(!model.settings().soundEnabled);
    }

    void uniqueLettersBoundsAndMisses()
    {
        FallingGameModel model(nullptr, 42);
        model.setFieldSize(QSizeF(800, 600));
        model.applySettings({10, 5, 20, false});
        model.start();
        QVERIFY(model.tokens().first().y < 0);
        for (int step = 0; step < 1200; ++step) {
            model.advance(0.05);
            QVERIFY(model.tokens().size() <= 5);
            QSet<QChar> symbols;
            for (const auto &token : model.tokens()) {
                QVERIFY(!symbols.contains(token.symbol));
                symbols.insert(token.symbol);
                const qreal halfWidth = model.tokenDiameter() / 2 / model.fieldSize().width();
                QVERIFY(token.x >= halfWidth && token.x <= 1.0 - halfWidth);
                QVERIFY(token.y < FallingGameModel::failureLine());
            }
        }
        QVERIFY(model.stats().missed > 0);
        QCOMPARE(model.stats().wrong, 0);
    }

    void letterMatchingAndAccuracy()
    {
        FallingGameModel model(nullptr, 10);
        model.applySettings({1, 1, 20, false});
        model.start();
        const QChar letter = visibleLetter(model);
        QVERIFY(!letter.isNull());
        QVERIFY(model.typeText(QString(letter).toLower()));
        QCOMPARE(model.stats().success, 1);
        QCOMPARE(model.stats().accuracy(), 100.0);
        QVERIFY(!model.typeText(QString(letter)));
        QCOMPARE(model.stats().wrong, 1);
        QCOMPARE(model.stats().accuracy(), 50.0);
        model.typeText(QStringLiteral("1"));
        model.typeText(QStringLiteral(" "));
        model.typeText(QStringLiteral("中文"));
        QCOMPARE(model.stats().wrong, 1);
    }

    void offscreenApplesCannotBeHit()
    {
        FallingGameModel model(nullptr, 2);
        model.start();
        QVERIFY(!model.typeText(QString(model.tokens().first().symbol)));
        QCOMPARE(model.stats().success, 0);
        QCOMPARE(model.tokens().size(), 1);
    }

    void pauseResumeStopAndRestart()
    {
        FallingGameModel model(nullptr, 20);
        model.start();
        advanceFor(model, 2.0);
        model.pause();
        const auto position = model.tokens().first().y;
        const auto count = model.tokens().size();
        model.advance(100.0);
        model.typeText(QStringLiteral("A"));
        QCOMPARE(model.tokens().first().y, position);
        QCOMPARE(model.tokens().size(), count);
        QCOMPARE(model.stats().failures(), 0);
        model.resume();
        model.advance(0.1);
        QVERIFY(model.tokens().first().y > position);
        model.stop();
        QCOMPARE(model.state(), GameState::End);
        QVERIFY(model.tokens().isEmpty());
        model.advance(10);
        QVERIFY(model.tokens().isEmpty());
        model.start();
        QCOMPARE(model.state(), GameState::Playing);
        QCOMPARE(model.stats().success, 0);
        QCOMPARE(model.stats().failures(), 0);
    }

    void progressionThroughTenLevels()
    {
        FallingGameModel model(nullptr, 3);
        model.applySettings({1, 1, 1, false});
        model.start();
        for (int level = 1; level <= 10; ++level) {
            QCOMPARE(model.settings().level, level);
            const QChar letter = visibleLetter(model);
            QVERIFY(!letter.isNull());
            QVERIFY(model.typeText(QString(letter)));
            QCOMPARE(model.stats().success, level);
            if (level < 10) {
                QCOMPARE(model.state(), GameState::Paused);
                QVERIFY(model.awaitingNextLevel());
                model.resume();
                QCOMPARE(model.state(), GameState::Paused);
                model.nextLevel();
                QCOMPARE(model.state(), GameState::Playing);
                QCOMPARE(model.stageSuccess(), 0);
            }
        }
        QCOMPARE(model.state(), GameState::End);
        QVERIFY(model.completed());
        QVERIFY(!model.awaitingNextLevel());
    }

    void liveSettings()
    {
        FallingGameModel model(nullptr, 4);
        model.applySettings({1, 5, 20, false});
        model.start();
        advanceFor(model, 5.0);
        QVERIFY(model.tokens().size() > 1);
        const double slow = model.speed();
        const int failures = model.stats().failures();
        model.applySettings({10, 1, 20, false});
        QCOMPARE(model.tokens().size(), 1);
        QCOMPARE(model.stats().failures(), failures);
        QVERIFY(model.speed() > slow);
    }

    void smallerTargetAppliesWhilePaused()
    {
        FallingGameModel model(nullptr, 5);
        model.start();
        QVERIFY(model.typeText(QString(visibleLetter(model))));
        model.pause();
        auto settings = model.settings();
        settings.target = 1;
        model.applySettings(settings);
        QVERIFY(model.awaitingNextLevel());
    }

    void resizePreservesVerticalProgress()
    {
        FallingGameModel model(nullptr, 6);
        model.start();
        advanceFor(model, 2);
        const double y = model.tokens().first().y;
        for (const QSizeF size : {QSizeF(800, 600), QSizeF(1920, 1080), QSizeF(640, 480)}) {
            model.setFieldSize(size);
            QCOMPARE(model.tokens().first().y, y);
            for (const auto &token : model.tokens()) {
                const double edge = model.tokenDiameter() / 2 / size.width();
                QVERIFY(token.x >= edge && token.x <= 1 - edge);
            }
        }
    }

    void customAlphabetAndTimeIndependence()
    {
        FallingGameModel limited(nullptr, 7, QStringLiteral("AaBBc"));
        limited.applySettings({1, 5, 20, false});
        QCOMPARE(limited.settings().maxApples, 3);
        limited.start();
        advanceFor(limited, 4);
        for (const auto &token : limited.tokens())
            QVERIFY(QStringLiteral("ABC").contains(token.symbol));

        FallingGameModel first(nullptr, 8), second(nullptr, 8);
        first.start();
        second.start();
        advanceFor(first, 0.5);
        second.advance(0.5);
        QVERIFY(qAbs(first.tokens().first().y - second.tokens().first().y) < 1e-9);
        const double y = first.tokens().first().y;
        first.advance(std::numeric_limits<double>::quiet_NaN());
        first.advance(-1);
        QCOMPARE(first.tokens().first().y, y);
    }
};

QTEST_GUILESS_MAIN(ModelTests)
#include "model_test.moc"
