#pragma once

#include <QObject>
#include <QSoundEffect>

#include "model/fallinggamemodel.h"
#include "view/skinmanager.h"

namespace RescueApple {

class SoundPlayer : public QObject {
    Q_OBJECT
public:
    explicit SoundPlayer(QObject *parent = nullptr);
    void synchronize(GameState state, bool enabled, const GameSkin &skin);
    void playHit();

private:
    QSoundEffect m_music;
    QSoundEffect m_hit;
    QString m_musicPath;
    QString m_hitPath;
    bool m_enabled = false;
};

} // namespace RescueApple
