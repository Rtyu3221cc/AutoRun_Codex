#include "QuotaCard.h"

#include "ui/AvatarUtils.h"
#include "ui/PillProgress.h"
#include "ui/Theme.h"

#include <QAbstractAnimation>
#include <QDateTime>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace arc {

namespace {

// 清空布局（含嵌套布局），控件延迟删除，避免在信号处理过程中销毁 sender。
void clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (auto *childLayout = dynamic_cast<QLayout *>(item)) {
            clearLayout(childLayout);
            delete childLayout;
        } else if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
            delete item;
        } else {
            delete item;
        }
    }
}

QString windowCaption(const RateLimitWindow &window, const QString &fallback)
{
    if (window.windowDurationMins == 300)
        return QStringLiteral("5h 窗口");
    if (window.windowDurationMins == 10080)
        return QStringLiteral("每周窗口");
    if (window.windowDurationMins > 0)
        return QStringLiteral("%1 分钟窗口").arg(window.windowDurationMins);
    return fallback;
}

// 展开/吸收动画的起止小矩形（以悬浮条朝向屏幕内部的那一点为中心）。
QRect smallAnchorRect(const QPoint &anchor)
{
    const int width = 52;
    const int height = 28;
    return QRect(anchor.x() - width / 2, anchor.y() - height / 2, width, height);
}

// 运行期重建时新控件默认是隐藏的，布局 sizeHint 会把它们算小，导致卡片被瞬间压扁。
void showLayoutWidgets(QLayout *layout)
{
    if (!layout)
        return;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (QWidget *widget = item->widget())
            widget->show();
        else if (QLayout *childLayout = item->layout())
            showLayoutWidgets(childLayout);
    }
}

// 在可见屏幕上夹住卡片，避免超出工作区。
void keepInsideScreen(QWidget *widget)
{
    QScreen *screen = QGuiApplication::screenAt(widget->frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect available = screen->availableGeometry();
    const int margin = 8;
    const int maxX = qMax(available.left() + margin, available.right() - widget->width() + 1 - margin);
    const int maxY = qMax(available.top() + margin, available.bottom() - widget->height() + 1 - margin);
    const int x = qBound(available.left() + margin, widget->x(), maxX);
    const int y = qBound(available.top() + margin, widget->y(), maxY);
    if (x != widget->x() || y != widget->y())
        widget->move(x, y);
}

// 计划胶囊：自绘，按字形实际墨迹 bbox 做垂直居中。
// 不同字体（如 HarmonyOS Sans）的 ascent/descent 不对称，QLabel 居中线会让文字看起来偏上/偏下。
class PlanBadge : public QWidget
{
public:
    explicit PlanBadge(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_text(text)
    {
        QFont f = font();
        f.setPixelSize(11);
        setFont(f);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        return QSize(metrics.horizontalAdvance(m_text) + 20, metrics.height() + 7);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF capsule = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = capsule.height() / 2.0;
        QColor fill = theme::green();
        fill.setAlpha(24);
        QColor border = theme::green();
        border.setAlpha(70);
        painter.setBrush(fill);
        painter.setPen(QPen(border, 1.0));
        painter.drawRoundedRect(capsule, radius, radius);

        painter.setPen(theme::green());
        const QFontMetrics metrics(font());
        const QRect tight = metrics.tightBoundingRect(m_text);
        // 让字形墨迹的垂直中心落在胶囊垂直中心上。
        const qreal baseline = capsule.center().y() - (tight.top() + tight.bottom()) / 2.0;
        const qreal x = (width() - metrics.horizontalAdvance(m_text)) / 2.0;
        painter.drawText(QPointF(x, baseline), m_text);
    }

private:
    QString m_text;
};

} // namespace

QuotaCard::QuotaCard(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("Codex 用量"));
    setFixedWidth(320);

    // 内容放在单独的子控件里，展开/吸收时用透明度特效做整体淡入淡出。
    m_content = new QWidget(this);
    m_content->setAttribute(Qt::WA_TranslucentBackground);
    m_content->setAttribute(Qt::WA_NoSystemBackground);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_content);
    // 允许窗口在展开动画期间小于内容的最小尺寸。
    outer->setSizeConstraint(QLayout::SetNoConstraint);

    m_contentOpacity = new QGraphicsOpacityEffect(m_content);
    m_contentOpacity->setOpacity(1.0);
    m_content->setGraphicsEffect(m_contentOpacity);

    installEventFilter(this);
    rebuildUi();
}

