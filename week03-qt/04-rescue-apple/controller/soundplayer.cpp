#include "soundplayer.h"

#include <QUrl>

namespace RescueApple {

static QUrl soundUrl(const QString &path)
{
    return path.startsWith(QLatin1Char(':')) ? QUrl(QStringLiteral("qrc") + path) : QUrl::fromLocalFile(path);
}

SoundPlayer::SoundPlayer(QObject *parent) : QObject(parent), m_music(this), m_hit(this)
{
    m_music.setLoopCount(QSoundEffect::Infinite);
}

void SoundPlayer::synchronize(GameState state, const GameSettings &settings, const GameSkin &skin)
{
    m_enabled = settings.soundEnabled;
    m_music.setVolume(settings.musicVolume / 100.0);
    m_hit.setVolume(settings.soundVolume / 100.0);
    if (skin.backgroundSoundPath != m_musicPath) {
        m_music.stop();
        m_musicPath = skin.backgroundSoundPath;
        m_music.setSource(m_musicPath.isEmpty() ? QUrl() : soundUrl(m_musicPath));
    }
    if (skin.hitSoundPath != m_hitPath) {
        m_hit.stop();
        m_hitPath = skin.hitSoundPath;
        m_hit.setSource(m_hitPath.isEmpty() ? QUrl() : soundUrl(m_hitPath));
    }
    if (settings.musicEnabled && state == GameState::Playing && !m_musicPath.isEmpty()) {
        if (!m_music.isPlaying())
            m_music.play();
    } else {
        m_music.stop();
    }
    if (!m_enabled)
        m_hit.stop();
}

void SoundPlayer::playHit()
{
    if (!m_enabled || m_hitPath.isEmpty())
        return;
    m_hit.stop();
    m_hit.play();
}

} // namespace RescueApple
