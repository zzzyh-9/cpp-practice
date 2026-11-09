#pragma once

#include <QObject>
#include <QRandomGenerator>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace RescueApple {

enum class GameState { Idle, Playing, Paused, End };

struct GameSettings {
    int level = 1;
    int maxApples = 3;
    int target = 20;
    bool soundEnabled = true;
    int missLimit = 50;
    bool musicEnabled = true;
    int soundVolume = 50;
    int musicVolume = 50;
};

struct GameStats {
    int success = 0;
    int missed = 0;
    int wrong = 0;
    int failures() const { return missed + wrong; }
    double accuracy() const;
};

// 使用归一化坐标，窗口缩放、换屏不改变苹果相对于场景的位置。
struct FallingToken {
    quint64 id = 0;
    QChar symbol;
    qreal x = 0.5;
    qreal y = 0.0;
};

// Model 只依赖 Qt Core，不包含控件或绘图代码。
// 字符集可以替换，供后续其他下落/打字游戏复用。
class FallingGameModel : public QObject {
    Q_OBJECT
public:
    explicit FallingGameModel(QObject *parent = nullptr,
                              quint32 seed = QRandomGenerator::global()->generate(),
                              const QString &alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));

    const QVector<FallingToken> &tokens() const { return m_tokens; }
    const GameSettings &settings() const { return m_settings; }
    const GameStats &stats() const { return m_stats; }
    GameState state() const { return m_state; }
    int stageSuccess() const { return m_stageSuccess; }
    bool awaitingNextLevel() const { return m_awaitingNextLevel; }
    bool completed() const { return m_completed; }
    QSizeF fieldSize() const { return m_fieldSize; }
    qreal tokenDiameter() const;
    qreal speed() const;
    static constexpr qreal failureLine() { return 0.70; }

    void applySettings(GameSettings settings);
    void setFieldSize(const QSizeF &size);
    void setSpawnRanges(const QVector<QPair<qreal, qreal>> &ranges);
    void start();
    void pause();
    void resume();
    void stop();
    void nextLevel();
    void advance(double seconds);
    bool typeText(const QString &text);

signals:
    void changed();
    void feedback(bool success);

private:
    void spawnToken();
    void finishLevel();
    qreal horizontalMargin() const;

    QRandomGenerator m_random;
    QString m_alphabet;
    QSizeF m_fieldSize{1000, 600};
    GameSettings m_settings;
    GameStats m_stats;
    GameState m_state = GameState::Idle;
    QVector<FallingToken> m_tokens;
    QVector<QPair<qreal, qreal>> m_spawnRanges;
    quint64 m_nextId = 1;
    double m_spawnElapsed = 0.0;
    int m_stageSuccess = 0;
    bool m_awaitingNextLevel = false;
    bool m_completed = false;
};

} // namespace RescueApple