void QuotaCard::setSnapshot(const RateLimitSnapshot &snapshot)
{
    const auto sameWindow = [](const RateLimitWindow &left, const RateLimitWindow &right) {
        return left.valid == right.valid && left.usedPercent == right.usedPercent
                && left.windowDurationMins == right.windowDurationMins
                && left.resetsAt == right.resetsAt;
    };
    const bool unchanged = m_snapshot.valid == snapshot.valid
            && sameWindow(m_snapshot.primary, snapshot.primary)
            && sameWindow(m_snapshot.secondary, snapshot.secondary)
            && m_snapshot.planType == snapshot.planType
            && m_snapshot.resetCreditsAvailable == snapshot.resetCreditsAvailable
            && nearestResetCreditExpiry(m_snapshot) == nearestResetCreditExpiry(snapshot);
    if (unchanged && m_snapshot.readAtMs > 0)
        return;
    m_snapshot = snapshot;
    rebuildUi();
    if (isVisible() && !m_opening && !m_closing) {
        setUpdatesEnabled(false);
        adjustSize();
        keepInsideScreen(this);
        setUpdatesEnabled(true);
        update();
    }
}

void QuotaCard::setAccount(const AccountProfile &account)
{
    const bool same = m_account.valid == account.valid
            && m_account.name == account.name
            && m_account.email == account.email
            && m_account.avatar.cacheKey() == account.avatar.cacheKey();
    if (same)
        return;
    m_account = account;
    rebuildUi();
    if (isVisible() && !m_opening && !m_closing) {
        setUpdatesEnabled(false);
        adjustSize();
        keepInsideScreen(this);
        setUpdatesEnabled(true);
        update();
    }
}

void QuotaCard::setPulseActive(bool active)
{
    if (m_pulseActive == active)
        return;
    m_pulseActive = active;
    for (PillProgress *bar : findChildren<PillProgress *>())
        bar->setPulseActive(active);
}

void QuotaCard::setBackgroundOpacity(int percent)
{
    const int value = qBound(30, percent, 100);
    if (m_opacityPercent == value)
        return;
    m_opacityPercent = value;
    update();
}

void QuotaCard::setDisplayRemaining(bool remaining)
{
    if (m_displayRemaining == remaining)
        return;
    m_displayRemaining = remaining;
    rebuildUi();
    if (isVisible() && !m_opening && !m_closing) {
        setUpdatesEnabled(false);
        adjustSize();
        keepInsideScreen(this);
        setUpdatesEnabled(true);
        update();
    }
}

void QuotaCard::setWatchedCount(int count, int runningCount, int failedCount)
{
    const int newWatched = qMax(0, count);
    const int newRunning = qMax(0, runningCount);
    const int newFailed = qMax(0, failedCount);
    if (m_watched == newWatched && m_running == newRunning && m_failed == newFailed)
        return;
    m_watched = newWatched;
    m_running = newRunning;
    m_failed = newFailed;
    m_pulseActive = m_running > 0;
    rebuildUi();
    if (isVisible() && !m_opening && !m_closing) {
        setUpdatesEnabled(false);
        adjustSize();
        keepInsideScreen(this);
        setUpdatesEnabled(true);
        update();
    }
}

void QuotaCard::applyThemeStyles()
{
    // 弹卡的文字颜色是在 rebuildUi 时按当前主题写进 stylesheet 的，
    // 主题切换时只改全局 palette 不会生效，必须重建一次。
    rebuildUi();
    if (isVisible() && !m_opening && !m_closing) {
        setUpdatesEnabled(false);
        adjustSize();
        keepInsideScreen(this);
        setUpdatesEnabled(true);
    }
    update();
}

void QuotaCard::setReveal(qreal value)
{
    m_reveal = qBound<qreal>(0.0, value, 1.0);
    if (m_contentOpacity)
        m_contentOpacity->setOpacity(m_reveal);
    update();
}

QRect QuotaCard::anchorRect() const
{
    return smallAnchorRect(m_anchor);
}

void QuotaCard::stopAnimation()
{
    if (!m_animation)
        return;
    m_animation->stop();
    m_animation->deleteLater();
    m_animation = nullptr;
}

