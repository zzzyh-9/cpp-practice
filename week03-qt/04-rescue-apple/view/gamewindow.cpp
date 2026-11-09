#include "gamewindow.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
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
#include <QStackedWidget>
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
    void setFallbackText(const QString &text) { m_fallback = text; }
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
            QFont captionFont = font();
            captionFont.setFamily(QStringLiteral("Microsoft YaHei"));
            captionFont.setPixelSize(qMax(captionFont.pixelSize(), qBound(13, height() / 3, 22)));
            captionFont.setBold(true);
            painter.setFont(captionFont);
            painter.setPen(QColor(image.isNull() ? QStringLiteral("#674321") : QStringLiteral("#fff9e9")));
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
    m_success->setObjectName(QStringLiteral("rescuedCount"));
    m_failure->setObjectName(QStringLiteral("missedCount"));
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

    m_pause = new SpriteButton(skins, QStringLiteral("apple_pause"), QStringLiteral("暂停"), surface);
    m_pause->setToolTip(QStringLiteral("暂停 / 继续（空格）"));
    m_pause->setObjectName(QStringLiteral("pauseButton"));
    m_stop = new SpriteButton(skins, QStringLiteral("apple_endgame"), QStringLiteral("结束"), surface);
    m_settings = new SpriteButton(skins, QStringLiteral("apple_settings"), QStringLiteral("设置"), surface);
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
    setupFullscreenTitleBar();
    arrangeOverlays();
}

void GameWindow::setupFullscreenTitleBar()
{
    m_fullscreenTitleBar = new QFrame(m_view->viewport());
    m_fullscreenTitleBar->setObjectName(QStringLiteral("fullscreenTitleBar"));
    m_fullscreenTitleBar->setStyleSheet(QStringLiteral(
        "QFrame#fullscreenTitleBar {background:#f4f3f0; border-bottom:1px solid #d6d2c9;}"
        "QLabel {color:#282828; font-size:14px;}"
        "QPushButton {color:#282828; background:transparent; border:0; font-size:18px;}"
        "QPushButton:hover {background:#e2e0dc;}"
        "QPushButton#fullscreenClose:hover {background:#c42b1c; color:white;}"));
    auto *layout = new QHBoxLayout(m_fullscreenTitleBar);
    layout->setContentsMargins(14, 0, 0, 1);
    layout->setSpacing(0);
    auto *title = new QLabel(windowTitle(), m_fullscreenTitleBar);
    connect(this, &QWidget::windowTitleChanged, title, &QLabel::setText);
    layout->addWidget(title, 1);
    const auto addButton = [this, layout](const QString &glyph, const QString &name, const char *id) {
        auto *button = new QPushButton(glyph, m_fullscreenTitleBar);
        button->setObjectName(QString::fromLatin1(id));
        button->setAccessibleName(name);
        button->setToolTip(name);
        button->setFixedWidth(48);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        return button;
    };
    auto *minimize = addButton(QStringLiteral("−"), QStringLiteral("最小化游戏"), "fullscreenMinimize");
    auto *restore = addButton(QStringLiteral("❐"), QStringLiteral("退出全屏"), "fullscreenRestore");
    auto *close = addButton(QStringLiteral("×"), QStringLiteral("关闭游戏"), "fullscreenClose");
    connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(restore, &QPushButton::clicked, this, &GameWindow::toggleFullScreen);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
}

