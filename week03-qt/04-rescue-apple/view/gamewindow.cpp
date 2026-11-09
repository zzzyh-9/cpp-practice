#include "gamewindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWindow>

namespace RescueApple {

// 老师的按钮图片是一行三帧：普通、悬停、按下。
class SpriteButton : public QAbstractButton {
public:
    SpriteButton(SkinManager *skins, const QString &asset, const QString &fallback, QWidget *parent)
        : QAbstractButton(parent), m_skins(skins), m_asset(asset), m_fallback(fallback)
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(fallback);
        setToolTip(fallback);
        connect(skins, &SkinManager::changed, this, [this]() { update(); });
    }
    void setAsset(const QString &asset, const QString &caption = QString())
    {
        if (m_asset == asset && m_caption == caption)
            return;
        m_asset = asset;
        m_caption = caption;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        if (!isEnabled())
            painter.setOpacity(0.45);
        const int state = isDown() ? 2 : underMouse() ? 1 : 0;
        const QPixmap image = m_skins->frame(m_asset, state);
        if (image.isNull()) {
            painter.setPen(QPen(QColor(QStringLiteral("#90613c")), 1));
            painter.setBrush(QColor(QStringLiteral("#ffe4a2")));
            painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
        } else {
            painter.drawPixmap(rect(), image);
        }
        const QString caption = m_caption.isEmpty() && image.isNull() ? m_fallback : m_caption;
        if (!caption.isEmpty()) {
            QFont font(QStringLiteral("Microsoft YaHei"));
            font.setPixelSize(qBound(13, height() / 3, 22));
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(QColor(QStringLiteral("#fff9e9")));
            painter.drawText(rect(), Qt::AlignCenter, caption);
        }
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
            update();
        return QAbstractButton::event(event);
    }
private:
    SkinManager *m_skins;
    QString m_asset;
    QString m_fallback;
    QString m_caption;
};

class ImagePanel : public QWidget {
public:
    ImagePanel(SkinManager *skins, const QString &key, QWidget *parent)
        : QWidget(parent), m_skins(skins), m_key(key)
    {
        connect(skins, &SkinManager::changed, this, [this]() { update(); });
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap image = m_skins->frame(m_key);
        if (!image.isNull()) {
            painter.drawPixmap(rect(), image);
        } else {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 250, 224, 235));
            painter.drawRoundedRect(rect(), 18, 18);
        }
    }
private:
    SkinManager *m_skins;
    QString m_key;
};