void QuotaCard::startOpenAnimation(const QRect &target)
{
    stopAnimation();
    m_opening = true;
    m_closing = false;
    setReveal(0.0);

    // 放开尺寸约束，允许窗口从悬浮条上的小矩形长到最终大小。
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    const QRect startRect = smallAnchorRect(m_anchor);
    setGeometry(startRect);
    show();
    raise();
    activateWindow();
    setFocus(Qt::PopupFocusReason);

    auto *group = new QParallelAnimationGroup(this);
    auto *geometryAnimation = new QPropertyAnimation(this, "geometry", group);
    geometryAnimation->setDuration(200);
    geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
    geometryAnimation->setStartValue(startRect);
    geometryAnimation->setEndValue(target);

    auto *revealAnimation = new QPropertyAnimation(this, "reveal", group);
    revealAnimation->setDuration(170);
    revealAnimation->setEasingCurve(QEasingCurve::OutCubic);
    revealAnimation->setStartValue(0.0);
    revealAnimation->setEndValue(1.0);

    m_animation = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        m_animation = nullptr;
        m_opening = false;
        setFixedWidth(320);
        setReveal(1.0);
    });
    group->start(QAbstractAnimation::KeepWhenStopped);
}

void QuotaCard::startCloseAnimation(const QPoint &anchor)
{
    if (!isVisible() || m_closing)
        return;
    stopAnimation();
    m_opening = false;
    m_closing = true;

    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    const QRect startRect = geometry();
    const QRect targetRect = smallAnchorRect(anchor);

    auto *group = new QParallelAnimationGroup(this);
    auto *geometryAnimation = new QPropertyAnimation(this, "geometry", group);
    geometryAnimation->setDuration(180);
    geometryAnimation->setEasingCurve(QEasingCurve::InCubic);
    geometryAnimation->setStartValue(startRect);
    geometryAnimation->setEndValue(targetRect);

    auto *revealAnimation = new QPropertyAnimation(this, "reveal", group);
    revealAnimation->setDuration(150);
    revealAnimation->setEasingCurve(QEasingCurve::InCubic);
    revealAnimation->setStartValue(m_reveal);
    revealAnimation->setEndValue(0.0);

    m_animation = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        m_animation = nullptr;
        m_closing = false;
        hide();
        setFixedWidth(320);
        setReveal(1.0);
    });
    group->start(QAbstractAnimation::KeepWhenStopped);
}

void QuotaCard::dismissAnimated()
{
    if (!isVisible())
        return;
    startCloseAnimation(m_anchor);
}

void QuotaCard::popupNear(const QPoint &globalPos)
{
    m_anchor = globalPos;
    if (layout())
        layout()->activate();
    adjustSize();

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    const int gap = 12;
    const int margin = 8;
    const QSize cardSize = size();

    // 根据锚点离哪条屏幕边最近，决定卡片弹出方向。
    const int distanceLeft = globalPos.x() - available.left();
    const int distanceRight = available.right() - globalPos.x();
    const int distanceTop = globalPos.y() - available.top();
    const int distanceBottom = available.bottom() - globalPos.y();
    const int nearest = qMin(qMin(distanceLeft, distanceRight), qMin(distanceTop, distanceBottom));

    int x = 0;
    int y = 0;
    if (nearest == distanceRight) {
        x = globalPos.x() - cardSize.width() - gap;
        y = globalPos.y() - cardSize.height() / 2;
    } else if (nearest == distanceLeft) {
        x = globalPos.x() + gap;
        y = globalPos.y() - cardSize.height() / 2;
    } else if (nearest == distanceTop) {
        x = globalPos.x() - cardSize.width() / 2;
        y = globalPos.y() + gap;
    } else {
        x = globalPos.x() - cardSize.width() / 2;
        y = globalPos.y() - cardSize.height() - gap;
    }

    const int maxX = qMax(available.left() + margin, available.right() - cardSize.width() + 1 - margin);
    const int maxY = qMax(available.top() + margin, available.bottom() - cardSize.height() + 1 - margin);
    x = qBound(available.left() + margin, x, maxX);
    y = qBound(available.top() + margin, y, maxY);

    startOpenAnimation(QRect(x, y, cardSize.width(), cardSize.height()));
}

void QuotaCard::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = qMin<qreal>(18.0, qMin(width(), height()) / 2.0);
    const QRectF cardRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(cardRect, radius, radius);

    QColor glass = theme::surface();
    glass.setAlphaF((m_opacityPercent / 100.0) * m_reveal);
    painter.fillPath(path, glass);

    QColor borderColor = theme::border();
    borderColor.setAlphaF(m_reveal);
    painter.setPen(QPen(borderColor, 1.0));
    painter.drawPath(path);
}

void QuotaCard::mousePressEvent(QMouseEvent *event)
{
    // 点击卡片内部不穿透到悬浮条，也不触发关闭。
    event->accept();
}