void GameWindow::arrangeOverlays()
{
    const int width = m_view->viewport()->width();
    const int height = m_view->viewport()->height();
    const qreal scale = qBound(0.65, qMin(width / 1280.0, height / 720.0), 1.65);
    const auto scaled = [scale](qreal number) { return qRound(number * scale); };
    const bool showTitleBar = isFullScreen() && m_displayedState == GameState::Paused;
    const int top = showTitleBar ? qBound(34, scaled(34), 46) : 0;
    if (m_fullscreenTitleBar) {
        m_fullscreenTitleBar->setGeometry(0, 0, width, top);
        m_fullscreenTitleBar->setVisible(showTitleBar);
    }
    const QRectF basket = m_view->basketRect();
    const int scoreWidth = scaled(185);
    const int scoreHeight = scaled(60);
    const int scoreX = width - scoreWidth - scaled(10);
    const int missedY = qRound(basket.top()) - scoreHeight + scaled(5);
    m_successPlate->setGeometry(scoreX, missedY - scoreHeight - scaled(12), scoreWidth, scoreHeight);
    m_failurePlate->setGeometry(scoreX, missedY, scoreWidth, scoreHeight);
    m_success->setGeometry(0, 0, scaled(140), scoreHeight);
    m_failure->setGeometry(0, 0, scaled(140), scoreHeight);
    m_levelPlate->setGeometry((width - scaled(130)) / 2, top, scaled(130), scaled(124));
    m_level->setGeometry(scaled(13), scaled(66), scaled(104), scaled(36));
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(scaled(22));
    m_level->setFont(font);
    font.setPixelSize(qMax(12, scaled(17)));
    for (auto *label : {m_success, m_failure})
        label->setFont(font);
    font.setPixelSize(qMax(11, scaled(15)));
    m_accuracy->setFont(font);
    m_accuracy->setGeometry(scaled(20), top + scaled(18), scaled(172), scaled(30));

    const int icon = scaled(62);
    const int right = width - scaled(18);
    m_settings->setGeometry(right - scaled(70), top + scaled(14), scaled(70), scaled(89));
    m_stop->setGeometry(right - scaled(145), top + scaled(20), icon, icon);
    m_pause->setGeometry(right - scaled(220), top + scaled(20), icon, icon);
    const int footerArea = qRound(basket.left()) - 12;
    const int footerWidth = qMin(footerArea - 24, 470);
    m_footer->setGeometry((footerArea - footerWidth) / 2, height - 57, footerWidth, 43);
    const int statusWidth = qMin(footerArea - 24, 520);
    m_status->setGeometry((footerArea - statusWidth) / 2, height - 95, statusWidth, 28);
    const int overlayWidth = qMin(2 * scoreX - width - 24, 490);
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
    if (showTitleBar && m_fullscreenTitleBar)
        m_fullscreenTitleBar->raise();
}

void GameWindow::display(const FallingGameModel &model)
{
    m_view->display(model);
    const bool stateChanged = m_displayedState != model.state();
    m_displayedState = model.state();
    m_passed = model.awaitingNextLevel() || model.completed();
    const auto &stats = model.stats();
    m_success->setText(QStringLiteral("救下 %1/%2").arg(model.stageSuccess()).arg(model.settings().target));
    m_successPlate->setToolTip(QStringLiteral("本局累计救下 %1 个苹果").arg(stats.success));
    m_failure->setText(QStringLiteral("漏接 %1/%2").arg(stats.missed).arg(model.settings().missLimit));
    m_failurePlate->setToolTip(QStringLiteral("落到场景高度的 70% 时计为漏接。误击 %1 次").arg(stats.wrong));
    m_level->setText(QStringLiteral("第 %1 关").arg(model.settings().level));
    m_accuracy->setText(QStringLiteral("准确率  %1%").arg(stats.accuracy(), 0, 'f', 1));
    const bool paused = model.state() == GameState::Paused && !model.awaitingNextLevel();
    m_status->setText(paused ? QStringLiteral("游戏暂停 · 空格继续")
                             : QStringLiteral("输入苹果上的字母 · 空格暂停"));
    const QSignalBlocker blocker(m_sound);
    m_sound->setChecked(model.settings().soundEnabled);
    const bool playing = model.state() == GameState::Playing;
    m_pause->setEnabled(playing || (model.state() == GameState::Paused && !model.awaitingNextLevel()));
    m_pause->setFallbackText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    m_pause->setAsset(model.state() == GameState::Paused ? QStringLiteral("apple_resume") : QStringLiteral("apple_pause"));
    m_stop->setEnabled(playing || model.state() == GameState::Paused);
    const bool showOverlay = !playing && !paused;
    m_overlay->setVisible(showOverlay);
    if (showOverlay) {
        if (model.state() == GameState::Idle) {
            m_overlayTitle->setText(QStringLiteral("拯救苹果 2026"));
            m_overlayText->setText(QStringLiteral("看清字母，按下对应按键。\n在苹果落下前救下它！"));
            m_primary->setAsset(QStringLiteral("apple_start"));
        } else if (model.awaitingNextLevel()) {
            m_overlayTitle->setText(QStringLiteral("第 %1 关通过！").arg(model.settings().level));
            m_overlayText->setText(QStringLiteral("本关已救下 %1 个苹果。\n准备好迎接更快的下一关了吗？").arg(model.stageSuccess()));
            m_primary->setAsset(QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("进入下一关"));
        } else {
            QString title = QStringLiteral("本局结束");
            if (model.completed())
                title = QStringLiteral("十关挑战完成！");
            else if (stats.missed >= model.settings().missLimit)
                title = QStringLiteral("已达到漏接上限");
            m_overlayTitle->setText(title);
            m_overlayText->setText(QStringLiteral("成功 %1  ·  漏接 %2  ·  误击 %3\n准确率 %4%")
                                      .arg(stats.success).arg(stats.missed).arg(stats.wrong)
                                      .arg(stats.accuracy(), 0, 'f', 1));
            m_primary->setAsset(QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("再来一局"));
        }
    }
    if (showOverlay || stateChanged)
        arrangeOverlays();
}