GameWindow::GameWindow(SkinManager *skins, QWidget *parent)
    : QMainWindow(parent), m_skins(skins), m_view(new GameView(skins, this))
{
    setWindowTitle(QStringLiteral("拯救苹果 2026 · 练习4"));
    setMinimumSize(640, 480);
    resize(1100, 760);
    setCentralWidget(m_view);
    auto *surface = m_view->viewport();
    m_successPlate = new ImagePanel(skins, QStringLiteral("apple_score_collected_bg"), surface);
    m_failurePlate = new ImagePanel(skins, QStringLiteral("apple_score_broken_bg"), surface);
    m_levelPlate = new ImagePanel(skins, QStringLiteral("apple_level_bg"), surface);
    m_success = new QLabel(m_successPlate);
    m_failure = new QLabel(m_failurePlate);
    m_level = new QLabel(m_levelPlate);
    for (auto *label : {m_success, m_failure, m_level}) {
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color: #694221; background: transparent; font-weight: bold;"));
    }
    m_accuracy = new QLabel(surface);
    m_accuracy->setAlignment(Qt::AlignCenter);
    m_accuracy->setStyleSheet(QStringLiteral("color:#573c22; background:rgba(255,249,222,220); border-radius:10px; padding:3px;"));
    m_status = new QLabel(surface);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color:#573c22; background:rgba(255,249,222,220); border-radius:12px; padding:5px;"));

    m_pause = new SpriteButton(skins, QStringLiteral("apple_pause"), QStringLiteral("暂停 / 继续（空格）"), surface);
    m_pause->setObjectName(QStringLiteral("pauseButton"));
    m_stop = new SpriteButton(skins, QStringLiteral("apple_endgame"), QStringLiteral("结束本局"), surface);
    m_settings = new SpriteButton(skins, QStringLiteral("apple_settings"), QStringLiteral("游戏设置"), surface);
    connect(m_pause, &QAbstractButton::clicked, this, &GameWindow::pauseRequested);
    connect(m_stop, &QAbstractButton::clicked, this, &GameWindow::stopRequested);
    connect(m_settings, &QAbstractButton::clicked, this, &GameWindow::settingsRequested);

    m_overlay = new ImagePanel(skins, QStringLiteral("apple_common_panel_content_bg"), surface);
    auto *overlayLayout = new QVBoxLayout(m_overlay);
    overlayLayout->setContentsMargins(35, 28, 35, 28);
    overlayLayout->setSpacing(10);
    m_overlayGraphic = new QLabel(m_overlay);
    m_overlayGraphic->setAlignment(Qt::AlignCenter);
    m_overlayTitle = new QLabel(m_overlay);
    m_overlayTitle->setAlignment(Qt::AlignCenter);
    m_overlayTitle->setStyleSheet(QStringLiteral("color: #5d3d22; font-size: 27px; font-weight: bold; background: transparent;"));
    m_overlayText = new QLabel(m_overlay);
    m_overlayText->setAlignment(Qt::AlignCenter);
    m_overlayText->setWordWrap(true);
    m_overlayText->setStyleSheet(QStringLiteral("color:#755536; font-size:15px; background:transparent;"));
    m_primary = new SpriteButton(skins, QStringLiteral("apple_start"), QStringLiteral("开始游戏"), m_overlay);
    m_primary->setObjectName(QStringLiteral("primaryAction"));
    m_primary->setFixedSize(215, 70);
    overlayLayout->addStretch();
    overlayLayout->addWidget(m_overlayGraphic);
    overlayLayout->addWidget(m_overlayTitle);
    overlayLayout->addWidget(m_overlayText);
    overlayLayout->addSpacing(6);
    overlayLayout->addWidget(m_primary, 0, Qt::AlignCenter);
    overlayLayout->addStretch();
    connect(m_primary, &QAbstractButton::clicked, this, &GameWindow::primaryRequested);

    m_footer = new QFrame(surface);
    m_footer->setObjectName(QStringLiteral("footer"));
    m_footer->setStyleSheet(QStringLiteral(
        "QFrame#footer {background:rgba(255,247,220,235); border:1px solid #d7b77c; border-radius:13px;}"
        "QPushButton {background:transparent; color:#674321; border:none; padding:5px 10px;}"
        "QPushButton:hover {background:#f6dfaf; border-radius:7px;}"
        "QCheckBox {color:#674321; background:transparent;}"
        "QComboBox {color:#674321; background:#fff3d4; border:1px solid #d7b77c; border-radius:5px; padding:3px;}"));
    auto *footerLayout = new QHBoxLayout(m_footer);
    footerLayout->setContentsMargins(12, 5, 12, 5);
    footerLayout->setSpacing(8);
    auto *skinButton = new QPushButton(QStringLiteral("更换皮肤"), m_footer);
    auto *fullscreenButton = new QPushButton(QStringLiteral("全屏 F11"), m_footer);
    for (auto *button : {skinButton, fullscreenButton}) {
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
    }
    m_sound = new QCheckBox(QStringLiteral("音效"), m_footer);
    m_sound->setFocusPolicy(Qt::NoFocus);
    m_screens = new QComboBox(m_footer);
    m_screens->setFocusPolicy(Qt::NoFocus);
    m_screens->setMinimumWidth(105);
    footerLayout->addWidget(skinButton);
    footerLayout->addWidget(fullscreenButton);
    footerLayout->addWidget(m_sound);
    footerLayout->addWidget(m_screens);
    connect(skinButton, &QPushButton::clicked, this, &GameWindow::skinRequested);
    connect(fullscreenButton, &QPushButton::clicked, this, &GameWindow::toggleFullScreen);
    connect(m_sound, &QCheckBox::toggled, this, &GameWindow::soundToggled);
    connect(m_screens, QOverload<int>::of(&QComboBox::activated), this, &GameWindow::moveToScreen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { updateScreens(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { updateScreens(); });
    updateScreens();

    auto *fullscreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullscreen, &QShortcut::activated, this, &GameWindow::toggleFullScreen);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this]() {
        if (m_displayedState == GameState::Playing)
            emit pauseRequested();
        if (isFullScreen())
            showNormal();
    });
    connect(m_skins, &SkinManager::changed, this, [this]() { arrangeOverlays(); });
    arrangeOverlays();
}

