#include "fallinggamemodel.h"

#include <algorithm>
#include <cmath>

namespace RescueApple {

double GameStats::accuracy() const
{
    const int total = success + failures();
    return total == 0 ? 100.0 : 100.0 * success / total;
}

FallingGameModel::FallingGameModel(QObject *parent, quint32 seed, const QString &alphabet)
    : QObject(parent), m_random(seed)
{
    for (QChar character : alphabet.toUpper()) {
        if (character >= QLatin1Char('A') && character <= QLatin1Char('Z') &&
            !m_alphabet.contains(character)) {
            m_alphabet.append(character);
        }
    }
    if (m_alphabet.isEmpty())
        m_alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

qreal FallingGameModel::tokenDiameter() const
{
    return qBound(48.0, m_fieldSize.height() * 0.13, 86.0);
}

qreal FallingGameModel::horizontalMargin() const
{
    return qMin(0.45, (tokenDiameter() / 2.0 + 8.0) / m_fieldSize.width());
}

qreal FallingGameModel::speed() const
{
    // 每秒移动的场景高度比例，等级越高速度越快。
    return 0.065 + 0.018 * (m_settings.level - 1);
}

void FallingGameModel::applySettings(GameSettings settings)
{
    settings.level = qBound(1, settings.level, 10);
    settings.maxApples = qBound(1, settings.maxApples, qMin(5, m_alphabet.size()));
    settings.target = qBound(1, settings.target, 999);
    settings.missLimit = qBound(1, settings.missLimit, 50);
    settings.soundVolume = qBound(0, settings.soundVolume, 100);
    settings.musicVolume = qBound(0, settings.musicVolume, 100);
    const bool levelChanged = settings.level != m_settings.level;
    m_settings = settings;
    if (levelChanged) {
        m_stageSuccess = 0;
        m_awaitingNextLevel = false;
        m_completed = false;
    }
    while (m_tokens.size() > m_settings.maxApples)
        m_tokens.removeLast(); // 减少上限时移除多余苹果，不计漏接。
    if (m_state == GameState::Playing || m_state == GameState::Paused) {
        if (m_stats.missed >= m_settings.missLimit) {
            stop();
            return;
        }
        if (!m_awaitingNextLevel && m_stageSuccess >= m_settings.target)
            finishLevel();
    }
    emit changed();
}

void FallingGameModel::setFieldSize(const QSizeF &size)
{
    if (size.width() <= 0 || size.height() <= 0 || size == m_fieldSize)
        return;
    m_fieldSize = size;
    const qreal margin = horizontalMargin();
    for (auto &token : m_tokens)
        token.x = qBound(margin, token.x, 1.0 - margin);
    emit changed();
}

void FallingGameModel::setSpawnRanges(const QVector<QPair<qreal, qreal>> &ranges)
{
    m_spawnRanges.clear();
    for (const auto &range : ranges) {
        if (range.first >= 0 && range.second <= 1 && range.first < range.second)
            m_spawnRanges.append(range);
    }
}

void FallingGameModel::start()
{
    m_stats = {};
    m_stageSuccess = 0;
    m_spawnElapsed = 0;
    m_awaitingNextLevel = false;
    m_completed = false;
    m_tokens.clear();
    m_state = GameState::Playing;
    spawnToken();
    emit changed();
}

void FallingGameModel::pause()
{
    if (m_state != GameState::Playing)
        return;
    m_state = GameState::Paused;
    emit changed();
}

void FallingGameModel::resume()
{
    if (m_state != GameState::Paused || m_awaitingNextLevel)
        return;
    m_state = GameState::Playing;
    emit changed();
}

void FallingGameModel::stop()
{
    if (m_state != GameState::Playing && m_state != GameState::Paused)
        return;
    m_state = GameState::End;
    m_awaitingNextLevel = false;
    m_tokens.clear();
    emit changed();
}

void FallingGameModel::finishLevel()
{
    m_tokens.clear();
    if (m_settings.level == 10) {
        m_state = GameState::End;
        m_completed = true;
        m_awaitingNextLevel = false;
    } else {
        m_state = GameState::Paused;
        m_awaitingNextLevel = true;
    }
}

void FallingGameModel::nextLevel()
{
    if (!m_awaitingNextLevel || m_state != GameState::Paused)
        return;
    ++m_settings.level;
    m_stageSuccess = 0;
    m_spawnElapsed = 0;
    m_awaitingNextLevel = false;
    m_state = GameState::Playing;
    spawnToken();
    emit changed();
}

void FallingGameModel::spawnToken()
{
    if (m_tokens.size() >= m_settings.maxApples)
        return;
    QString candidates = m_alphabet;
    for (const auto &token : m_tokens)
        candidates.remove(token.symbol);
    if (candidates.isEmpty())
        return;

    FallingToken token;
    token.id = m_nextId++;
    token.symbol = candidates.at(m_random.bounded(candidates.size()));
    const qreal margin = horizontalMargin();
    // 从多组随机位置中选择与现有苹果最不拥挤的一组，减少字母遮挡。
    qreal bestDistance = -1;
    for (int attempt = 0; attempt < 12; ++attempt) {
        qreal minimum = margin;
        qreal maximum = 1.0 - margin;
        if (!m_spawnRanges.isEmpty()) {
            const auto range = m_spawnRanges.at(m_random.bounded(m_spawnRanges.size()));
            minimum = qMax(minimum, range.first);
            maximum = qMin(maximum, range.second);
        }
        const qreal x = minimum + m_random.generateDouble() * qMax(0.0, maximum - minimum);
        qreal nearest = 1.0;
        for (const auto &other : m_tokens)
            nearest = qMin(nearest, qAbs(other.x - x));
        if (nearest > bestDistance) {
            bestDistance = nearest;
            token.x = x;
        }
    }
    // 中心在场景外，整个苹果从上方进入。
    token.y = -(tokenDiameter() / 2.0 + 2.0) / m_fieldSize.height();
    m_tokens.append(token);
}

void FallingGameModel::advance(double seconds)
{
    if (m_state != GameState::Playing || seconds <= 0 || !std::isfinite(seconds))
        return;
    for (auto &token : m_tokens)
        token.y += speed() * seconds;
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (m_tokens.at(i).y >= failureLine()) {
            m_tokens.removeAt(i);
            ++m_stats.missed;
            emit feedback(false);
            if (m_stats.missed >= m_settings.missLimit) {
                stop();
                return;
            }
        }
    }
    m_spawnElapsed += seconds;
    const double interval = 1.05 - 0.055 * m_settings.level;
    if (m_spawnElapsed >= interval) {
        m_spawnElapsed = std::fmod(m_spawnElapsed, interval);
        spawnToken();
    }
    emit changed();
}

bool FallingGameModel::typeText(const QString &text)
{
    if (m_state != GameState::Playing)
        return false;
    const QString upper = text.toUpper();
    if (upper.size() != 1 || upper.at(0) < QLatin1Char('A') || upper.at(0) > QLatin1Char('Z'))
        return false;
    for (int i = 0; i < m_tokens.size(); ++i) {
        // 未进入画面的苹果不能通过提前输入被消除。
        if (m_tokens.at(i).symbol == upper.at(0) && m_tokens.at(i).y >= 0) {
            m_tokens.removeAt(i);
            ++m_stats.success;
            ++m_stageSuccess;
            emit feedback(true);
            if (m_stageSuccess >= m_settings.target)
                finishLevel();
            emit changed();
            return true;
        }
    }
    ++m_stats.wrong;
    emit feedback(false);
    emit changed();
    return false;
}

} // namespace RescueApple