QVector<QPair<qreal, qreal>> GameWindow::spawnRanges(qreal tokenDiameter) const
{
    // 只向 Model 传递数值范围，避免苹果被计分板、关卡牌或按钮遮住。
    // 苹果仍从屏幕上方进入，字母不会被这些 UI 控件挡住。
    const qreal width = m_view->viewport()->width();
    const qreal margin = tokenDiameter / 2.0 + 7.0;
    QVector<QPair<qreal, qreal>> ranges;
    const qreal left = (m_accuracy->geometry().right() + margin) / width;
    const qreal middleLeft = (m_levelPlate->geometry().left() - margin) / width;
    const qreal middleRight = (m_levelPlate->geometry().right() + margin) / width;
    const qreal right = (qMin(m_pause->geometry().left(), m_successPlate->geometry().left()) - margin) / width;
    if (left < middleLeft)
        ranges.append(qMakePair(left, middleLeft));
    if (middleRight < right)
        ranges.append(qMakePair(middleRight, right));
    return ranges;
}

bool GameWindow::editSettings(const GameSettings &current, GameSettings *result)
{
    const QSize viewportSize = m_view->viewport()->size();
    const qreal scale = qBound(1.0, qMin(viewportSize.width() / 1024.0,
                                        viewportSize.height() / 680.0), 2.6);
    const auto px = [scale](int value) { return qRound(value * scale); };
    const auto panelRect = [px](int x, int y, int width, int height) {
        return QRect(px(x), px(y), px(width), px(height));
    };

    QDialog dialog(this, Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setWindowTitle(QStringLiteral("游戏设置"));
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setFixedSize(px(468), px(360));
    QFont settingsFont(QStringLiteral("Microsoft YaHei"));
    settingsFont.setPixelSize(px(15));
    dialog.setFont(settingsFont);
    dialog.setStyleSheet(QStringLiteral(
        "QLabel,QCheckBox,QPushButton {color:#674321; font-size:%7px;}"
        "QCheckBox::indicator {width:%1px; height:%1px;}"
        "QSlider::groove:horizontal {height:%2px; background:#fff7ea; border-radius:%3px;}"
        "QSlider::sub-page:horizontal {background:#a85e36; border-radius:%3px;}"
        "QSlider::handle:horizontal {background:#d89d71; border:%4px solid #a85e36;"
        "width:%5px; margin:-%3px 0; border-radius:%6px;}"
        "QPushButton#restoreDefaults {color:#77411e; border:none; background:transparent;}"
        "QPushButton#restoreDefaults:hover {text-decoration:underline;}")
        .arg(px(14)).arg(px(7)).arg(px(3)).arg(px(1)).arg(px(12)).arg(px(6)).arg(px(15)));

    auto *board = new ImagePanel(m_skins, QStringLiteral("apple_common_panel_base"), &dialog);
    board->setGeometry(panelRect(4, 21, 421, 333));
    auto *title = new QLabel(&dialog);
    title->setGeometry(panelRect(69, 0, 291, 68));
    title->setAlignment(Qt::AlignCenter);
    const QPixmap titleImage = m_skins->frame(QStringLiteral("apple_settings_panel_title"));
    if (!titleImage.isNull())
        title->setPixmap(titleImage.scaled(title->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else {
        title->setText(QStringLiteral("游戏设置"));
        title->setStyleSheet(QStringLiteral("font-size:%1px; font-weight:bold;").arg(px(26)));
    }
    auto *close = new SpriteButton(m_skins, QStringLiteral("apple_common_panel_close"), QStringLiteral("×"), &dialog);
    close->setGeometry(panelRect(378, 23, 29, 29));
    connect(close, &QAbstractButton::clicked, &dialog, &QDialog::reject);

    auto *content = new ImagePanel(m_skins, QStringLiteral("apple_common_panel_content_bg"), &dialog);
    content->setGeometry(panelRect(53, 77, 322, 207));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(px(24), px(13), px(24), px(16));
    contentLayout->setSpacing(px(6));
    auto *restore = new QPushButton(QStringLiteral("恢复默认"), content);
    restore->setObjectName(QStringLiteral("restoreDefaults"));
    contentLayout->addWidget(restore, 0, Qt::AlignRight);

    auto *pages = new QStackedWidget(content);
    pages->setObjectName(QStringLiteral("settingsPages"));
    auto *gameplay = new QWidget(pages);
    auto *audio = new QWidget(pages);
    auto *gameForm = new QFormLayout(gameplay);
    auto *audioForm = new QFormLayout(audio);
    for (auto *form : {gameForm, audioForm}) {
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(px(8));
        form->setHorizontalSpacing(px(10));
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }

    const auto addSlider = [px](QFormLayout *form, const QString &label, const char *name,
                                int minimum, int maximum, int value) {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(px(6));
        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setMinimumHeight(px(20));
        slider->setObjectName(QString::fromLatin1(name));
        slider->setRange(minimum, maximum);
        slider->setValue(value);
        auto *number = new QLabel(QString::number(value), row);
        number->setFixedWidth(px(29));
        number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, number, [number](int changed) {
            number->setText(QString::number(changed));
        });
        layout->addWidget(slider);
        layout->addWidget(number);
        form->addRow(label, row);
        return slider;
    };
    auto *level = addSlider(gameForm, QStringLiteral("游戏难度"), "difficulty", 1, 10, current.level);
    auto *target = addSlider(gameForm, QStringLiteral("过关目标"), "stageTarget", 1, 999, current.target);
    auto *limit = addSlider(gameForm, QStringLiteral("漏接上限"), "missLimit", 1, 50, current.missLimit);
    auto *count = addSlider(gameForm, QStringLiteral("同屏苹果"), "appleCount", 1, 5, current.maxApples);

    auto *toggles = new QWidget(audio);
    auto *toggleLayout = new QHBoxLayout(toggles);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setSpacing(px(6));
    auto *sound = new QCheckBox(QStringLiteral("击中音效"), toggles);
    auto *music = new QCheckBox(QStringLiteral("背景音乐"), toggles);
    sound->setObjectName(QStringLiteral("hitAudio"));
    music->setObjectName(QStringLiteral("backgroundAudio"));
    sound->setChecked(current.soundEnabled);
    music->setChecked(current.musicEnabled);
    toggleLayout->addWidget(sound);
    toggleLayout->addWidget(music);
    audioForm->addRow(toggles);
    auto *soundVolume = addSlider(audioForm, QStringLiteral("音效音量"), "soundVolume", 0, 100, current.soundVolume);
    auto *musicVolume = addSlider(audioForm, QStringLiteral("音乐音量"), "musicVolume", 0, 100, current.musicVolume);

    pages->addWidget(gameplay);
    pages->addWidget(audio);
    contentLayout->addWidget(pages, 1);
    auto *gameTab = new SpriteButton(m_skins, QStringLiteral("apple_tab_gameplay"), QStringLiteral("玩法"), &dialog);
    auto *audioTab = new SpriteButton(m_skins, QStringLiteral("apple_tab_audio"), QStringLiteral("音频"), &dialog);
    audioTab->setObjectName(QStringLiteral("audioTab"));
    gameTab->setGeometry(panelRect(375, 109, 78, 30));
    audioTab->setGeometry(panelRect(375, 151, 78, 30));
    connect(gameTab, &QAbstractButton::clicked, pages, [pages]() { pages->setCurrentIndex(0); });
    connect(audioTab, &QAbstractButton::clicked, pages, [pages]() { pages->setCurrentIndex(1); });
    connect(restore, &QPushButton::clicked, &dialog, [=]() {
        const GameSettings defaults;
        if (pages->currentIndex() == 0) {
            level->setValue(defaults.level);
            count->setValue(defaults.maxApples);
            target->setValue(defaults.target);
            limit->setValue(defaults.missLimit);
        } else {
            sound->setChecked(defaults.soundEnabled);
            music->setChecked(defaults.musicEnabled);
            soundVolume->setValue(defaults.soundVolume);
            musicVolume->setValue(defaults.musicVolume);
        }
    });

    auto *cancel = new SpriteButton(m_skins, QStringLiteral("apple_common_panel_button_secondary"), QStringLiteral("取消"), &dialog);
    cancel->setAsset(QStringLiteral("apple_common_panel_button_secondary"), QStringLiteral("取消"));
    cancel->setGeometry(panelRect(97, 290, 105, 34));
    auto *confirm = new SpriteButton(m_skins, QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("确认"), &dialog);
    confirm->setObjectName(QStringLiteral("confirmSettings"));
    confirm->setAsset(QStringLiteral("apple_common_panel_button_primary"), QStringLiteral("确认"));
    confirm->setGeometry(panelRect(225, 290, 105, 34));
    connect(cancel, &QAbstractButton::clicked, &dialog, &QDialog::reject);
    connect(confirm, &QAbstractButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    *result = {level->value(), count->value(), target->value(), sound->isChecked(),
               limit->value(), music->isChecked(), soundVolume->value(), musicVolume->value()};
    return true;
}

void GameWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_fullscreenTitleBar)
        arrangeOverlays();
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
