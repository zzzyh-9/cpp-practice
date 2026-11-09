#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "model/fallinggamemodel.h"
#include "soundplayer.h"
#include "view/gamewindow.h"

namespace RescueApple {

class GameController : public QObject {
    Q_OBJECT
public:
    GameController(FallingGameModel *model, GameWindow *window, SkinManager *skins,
                   bool persistSettings = true, QObject *parent = nullptr);

private:
    void refresh();
    void primaryAction();
    void togglePause();
    void openSettings();
    void chooseSkin();
    void saveSettings();

    FallingGameModel *m_model;
    GameWindow *m_window;
    SkinManager *m_skins;
    QTimer m_gameTimer;
    QElapsedTimer m_elapsed;
    SoundPlayer m_sound;
    bool m_persistSettings;
};

} // namespace RescueApple