bool QuotaCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this) {
        switch (event->type()) {
        case QEvent::KeyPress: {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                dismissAnimated();
                return true;
            }
            break;
        }
        case QEvent::WindowDeactivate:
        case QEvent::ApplicationDeactivate:
            dismissAnimated();
            break;
        case QEvent::Leave: {
            // 鼠标离开卡片后自动收起；若仍在卡片/子控件内则保留。
            // 展开动画期间鼠标可能还在悬浮条上，此时不安排自动收起。
            QTimer::singleShot(350, this, [this]() {
                if (!isVisible() || m_opening || m_closing)
                    return;
                const QPoint global = QCursor::pos();
                if (rect().contains(mapFromGlobal(global)))
                    return;
                dismissAnimated();
            });
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QuotaCard::rebuildUi()
{
    auto *root = qobject_cast<QVBoxLayout *>(m_content ? m_content->layout() : nullptr);
    if (!root) {
        root = new QVBoxLayout(m_content);
        root->setContentsMargins(20, 18, 20, 16);
        root->setSpacing(10);
        root->setSizeConstraint(QLayout::SetNoConstraint);
    }
    clearLayout(root);

    auto makeLabel = [this](const QString &text, const QString &styleSheet) {
        auto *label = new QLabel(text, m_content);
        label->setTextFormat(Qt::PlainText);
        label->setStyleSheet(styleSheet);
        return label;
    };
    auto tinted = [](const QColor &color, int alpha) {
        QColor tintedColor = color;
        tintedColor.setAlpha(alpha);
        return theme::rgba(tintedColor);
    };

    // 标题行：当前用户头像 + 昵称（未取到资料时回退“Codex 用量”）
    auto *titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);
    const QString accountName = m_account.valid && !m_account.name.trimmed().isEmpty()
            ? m_account.name.trimmed()
            : QStringLiteral("Codex 用量");
    if (m_account.valid) {
        const int avatarSize = 22;
        auto *avatar = new QLabel(m_content);
        avatar->setFixedSize(avatarSize, avatarSize);
        avatar->setPixmap(arc::avatar::pixmapFor(m_account, avatarSize, devicePixelRatioF()));
        avatar->setAttribute(Qt::WA_TransparentForMouseEvents);
        if (!m_account.email.isEmpty())
            avatar->setToolTip(m_account.email);
        titleRow->addWidget(avatar);
    }
    auto *title = makeLabel(accountName,
                            QStringLiteral("color: %1; font-size: 16px; font-weight: bold;")
                                    .arg(theme::text().name()));
    if (!m_account.email.isEmpty())
        title->setToolTip(m_account.email);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    QString planText = QStringLiteral("未连接");
    if (m_snapshot.valid) {
        planText = m_snapshot.planType.trimmed();
        if (planText.isEmpty())
            planText = QStringLiteral("未知套餐");
    }
    auto *planBadge = new PlanBadge(planText, m_content);
    titleRow->addWidget(planBadge, 0, Qt::AlignVCenter);
    root->addLayout(titleRow);

    auto *divider = new QFrame(m_content);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(theme::border().name()));
    root->addWidget(divider);

    if (!m_snapshot.valid) {
        auto *empty = makeLabel(QStringLiteral("尚未获取到用量数据，等待 Codex app-server 响应…"),
                                QStringLiteral("color: %1; font-size: 12px;").arg(theme::textMuted().name()));
        empty->setWordWrap(true);
        root->addWidget(empty);
    } else {
        auto addWindowSection = [&](const RateLimitWindow &window, const QString &fallbackName) {
            const bool valid = window.valid;
            const int usedPercent = qBound(0, window.usedPercent, 100);
            const QColor accent = valid ? theme::quotaColor(usedPercent) : theme::textDim();
            const int displayPercent = valid && m_displayRemaining
                    ? qBound(0, 100 - usedPercent, 100)
                    : (valid ? usedPercent : 0);

            root->addWidget(makeLabel(windowCaption(window, fallbackName),
                                      QStringLiteral("color: %1; font-size: 13px; font-weight: 600;")
                                              .arg(theme::text().name())));

            auto *progress = new PillProgress(m_content);
            progress->setValue(displayPercent);
            progress->setTrackColor(theme::track());
            progress->setAccentColor(valid ? accent : theme::textDim());
            progress->setPulseActive(m_pulseActive);
            // 额度耗尽（剩余 0%）：整条进度条外圈常亮红色呼吸灯。
            progress->setExhausted(valid && usedPercent >= 100);
            root->addWidget(progress);

            auto *footerRow = new QHBoxLayout();
            footerRow->setSpacing(8);
            const QColor percentColor = (valid && usedPercent >= 100) ? theme::red()
                    : (valid ? accent : theme::textDim());
            auto *usedLabel = makeLabel(valid ? QStringLiteral("%1 %2%")
                                                        .arg(m_displayRemaining ? QStringLiteral("剩余")
                                                                                : QStringLiteral("已用"))
                                                        .arg(displayPercent)
                                              : QStringLiteral("暂无数据"),
                                        QStringLiteral("color: %1; font-size: 14px; font-weight: bold;")
                                                .arg(percentColor.name()));
            footerRow->addWidget(usedLabel);
            footerRow->addStretch(1);
            if (valid) {
                footerRow->addWidget(makeLabel(QStringLiteral("%1 重置").arg(formatEpochShort(window.resetsAt)),
                                               QStringLiteral("color: %1; font-size: 12px;")
                                                       .arg(theme::textDim().name())));
            }
            root->addLayout(footerRow);
            root->addSpacing(3);
        };

        // 通常 primary 是 5h 窗口、secondary 是每周窗口；若服务端顺序相反则纠正。
        const RateLimitWindow *fiveHour = &m_snapshot.primary;
        const RateLimitWindow *weekly = &m_snapshot.secondary;
        if (fiveHour->windowDurationMins == 10080 && weekly->windowDurationMins == 300) {
            fiveHour = &m_snapshot.secondary;
            weekly = &m_snapshot.primary;
        }
        addWindowSection(*fiveHour, QStringLiteral("5h 窗口"));
        addWindowSection(*weekly, QStringLiteral("每周窗口"));

        if (m_snapshot.resetCreditsAvailable > 0) {
            auto *creditsDivider = new QFrame(m_content);
            creditsDivider->setFixedHeight(1);
            creditsDivider->setStyleSheet(QStringLiteral("background: %1; border: none;")
                                                  .arg(theme::border().name()));
            root->addWidget(creditsDivider);
            auto *creditsRow = new QHBoxLayout();
            creditsRow->setSpacing(8);
            creditsRow->addWidget(makeLabel(QStringLiteral("重置卡"),
                                            QStringLiteral("color: %1; font-size: 13px; font-weight: 600;")
                                                    .arg(theme::text().name())));
            creditsRow->addStretch(1);
            // 重置卡剩余时间预警：最近一张 <2 天红、<7 天黄、>=7 天绿。
            const ResetCreditUrgency creditUrgency = resetCreditUrgency(m_snapshot);
            const QColor creditColor = theme::resetCreditColor(creditUrgency);
            auto *creditsLabel = makeLabel(QStringLiteral("可用 %1 张").arg(m_snapshot.resetCreditsAvailable),
                                           QStringLiteral("color: %1; font-size: 14px; font-weight: bold;")
                                                   .arg(creditColor.name()));
            const qint64 nearestCreditExpiry = nearestResetCreditExpiry(m_snapshot);
            if (nearestCreditExpiry > 0) {
                creditsLabel->setToolTip(QStringLiteral("最近一张重置卡到期：%1\n剩余 %2")
                                                 .arg(formatEpoch(nearestCreditExpiry),
                                                      formatCountdown(nearestCreditExpiry)));
            } else {
                creditsLabel->setToolTip(QStringLiteral("重置卡到期时间未知（服务端未返回明细）"));
            }
            creditsRow->addWidget(creditsLabel);
            root->addLayout(creditsRow);
        }
    }

    // 看守信息 + 刷新时间
    QString watchText = QStringLiteral("看守中 %1 个对话（%2 个进行中").arg(m_watched).arg(m_running);
    if (m_failed > 0)
        watchText += QStringLiteral("，%1 个续跑失败").arg(m_failed);
    watchText += QStringLiteral("）");
    const QColor watchColor = m_failed > 0 ? theme::red() : theme::textMuted();
    root->addWidget(makeLabel(watchText, QStringLiteral("color: %1; font-size: 12px;")
                                                 .arg(watchColor.name())));

    QString refreshText = QStringLiteral("尚未刷新");
    if (m_snapshot.readAtMs > 0) {
        refreshText = QStringLiteral("刷新于 %1").arg(
                QDateTime::fromMSecsSinceEpoch(m_snapshot.readAtMs).toString(QStringLiteral("MM-dd HH:mm:ss")));
    }
    root->addWidget(makeLabel(refreshText, QStringLiteral("color: %1; font-size: 10px;")
                                                  .arg(theme::textDim().name())));

    auto *hint = makeLabel(QStringLiteral("双击悬浮条或右键菜单打开主窗口"),
                           QStringLiteral("color: %1; font-size: 10px;")
                                   .arg(theme::textDim().name()));
    hint->setAlignment(Qt::AlignCenter);
    root->addWidget(hint);

    if (isVisible()) {
        showLayoutWidgets(root);
        root->activate();
    }
}

} // namespace arc