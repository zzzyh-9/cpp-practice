#pragma once

#include <QMainWindow>

#include "gameview.h"

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QFrame;

namespace RescueApple {

class ImagePanel;
class SpriteButton;

class GameWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit GameWindow(SkinManager *skins, QWidget *parent = nullptr);
    GameView *gameView() const { return m_view; }
    QVector<QPair<qreal, qreal>> spawnRanges(qreal tokenDiameter) const;
    void display(const FallingGameModel &model);
    bool editSettings(const GameSettings &current, GameSettings *result);
    void toggleFullScreen();

signals:
    void primaryRequested();
    void pauseRequested();
    void stopRequested();
    void settingsRequested();
    void skinRequested();
    void soundToggled(bool enabled);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrangeOverlays();
    void updateScreens();
    void moveToScreen(int index);

    SkinManager *m_skins;
    GameView *m_view;
    ImagePanel *m_successPlate;
    ImagePanel *m_failurePlate;
    ImagePanel *m_levelPlate;
    QLabel *m_success;
    QLabel *m_failure;
    QLabel *m_level;
    QLabel *m_accuracy;
    QLabel *m_status;
    SpriteButton *m_pause;
    SpriteButton *m_stop;
    SpriteButton *m_settings;
    ImagePanel *m_overlay;
    QLabel *m_overlayTitle;
    QLabel *m_overlayGraphic;
    QLabel *m_overlayText;
    SpriteButton *m_primary;
    QFrame *m_footer;
    QCheckBox *m_sound;
    QComboBox *m_screens;
    GameState m_displayedState = GameState::Idle;
    bool m_passed = false;
};

} // namespace RescueApple