void GameWindow::arrangeOverlays()
{
    const int width = m_view->viewport()->width();
    const int height = m_view->viewport()->height();
    const qreal scale = qBound(0.65, qMin(width / 1280.0, height / 720.0), 1.65);
    const auto scaled = [scale](qreal number) { return qRound(number * scale); };
    m_successPlate->setGeometry(scaled(20), scaled(18), scaled(205), scaled(76));
    m_failurePlate->setGeometry(scaled(20), scaled(100), scaled(205), scaled(76));
    m_success->setGeometry(scaled(10), scaled(14), scaled(145), scaled(45));
    m_failure->setGeometry(scaled(10), scaled(14), scaled(145), scaled(45));
    m_levelPlate->setGeometry((width - scaled(130)) / 2, 0, scaled(130), scaled(124));
    m_level->setGeometry(scaled(13), scaled(66), scaled(104), scaled(36));
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(scaled(22));
    for (auto *label : {m_success, m_failure, m_level})
        label->setFont(font);
    font.setPixelSize(qMax(11, scaled(15)));
    m_accuracy->setFont(font);
    m_accuracy->setGeometry(scaled(20), scaled(184), scaled(205), scaled(30));

    const int icon = scaled(62);
    const int right = width - scaled(18);
    m_settings->setGeometry(right - scaled(70), scaled(14), scaled(70), scaled(89));
    m_stop->setGeometry(right - scaled(145), scaled(20), icon, icon);
    m_pause->setGeometry(right - scaled(220), scaled(20), icon, icon);
    const int footerWidth = qMin(width - 24, 470);
    m_footer->setGeometry((width - footerWidth) / 2, height - 57, footerWidth, 43);
    m_status->setGeometry((width - qMin(width - 24, 520)) / 2, height - 95, qMin(width - 24, 520), 28);
    const int overlayWidth = qMin(width - 60, 490);
    const int overlayHeight = qMin(height - 170, 330);
    m_overlay->setGeometry((width - overlayWidth) / 2, (height - overlayHeight) / 2,
                            overlayWidth, overlayHeight);
    const QString graphic = m_passed ? QStringLiteral("apple_success_panel_title") : QString();
    if (!graphic.isEmpty()) {
        const QPixmap image = m_skins->frame(graphic);
        m_overlayGraphic->setPixmap(image.scaled(250, 95, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_overlayGraphic->setVisible(!image.isNull());
    } else {
        m_overlayGraphic->hide();
    }
    m_overlay->raise();
    m_footer->raise();
}

void GameWindow::display(const FallingGameModel &model)
{
    m_view->display(model);
    m_displayedState = model.state();
    m_passed = model.awaitingNextLevel() || model.completed();
    const auto &stats = model.stats();
    m_success->setText(QStringLiteral("成功  %1").arg(stats.success));
    m_failure->setText(QStringLiteral("失败  %1").arg(stats.failures()));
    m_failurePlate->setToolTip(QStringLiteral("漏接 %1 · 误击 %2").arg(stats.missed).arg(stats.wrong));
    m_level->setText(QStringLiteral("第 %1 关").arg(model.settings().level));
    m_accuracy->setText(QStringLiteral("准确率  %1%").arg(stats.accuracy(), 0, 'f', 1));
    m_status->setText(QStringLiteral("本关 %1 / %2  ·  输入苹果字母  ·  空格暂停")
                         .arg(model.stageSuccess()).arg(model.settings().target));
    const QSignalBlocker blocker(m_sound);
    m_sound->setChecked(model.settings().soundEnabled);
    const bool playing = model.state() == GameState::Playing;
    m_pause->setEnabled(playing || (model.state() == GameState::Paused && !model.awaitingNextLevel()));
    m_pause->setAsset(model.state() == GameState::Paused ? QStringLiteral("apple_resume") : QStringLiteral("apple_pause"));
    m_stop->setEnabled(playing || model.state() == GameState::Paused);
    m_overlay->setVisible(!playing);
    if (!playing) {
        if (model.state() == GameState::Idle) {
            m_overlayTitle->setText(QStringLiteral("拯救苹果 2026"));
            m_overlayText->setText(QStringLiteral("看清字母，按下对应按键。\n在苹果越过警戒线前救下它！"));
            m_primary->setAsset(QStringLiteral("apple_start"));
        } else if (model.awaitingNextLevel()) {
            m_overlayTitle->setText(QStringLiteral("第 %1 关通过！").arg(model.settings().level));
            m_overlayText->setText(QStringLiteral("本关已救下 %1 个苹果。\n准备好迎接更快的下一关了吗？").arg(model.stageSuccess()));
            m_primary->setAsset(QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("进入下一关"));
        } else if (model.state() == GameState::Paused) {
            m_overlayTitle->setText(QStringLiteral("游戏已暂停"));
            m_overlayText->setText(QStringLiteral("苹果已经停住，放松一下。\n点击继续或按空格返回游戏。"));
            m_primary->setAsset(QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("继续游戏"));
        } else {
            m_overlayTitle->setText(model.completed() ? QStringLiteral("十关挑战完成！") : QStringLiteral("本局结束"));
            m_overlayText->setText(QStringLiteral("成功 %1  ·  漏接 %2  ·  误击 %3\n准确率 %4%")
                                      .arg(stats.success).arg(stats.missed).arg(stats.wrong)
                                      .arg(stats.accuracy(), 0, 'f', 1));
            m_primary->setAsset(QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("再来一局"));
        }
        arrangeOverlays();
    }
}

QVector<QPair<qreal, qreal>> GameWindow::spawnRanges(qreal tokenDiameter) const
{
    // 只向 Model 传递数值范围，避开顶部计分板、关卡牌和控制按钮。
    // 苹果仍从屏幕上方进入，字母不会被这些 UI 控件挡住。
    const qreal width = m_view->viewport()->width();
    const qreal margin = tokenDiameter / 2.0 + 7.0;
    QVector<QPair<qreal, qreal>> ranges;
    const qreal left = (m_successPlate->geometry().right() + margin) / width;
    const qreal middleLeft = (m_levelPlate->geometry().left() - margin) / width;
    const qreal middleRight = (m_levelPlate->geometry().right() + margin) / width;
    const qreal right = (m_pause->geometry().left() - margin) / width;
    if (left < middleLeft)
        ranges.append(qMakePair(left, middleLeft));
    if (middleRight < right)
        ranges.append(qMakePair(middleRight, right));
    return ranges;
}

bool GameWindow::editSettings(const GameSettings &current, GameSettings *result)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("拯救苹果 · 设置"));
    dialog.setMinimumWidth(370);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog {background:#fff3d6; color:#674321;}"
        "QLabel,QCheckBox {color:#674321;}"
        "QSlider::groove:horizontal {height:8px; background:#dcb77b; border-radius:4px;}"
        "QSlider::handle:horizontal {background:#a6622d; width:20px; margin:-6px 0; border-radius:10px;}"
        "QSpinBox {padding:5px; background:#fffaf0; border:1px solid #d6b882; border-radius:5px;}"
        "QPushButton {padding:7px 18px;}"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(&dialog);
    const QPixmap titleImage = m_skins->frame(QStringLiteral("apple_settings_panel_title"));
    if (!titleImage.isNull())
        title->setPixmap(titleImage.scaled(290, 65, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        title->setText(QStringLiteral("游戏设置"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto *form = new QFormLayout;
    auto *levelRow = new QWidget(&dialog);
    auto *levelLayout = new QHBoxLayout(levelRow);
    levelLayout->setContentsMargins(0, 0, 0, 0);
    auto *level = new QSlider(Qt::Horizontal, levelRow);
    level->setRange(1, 10);
    level->setValue(current.level);
    auto *levelText = new QLabel(QString::number(current.level), levelRow);
    connect(level, &QSlider::valueChanged, levelText, [levelText](int value) { levelText->setText(QString::number(value)); });
    levelLayout->addWidget(level);
    levelLayout->addWidget(levelText);
    auto *count = new QSpinBox(&dialog);
    count->setRange(1, 5);
    count->setValue(current.maxApples);
    auto *target = new QSpinBox(&dialog);
    target->setRange(1, 999);
    target->setValue(current.target);
    auto *sound = new QCheckBox(QStringLiteral("开启音乐和击中音效"), &dialog);
    sound->setChecked(current.soundEnabled);
    form->addRow(QStringLiteral("等级 / 下落速度"), levelRow);
    form->addRow(QStringLiteral("同屏最多苹果"), count);
    form->addRow(QStringLiteral("每关目标数量"), target);
    form->addRow(sound);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *result = {level->value(), count->value(), target->value(), sound->isChecked()};
    return true;
}

void GameWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    arrangeOverlays();
}

void GameWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
    m_view->setFocus();
}

void GameWindow::updateScreens()
{
    const QSignalBlocker blocker(m_screens);
    m_screens->clear();
    const auto screens = QGuiApplication::screens();
    for (int index = 0; index < screens.size(); ++index) {
        const QSize size = screens.at(index)->geometry().size();
        m_screens->addItem(QStringLiteral("屏幕 %1 · %2×%3").arg(index + 1).arg(size.width()).arg(size.height()));
        if (windowHandle() && windowHandle()->screen() == screens.at(index))
            m_screens->setCurrentIndex(index);
    }
    m_screens->setToolTip(QStringLiteral("选择运行游戏的显示器"));
}

void GameWindow::moveToScreen(int index)
{
    const auto screens = QGuiApplication::screens();
    if (index < 0 || index >= screens.size())
        return;
    const bool fullscreen = isFullScreen();
    if (fullscreen)
        showNormal();
    (void)winId();
    windowHandle()->setScreen(screens.at(index));
    const QRect available = screens.at(index)->availableGeometry();
    const QSize size(qMin(width(), available.width()), qMin(height(), available.height()));
    setGeometry(QRect(available.center() - QPoint(size.width() / 2, size.height() / 2), size));
    if (fullscreen)
        showFullScreen();
    m_view->setFocus();
}

} // namespace RescueApple
