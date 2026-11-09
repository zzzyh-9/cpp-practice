#include "gamecontroller.h"

#include <QCursor>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

namespace RescueApple {

GameController::GameController(FallingGameModel *model, GameWindow *window, SkinManager *skins,
                               bool persistSettings, QObject *parent)
    : QObject(parent), m_model(model), m_window(window), m_skins(skins),
      m_gameTimer(this), m_sound(this), m_persistSettings(persistSettings)
{
    if (m_persistSettings) {
        QSettings saved;
        GameSettings settings;
        settings.level = saved.value(QStringLiteral("level"), 1).toInt();
        settings.maxApples = saved.value(QStringLiteral("maxApples"), 3).toInt();
        settings.target = saved.value(QStringLiteral("target"), 20).toInt();
        settings.soundEnabled = saved.value(QStringLiteral("sound"), true).toBool();
        settings.missLimit = saved.value(QStringLiteral("missLimit"), settings.missLimit).toInt();
        settings.musicEnabled = saved.value(QStringLiteral("music"), settings.soundEnabled).toBool();
        settings.soundVolume = saved.value(QStringLiteral("soundVolume"), settings.soundVolume).toInt();
        settings.musicVolume = saved.value(QStringLiteral("musicVolume"), settings.musicVolume).toInt();
        m_model->applySettings(settings);
    }
    m_gameTimer.setInterval(16);
    m_gameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_gameTimer, &QTimer::timeout, this, [this]() {
        // 按真实经过的时间移动，而不是每帧写死像素距离。
        const double seconds = qMin(m_elapsed.restart() / 1000.0, 0.10);
        m_model->advance(seconds);
    });
    connect(m_model, &FallingGameModel::changed, this, &GameController::refresh);
    connect(m_skins, &SkinManager::changed, this, &GameController::refresh);
    connect(m_model, &FallingGameModel::feedback, this, [this](bool hit) {
        if (hit)
            m_sound.playHit();
    });
    connect(window->gameView(), &GameView::textTyped, model, &FallingGameModel::typeText);
    connect(window->gameView(), &GameView::fieldResized, model, &FallingGameModel::setFieldSize);
    connect(window->gameView(), &GameView::pauseRequested, this, &GameController::togglePause);
    connect(window, &GameWindow::primaryRequested, this, &GameController::primaryAction);
    connect(window, &GameWindow::pauseRequested, this, &GameController::togglePause);
    connect(window, &GameWindow::stopRequested, model, &FallingGameModel::stop);
    connect(window, &GameWindow::settingsRequested, this, &GameController::openSettings);
    connect(window, &GameWindow::skinRequested, this, &GameController::chooseSkin);
    connect(window, &GameWindow::soundToggled, this, [this](bool enabled) {
        GameSettings settings = m_model->settings();
        settings.soundEnabled = enabled;
        m_model->applySettings(settings);
        saveSettings();
        m_window->gameView()->setFocus();
    });
    m_model->setFieldSize(window->gameView()->viewport()->size());
    refresh();
}

void GameController::refresh()
{
    m_model->setSpawnRanges(m_window->spawnRanges(m_model->tokenDiameter()));
    const bool playing = m_model->state() == GameState::Playing;
    if (playing && !m_gameTimer.isActive()) {
        m_elapsed.start();
        m_gameTimer.start();
    } else if (!playing) {
        m_gameTimer.stop();
    }
    m_window->display(*m_model);
    m_sound.synchronize(m_model->state(), m_model->settings(), m_skins->skin());
}

void GameController::primaryAction()
{
    if (m_model->awaitingNextLevel())
        m_model->nextLevel();
    else if (m_model->state() == GameState::Paused)
        m_model->resume();
    else if (m_model->state() == GameState::Idle || m_model->state() == GameState::End)
        m_model->start();
    m_window->gameView()->setFocus();
}

void GameController::togglePause()
{
    if (m_model->state() == GameState::Playing)
        m_model->pause();
    else if (m_model->state() == GameState::Paused && !m_model->awaitingNextLevel())
        m_model->resume();
    m_window->gameView()->setFocus();
}

void GameController::openSettings()
{
    const bool wasPlaying = m_model->state() == GameState::Playing;
    m_model->pause();
    GameSettings settings;
    if (m_window->editSettings(m_model->settings(), &settings)) {
        m_model->applySettings(settings);
        saveSettings();
    }
    if (wasPlaying)
        m_model->resume();
    m_window->gameView()->setFocus();
}

void GameController::chooseSkin()
{
    const bool wasPlaying = m_model->state() == GameState::Playing;
    m_model->pause();
    QMenu menu(m_window);
    auto *orchard = menu.addAction(QStringLiteral("内置果园"));
    auto *simple = menu.addAction(QStringLiteral("程序绘制 · 课堂简洁"));
    menu.addSeparator();
    auto *load = menu.addAction(QStringLiteral("加载其他皮肤文件夹…"));
    const QAction *selected = menu.exec(QCursor::pos());
    if (selected == orchard) {
        m_skins->useBuiltin();
    } else if (selected == simple) {
        m_skins->useBuiltin(false);
    } else if (selected == load) {
        const QString directory = QFileDialog::getExistingDirectory(m_window, QStringLiteral("选择皮肤资源文件夹"));
        if (!directory.isEmpty()) {
            QString error;
            if (!m_skins->loadDirectory(directory, &error))
                QMessageBox::warning(m_window, QStringLiteral("无法加载皮肤"), error);
        }
    }
    if (wasPlaying)
        m_model->resume();
    m_window->gameView()->setFocus();
}

void GameController::saveSettings()
{
    if (!m_persistSettings)
        return;
    QSettings saved;
    const GameSettings settings = m_model->settings();
    saved.setValue(QStringLiteral("level"), settings.level);
    saved.setValue(QStringLiteral("maxApples"), settings.maxApples);
    saved.setValue(QStringLiteral("target"), settings.target);
    saved.setValue(QStringLiteral("sound"), settings.soundEnabled);
    saved.setValue(QStringLiteral("missLimit"), settings.missLimit);
    saved.setValue(QStringLiteral("music"), settings.musicEnabled);
    saved.setValue(QStringLiteral("soundVolume"), settings.soundVolume);
    saved.setValue(QStringLiteral("musicVolume"), settings.musicVolume);
}

} // namespace RescueApple
