#include "QuotaWidget.h"

#include "DualRingWidget.h"
#include "QuotaCard.h"
#include "RingWidget.h"
#include "Theme.h"
#include "WatchBadge.h"

#include <QAbstractAnimation>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QGuiApplication>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <functional>

namespace arc {

namespace {

constexpr int kEdgeMargin = 8;      // 贴边时距离屏幕工作区的留白
constexpr int kSnapDistance = 40;   // 吸附阈值（像素）
constexpr qreal kBaseRingSize = 74.0;
constexpr qreal kBaseBarWidth = 100.0;
const QColor kDefaultAccent = theme::green();

void animateMove(QWidget *widget, const QPoint &target, const std::function<void()> &onFinished)
{
    if (widget->pos() == target) {
        if (onFinished)
            onFinished();
        return;
    }
    auto *animation = new QPropertyAnimation(widget, "pos", widget);
    animation->setDuration(220);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(widget->pos());
    animation->setEndValue(target);
    if (onFinished)
        QObject::connect(animation, &QPropertyAnimation::finished, widget, onFinished);
    // Qt6 没有 setDeleteWhenStopped()，用 start(policy) 指定动画结束后自动销毁。
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

int clampSize(int value, int minimum, int maximum)
{
    if (maximum < minimum)
        return minimum;
    return qBound(minimum, value, maximum);
}

} // namespace

QuotaWidget::QuotaWidget(QWidget *parent)
    : QWidget(parent)
{
    // 无边框、置顶、半透明工具窗：不占任务栏，可悬浮在 Codex 窗口之上。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setWindowTitle(QStringLiteral("AutoRun Codex 额度"));

    auto *primary = new RingWidget(this);
    primary->setObjectName(QStringLiteral("ringPrimary"));
    primary->setTitle(QStringLiteral("5h 窗口"));
    primary->setSubtitle(QStringLiteral("--"));
    primary->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *weekly = new RingWidget(this);
    weekly->setObjectName(QStringLiteral("ringWeekly"));
    weekly->setTitle(QStringLiteral("每周"));
    weekly->setSubtitle(QStringLiteral("--"));
    weekly->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_watchBadge = new WatchBadge(this);
    m_watchBadge->setObjectName(QStringLiteral("watchLabel"));

    auto makeStatLabel = [this](const QString &objectName, bool bold) {
        auto *label = new QLabel(this);
        label->setObjectName(objectName);
        label->setAlignment(Qt::AlignCenter);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont f = label->font();
        f.setBold(bold);
        label->setFont(f);
        label->setStyleSheet(QStringLiteral("color:%1;")
                                      .arg(bold ? theme::text().name() : theme::textMuted().name()));
        return label;
    };
    makeStatLabel(QStringLiteral("primaryPercent"), true);
    makeStatLabel(QStringLiteral("primarySubtitle"), false);
    makeStatLabel(QStringLiteral("weeklyPercent"), true);
    makeStatLabel(QStringLiteral("weeklySubtitle"), false);

    m_dualRing = new DualRingWidget(this);
    m_dualRing->setObjectName(QStringLiteral("dualRing"));
    m_dualRing->hide();

    auto makeMinimalLabel = [this](const QString &text, const QString &color) {
        auto *label = new QLabel(text, this);
        label->setAlignment(Qt::AlignCenter);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        label->hide();
        return label;
    };
    m_minFiveLabel = makeMinimalLabel(QStringLiteral("5h"), theme::text().name());
    m_minFiveTime = makeMinimalLabel(QStringLiteral("--"), theme::textMuted().name());
    m_minWeeklyLabel = makeMinimalLabel(QStringLiteral("每周用量"), theme::textMuted().name());
    m_minWeeklyTime = makeMinimalLabel(QStringLiteral("--"), theme::textDim().name());
    // 重置卡临期小点：不参与布局，由 updateCreditDot() 按样式手动定位。
    m_creditDot = new QLabel(this);
    m_creditDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_creditDot->hide();

    m_minimalTimer = new QTimer(this);
    m_minimalTimer->setInterval(3800);
    connect(m_minimalTimer, &QTimer::timeout, this, [this]() {
        m_minShowFive = !m_minShowFive;
        updateMinimalInfo();
    });

    setScalePercent(100);

    // 初次构造时默认贴在主屏右侧并垂直居中，MainWindow 之后会按设置重新贴边。
    const QRect available = availableScreenRect();
    move(available.right() - width() + 1 - kEdgeMargin,
         available.center().y() - height() / 2);
}

void QuotaWidget::setSnapshot(const RateLimitSnapshot &snapshot)
{
    m_snapshot = snapshot;

    // 通常 primary 是 5h 窗口、secondary 是每周窗口；若服务端顺序相反则纠正。
    const RateLimitWindow *fiveHour = &m_snapshot.primary;
    const RateLimitWindow *weekly = &m_snapshot.secondary;
    if (fiveHour->windowDurationMins == 10080 && weekly->windowDurationMins == 300) {
        fiveHour = &m_snapshot.secondary;
        weekly = &m_snapshot.primary;
    }

    auto *primaryRing = findChild<RingWidget *>(QStringLiteral("ringPrimary"));
    auto *weeklyRing = findChild<RingWidget *>(QStringLiteral("ringWeekly"));
    auto displayPercent = [this](int used) {
        return m_displayRemaining ? qBound(0, 100 - used, 100) : qBound(0, used, 100);
    };
    const bool referenceStyle = m_style == QStringLiteral("reference");
    auto updateRing = [&](RingWidget *ring, QLabel *percentLabel, QLabel *subtitleLabel,
                          bool valid, int percent, qint64 resetsAt, bool fiveHourWindow,
                          bool exhausted) {
        if (ring) {
            if (referenceStyle)
                ring->setTitle(fiveHourWindow ? QStringLiteral("5h") : QStringLiteral("周"));
            else
                ring->setTitle(fiveHourWindow ? QStringLiteral("5h 窗口") : QStringLiteral("每周"));
            ring->setRemainingMode(m_displayRemaining);
            ring->setPercent(valid ? percent : 0);
            ring->setPercentTextVisible(!referenceStyle);
            ring->setSubtitle(referenceStyle ? QString()
                                             : (valid ? formatCountdown(resetsAt) : QStringLiteral("--")));
            ring->setTitleEmphasis(referenceStyle);
            ring->setAccentColor(valid ? kDefaultAccent : theme::textDim());
            ring->setExhausted(valid && exhausted);
        }
        if (percentLabel)
            percentLabel->setText(valid ? QStringLiteral("%1%").arg(percent) : QStringLiteral("--"));
        if (subtitleLabel)
            subtitleLabel->setText(valid ? formatCountdown(resetsAt) : QStringLiteral("--"));
    };
    const bool fiveHourValid = m_snapshot.valid && fiveHour->valid;
    const bool weeklyValid = m_snapshot.valid && weekly->valid;
    updateRing(primaryRing, findChild<QLabel *>(QStringLiteral("primaryPercent")),
               findChild<QLabel *>(QStringLiteral("primarySubtitle")),
               fiveHourValid, displayPercent(fiveHour->usedPercent),
               fiveHour->resetsAt, true, fiveHour->usedPercent >= 100);
    updateRing(weeklyRing, findChild<QLabel *>(QStringLiteral("weeklyPercent")),
               findChild<QLabel *>(QStringLiteral("weeklySubtitle")),
               weeklyValid, displayPercent(weekly->usedPercent),
               weekly->resetsAt, false, weekly->usedPercent >= 100);
    if (m_dualRing) {
        m_dualRing->setRemainingMode(m_displayRemaining);
        m_dualRing->setData(fiveHour->usedPercent, weekly->usedPercent, fiveHourValid, weeklyValid,
                            fiveHour->resetsAt, weekly->resetsAt,
                            fiveHour->windowDurationMins, weekly->windowDurationMins);
    }
    updateMinimalInfo();
    updateCreditDot();

    QStringList tooltip;
    if (!m_snapshot.valid) {
        tooltip << QStringLiteral("尚未获取到 Codex 用量数据");
    } else {
        if (!m_snapshot.planType.trimmed().isEmpty())
            tooltip << QStringLiteral("套餐：%1").arg(m_snapshot.planType.trimmed());
        if (fiveHour->valid) {
            tooltip << QStringLiteral("5h 窗口：%1 %2%，重置 %3")
                               .arg(m_displayRemaining ? QStringLiteral("剩余") : QStringLiteral("已用"))
                               .arg(displayPercent(fiveHour->usedPercent))
                               .arg(formatEpoch(fiveHour->resetsAt));
        }
        if (weekly->valid) {
            tooltip << QStringLiteral("每周：%1 %2%，重置 %3")
                               .arg(m_displayRemaining ? QStringLiteral("剩余") : QStringLiteral("已用"))
                               .arg(displayPercent(weekly->usedPercent))
                               .arg(formatEpoch(weekly->resetsAt));
        }
        if (m_snapshot.resetCreditsAvailable > 0) {
            QString creditLine = QStringLiteral("可用重置卡：%1 张").arg(m_snapshot.resetCreditsAvailable);
            const qint64 nearestCreditExpiry = nearestResetCreditExpiry(m_snapshot);
            if (nearestCreditExpiry > 0) {
                creditLine += QStringLiteral("（最近一张 %1 到期，剩 %2）")
                                      .arg(formatEpochShort(nearestCreditExpiry),
                                           formatCountdown(nearestCreditExpiry));
            }
            tooltip << creditLine;
        }
    }
    tooltip << QStringLiteral("双击打开主窗口 · 右键更多操作");
    setToolTip(tooltip.join(QStringLiteral("\n")));

    // 卡片可见时不重建 UI（重建会闪）；下次打开前 showCard() 会同步最新数据。
    if (m_card && !m_card->isVisible()) {
        m_card->setBackgroundOpacity(m_opacityPercent);
        m_card->setDisplayRemaining(m_displayRemaining);
        m_card->setSnapshot(m_snapshot);
        m_card->setWatchedCount(m_watched, m_running);
    }
}

void QuotaWidget::setAccountProfile(const AccountProfile &account)
{
    const bool same = m_account.valid == account.valid
            && m_account.name == account.name
            && m_account.email == account.email
            && m_account.avatar.cacheKey() == account.avatar.cacheKey();
    if (same)
        return;
    m_account = account;
    if (m_card)
        m_card->setAccount(m_account);
}

void QuotaWidget::setDisplayRemaining(bool remaining)
{
    if (m_displayRemaining == remaining)
        return;
    m_displayRemaining = remaining;
    setSnapshot(m_snapshot);
}

void QuotaWidget::setWatchedCount(int count, int runningCount, int failedCount)
{
    m_watched = qMax(0, count);
    m_running = qMax(0, runningCount);
    m_failed = qMax(0, failedCount);

    if (m_watchBadge) {
        m_watchBadge->setWatchedCount(m_watched);
        m_watchBadge->setFailedCount(m_failed);
    }
    const bool pulsed = m_running > 0;
    if (auto *primaryRing = findChild<RingWidget *>(QStringLiteral("ringPrimary")))
        primaryRing->setPulseActive(pulsed);
    if (auto *weeklyRing = findChild<RingWidget *>(QStringLiteral("ringWeekly")))
        weeklyRing->setPulseActive(pulsed);
    if (m_dualRing)
        m_dualRing->setPulseActive(pulsed);
    // 卡片可见时冻结更新，避免重建/闪烁；打开时 showCard() 会同步。
    if (m_card && !m_card->isVisible())
        m_card->setWatchedCount(m_watched, m_running, m_failed);
}

void QuotaWidget::setAlwaysOnTopHintEnabled(bool on)
{
    if (m_alwaysOnTop == on)
        return;
    m_alwaysOnTop = on;
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (wasVisible)
        show();
}

void QuotaWidget::setBackgroundOpacity(int percent)
{
    const int value = qBound(30, percent, 100);
    if (m_opacityPercent == value)
        return;
    m_opacityPercent = value;
    if (m_card)
        m_card->setBackgroundOpacity(m_opacityPercent);
    update();
}

void QuotaWidget::applyThemeStyles()
{
    const QColor text = theme::text();
    const QColor muted = theme::textMuted();
    const QColor dim = theme::textDim();
    const QString textStyle = QStringLiteral("color:%1;").arg(text.name());
    const QString mutedBoldStyle = QStringLiteral("color:%1;font-weight:bold;").arg(text.name());
    const QString mutedStyle = QStringLiteral("color:%1;").arg(muted.name());
    const QString dimStyle = QStringLiteral("color:%1;").arg(dim.name());

    if (auto *label = findChild<QLabel *>(QStringLiteral("primaryPercent")))
        label->setStyleSheet(mutedBoldStyle);
    if (auto *label = findChild<QLabel *>(QStringLiteral("weeklyPercent")))
        label->setStyleSheet(mutedBoldStyle);
    if (auto *label = findChild<QLabel *>(QStringLiteral("primarySubtitle")))
        label->setStyleSheet(mutedStyle);
    if (auto *label = findChild<QLabel *>(QStringLiteral("weeklySubtitle")))
        label->setStyleSheet(dimStyle);
    if (m_minFiveLabel)
        m_minFiveLabel->setStyleSheet(textStyle);
    if (m_minFiveTime)
        m_minFiveTime->setStyleSheet(mutedStyle);
    if (m_minWeeklyLabel)
        m_minWeeklyLabel->setStyleSheet(mutedStyle);
    if (m_minWeeklyTime)
        m_minWeeklyTime->setStyleSheet(dimStyle);

    updateCreditDot();

    if (m_card) {
        m_card->setBackgroundOpacity(m_opacityPercent);
        // 弹卡的文字颜色缓存在内部控件的 stylesheet 里，必须显式重建才能跟上主题。
        m_card->applyThemeStyles();
        m_card->setWatchedCount(m_watched, m_running, m_failed);
    }

    // 自绘控件（环、徽标）缓存的是自绘颜色而不是 palette，主题切换后要主动重绘。
    for (QWidget *child : findChildren<QWidget *>())
        child->update();
    update();
}

void QuotaWidget::setFloatingStyle(const QString &style)
{
    const QString requested = style.trimmed().toLower();
    const QString normalized = requested == QStringLiteral("classic") ? QStringLiteral("classic")
            : requested == QStringLiteral("minimal") ? QStringLiteral("minimal")
                                                     : QStringLiteral("reference");
    if (m_style == normalized)
        return;
    m_style = normalized;
    if (m_minimalTimer) {
        if (m_style == QStringLiteral("minimal")) {
            m_minShowFive = true;
            m_minimalTimer->start();
        } else {
            m_minimalTimer->stop();
        }
    }
    setSnapshot(m_snapshot);
    updateMinimalInfo();
    setScalePercent(m_scale);
}

bool QuotaWidget::creditDotVisible() const
{
    return resetCreditUrgency(m_snapshot) == ResetCreditUrgency::Critical;
}

void QuotaWidget::updateCreditDot()
{
    if (!m_creditDot)
        return;
    const bool minimal = m_style == QStringLiteral("minimal");
    if (!creditDotVisible() || minimal) {
        // 极简样式的橙点直接拼进“周 · 剩余 x%”文字，不用独立小点。
        m_creditDot->hide();
        return;
    }

    const qreal scale = m_scale / 100.0;
    const int dot = qMax(5, qRound(6.0 * scale));
    m_creditDot->setFixedSize(dot, dot);
    m_creditDot->setStyleSheet(QStringLiteral("background:%1;border-radius:%2px;")
                                       .arg(theme::warningDot().name())
                                       .arg(dot / 2));

    if (m_style == QStringLiteral("reference")) {
        // 参考样式：紧贴“周 33%”百分比文字右侧。
        auto *percentLabel = findChild<QLabel *>(QStringLiteral("weeklyPercent"));
        if (!percentLabel || !percentLabel->isVisible()) {
            m_creditDot->hide();
            return;
        }
        const QFontMetrics metrics(percentLabel->font());
        const int textWidth = metrics.horizontalAdvance(percentLabel->text());
        // 用户口径：每次都按最初 3px 的百分比计算。最新一次“+70%” → 3px × 1.7 ≈ 5px @100%。
        // 多轮反馈记录见 HANDOFF/CONVERSATION_HANDOFF。
        int x = percentLabel->geometry().center().x() + textWidth / 2
                + qMax(2, qRound(3.0 * scale * 1.7));
        // 保证小点不超出悬浮窗右边界（极端宽度如 “100%” + 大缩放时贴边收缩）。
        x = qMin(x, width() - dot - 2);
        const int y = percentLabel->geometry().center().y() - dot / 2;
        m_creditDot->move(x, y);
    } else {
        // 经典样式：百分比在圆环内部，橙点贴在周环右上角外沿。
        QWidget *ring = findChild<RingWidget *>(QStringLiteral("ringWeekly"));
        if (!ring || !ring->isVisible()) {
            m_creditDot->hide();
            return;
        }
        const QRect ringRect = ring->geometry();
        m_creditDot->move(ringRect.right() + 1 - dot / 2, ringRect.top() - dot / 2 + 2);
    }
    m_creditDot->show();
    m_creditDot->raise();
}

void QuotaWidget::updateMinimalInfo()
{
    if (!m_minFiveLabel || !m_minFiveTime)
        return;
    const RateLimitWindow *fiveHour = &m_snapshot.primary;
    const RateLimitWindow *weekly = &m_snapshot.secondary;
    if (fiveHour->windowDurationMins == 10080 && weekly->windowDurationMins == 300) {
        const RateLimitWindow *tmp = fiveHour;
        fiveHour = weekly;
        weekly = tmp;
    }
    const bool showFive = m_minShowFive;
    const RateLimitWindow *window = showFive ? fiveHour : weekly;
    const QString name = showFive ? QStringLiteral("5h") : QStringLiteral("周");
    const bool valid = m_snapshot.valid && window->valid;
    if (!valid) {
        m_minFiveLabel->setText(name);
        m_minFiveTime->setText(QStringLiteral("--"));
        return;
    }
    const int displayPercent = m_displayRemaining
            ? qBound(0, 100 - window->usedPercent, 100)
            : qBound(0, window->usedPercent, 100);
    QString minimalText = QStringLiteral("%1 · %2 %3%")
                                 .arg(name)
                                 .arg(m_displayRemaining ? QStringLiteral("剩余")
                                                         : QStringLiteral("已用"))
                                 .arg(displayPercent);
    // 极简模式没有独立的周百分比标签：显示“周”时把橙点拼在百分比后面。
    if (!showFive && creditDotVisible()) {
        minimalText += QStringLiteral(" <span style=\"color:%1;\">●</span>")
                               .arg(theme::warningDot().name());
        m_minFiveLabel->setTextFormat(Qt::RichText);
    } else {
        m_minFiveLabel->setTextFormat(Qt::PlainText);
    }
    m_minFiveLabel->setText(minimalText);
    m_minFiveTime->setText(QStringLiteral("%1后刷新").arg(formatCountdown(window->resetsAt)));
}

void QuotaWidget::setScalePercent(int percent)
{
    m_scale = qBound(50, percent, 150);
    const qreal scale = m_scale / 100.0;
    const bool minimal = m_style == QStringLiteral("minimal");
    const bool reference = !minimal && m_style == QStringLiteral("reference");

    int barWidth = 0;
    int ringSize = 0;
    int padding = 0;
    int gap = 0;
    int bottomPadding = 0;
    int labelHeight = 0;
    int percentHeight = 0;
    int subtitleHeight = 0;
    int valueHeight = 0;
    int timeHeight = 0;

    // 标签高度按实际字体行高兜底：HarmonyOS 等字体的 ascent/descent 比固定高度大时，
    // 若沿用固定的 percentHeight/subtitleHeight，QLabel 会把百分比数字上下裁掉。
    auto fontLineHeight = [this](int pixelSize, bool bold) {
        QFont f = font();
        f.setPixelSize(qMax(8, pixelSize));
        f.setBold(bold);
        return QFontMetrics(f).height();
    };

    if (minimal) {
        ringSize = qMax(56, qRound(70 * scale));
        barWidth = qMax(78, qRound(92 * scale));
        padding = qMax(4, qRound(7 * scale));
        gap = qMax(3, qRound(4 * scale));
        valueHeight = qMax(qRound(16 * scale), fontLineHeight(qRound(11 * scale), true) + 2);
        timeHeight = qMax(qRound(13 * scale), fontLineHeight(qRound(10 * scale), false) + 2);
        bottomPadding = qMax(6, qRound(9 * scale));
        // 看守徽标行高也随缩放一起变，不再用固定下限把悬浮条撑住。
        labelHeight = qMax(16, qRound(32 * scale));
    } else if (reference) {
        ringSize = qMax(44, qRound(64 * scale));
        barWidth = qMax(64, qRound(88 * scale));
        padding = qMax(5, qRound(9 * scale));
        gap = qMax(5, qRound(9 * scale));
        percentHeight = qMax(qRound(24 * scale), fontLineHeight(qRound(17 * scale), true) + 2);
        subtitleHeight = qMax(qRound(15 * scale), fontLineHeight(qRound(11 * scale), false) + 2);
        bottomPadding = qMax(7, qRound(10 * scale));
        // 看守徽标行高也随缩放一起变，不再用固定下限把悬浮条撑住。
        labelHeight = qMax(17, qRound(34 * scale));
    } else {
        barWidth = qMax(56, qRound(kBaseBarWidth * scale));
        ringSize = qMax(40, qRound(kBaseRingSize * scale));
        padding = qMax(4, qRound(10 * scale));
        gap = qMax(3, qRound(9 * scale));
        bottomPadding = qMax(7, qRound(12 * scale));
        // 看守徽标行高也随缩放一起变，不再用固定下限把悬浮条撑住。
        labelHeight = qMax(17, qRound(34 * scale));
    }

    auto *primaryRing = findChild<RingWidget *>(QStringLiteral("ringPrimary"));
    auto *weeklyRing = findChild<RingWidget *>(QStringLiteral("ringWeekly"));
    auto *watchBadge = m_watchBadge;

    int totalHeight = 0;
    if (minimal) {
        if (m_dualRing) {
            m_dualRing->setFixedSize(ringSize, ringSize);
            m_dualRing->move((barWidth - ringSize) / 2, padding);
            m_dualRing->show();
        }
        if (primaryRing)
            primaryRing->hide();
        if (weeklyRing)
            weeklyRing->hide();
        for (const char *name : {"primaryPercent", "primarySubtitle", "weeklyPercent", "weeklySubtitle"}) {
            if (auto *label = findChild<QLabel *>(QString::fromLatin1(name)))
                label->hide();
        }
        if (m_minWeeklyLabel)
            m_minWeeklyLabel->hide();
        if (m_minWeeklyTime)
            m_minWeeklyTime->hide();
        auto placeMinimalLabel = [&](QLabel *label, int top, int height, int pixelSize, bool bold) {
            if (!label)
                return;
            QFont font = label->font();
            font.setPixelSize(qMax(8, pixelSize));
            font.setBold(bold);
            label->setFont(font);
            label->setGeometry(0, top, barWidth, height);
            label->show();
        };
        int cursorY = padding + ringSize + gap;
        placeMinimalLabel(m_minFiveLabel, cursorY, valueHeight, qRound(11 * scale), true);
        cursorY += valueHeight;
        placeMinimalLabel(m_minFiveTime, cursorY, timeHeight, qRound(10 * scale), false);
        cursorY += timeHeight;
        totalHeight = cursorY + gap + labelHeight + bottomPadding;
    } else {
        if (m_dualRing)
            m_dualRing->hide();
        for (QLabel *label : {m_minFiveLabel, m_minFiveTime, m_minWeeklyLabel, m_minWeeklyTime}) {
            if (label)
                label->hide();
        }
        auto placeLabel = [&](const QString &objectName, int top, int height, int pixelSize, bool bold) {
            auto *label = findChild<QLabel *>(objectName);
            if (!label)
                return;
            QFont font = label->font();
            font.setPixelSize(qMax(8, pixelSize));
            font.setBold(bold);
            label->setFont(font);
            label->setVisible(reference);
            label->setGeometry(0, top, barWidth, height);
        };

        int cursorY = padding;
        if (primaryRing) {
            primaryRing->setFixedSize(ringSize, ringSize);
            primaryRing->move((barWidth - ringSize) / 2, cursorY);
        }
        cursorY += ringSize;
        placeLabel(QStringLiteral("primaryPercent"), cursorY, percentHeight, qRound(17 * scale), true);
        cursorY += percentHeight;
        placeLabel(QStringLiteral("primarySubtitle"), cursorY, subtitleHeight, qRound(11 * scale), false);
        cursorY += subtitleHeight + gap;

        if (weeklyRing) {
            weeklyRing->setFixedSize(ringSize, ringSize);
            weeklyRing->move((barWidth - ringSize) / 2, cursorY);
        }
        cursorY += ringSize;
        placeLabel(QStringLiteral("weeklyPercent"), cursorY, percentHeight, qRound(17 * scale), true);
        cursorY += percentHeight;
        placeLabel(QStringLiteral("weeklySubtitle"), cursorY, subtitleHeight, qRound(11 * scale), false);
        cursorY += subtitleHeight;

        totalHeight = reference
                ? cursorY + gap + labelHeight + bottomPadding
                : padding + ringSize + gap + ringSize + gap + labelHeight + bottomPadding;

        if (!reference) {
            for (const char *name : {"primaryPercent", "primarySubtitle", "weeklyPercent", "weeklySubtitle"}) {
                if (auto *label = findChild<QLabel *>(QString::fromLatin1(name)))
                    label->setVisible(false);
            }
        }
    }

    if (watchBadge) {
        // 徽标内部（文字/圆点/呼吸光晕/涟漪）按同一缩放比例绘制。
        watchBadge->setScalePercent(m_scale);
        watchBadge->setGeometry(0, totalHeight - bottomPadding - labelHeight, barWidth, labelHeight);
    }

    setFixedSize(barWidth, totalHeight);

    // 缩放后保持原来的贴边位置。
    const QRect available = availableScreenRect();
    const int maxLeft = qMax(available.left() + kEdgeMargin, available.right() - width() + 1 - kEdgeMargin);
    const int maxTop = qMax(available.top() + kEdgeMargin, available.bottom() - height() + 1 - kEdgeMargin);
    // 注意：局部变量不能命名为 x/y，否则会遮蔽 QWidget::x()/y()。
    int newX = this->x();
    int newY = this->y();
    if (m_edge == QStringLiteral("left")) {
        newX = available.left() + kEdgeMargin;
        newY = clampSize(newY, available.top() + kEdgeMargin, maxTop);
    } else if (m_edge == QStringLiteral("right")) {
        newX = available.right() - width() + 1 - kEdgeMargin;
        newY = clampSize(newY, available.top() + kEdgeMargin, maxTop);
    } else if (m_edge == QStringLiteral("top")) {
        newX = clampSize(available.center().x() - width() / 2, available.left() + kEdgeMargin, maxLeft);
        newY = available.top() + kEdgeMargin;
    } else if (m_edge == QStringLiteral("bottom")) {
        newX = clampSize(available.center().x() - width() / 2, available.left() + kEdgeMargin, maxLeft);
        newY = available.bottom() - height() + 1 - kEdgeMargin;
    } else {
        newX = clampSize(newX, available.left() + kEdgeMargin, maxLeft);
        newY = clampSize(newY, available.top() + kEdgeMargin, maxTop);
    }
    move(newX, newY);
    updateCreditDot();
}

void QuotaWidget::applyDockEdge(const QString &edge)
{
    QString normalized = edge.trimmed().toLower();
    if (normalized.isEmpty())
        normalized = QStringLiteral("auto");
    m_edge = normalized;

    const QRect available = availableScreenRect();
    const int maxLeft = qMax(available.left() + kEdgeMargin, available.right() - width() + 1 - kEdgeMargin);
    const int maxTop = qMax(available.top() + kEdgeMargin, available.bottom() - height() + 1 - kEdgeMargin);

    int targetX = x();
    int targetY = y();
    if (m_edge == QStringLiteral("left")) {
        targetX = available.left() + kEdgeMargin;
        targetY = clampSize(y(), available.top() + kEdgeMargin, maxTop);
    } else if (m_edge == QStringLiteral("right")) {
        targetX = available.right() - width() + 1 - kEdgeMargin;
        targetY = clampSize(y(), available.top() + kEdgeMargin, maxTop);
    } else if (m_edge == QStringLiteral("top")) {
        targetX = clampSize(available.center().x() - width() / 2, available.left() + kEdgeMargin, maxLeft);
        targetY = available.top() + kEdgeMargin;
    } else if (m_edge == QStringLiteral("bottom")) {
        targetX = clampSize(available.center().x() - width() / 2, available.left() + kEdgeMargin, maxLeft);
        targetY = available.bottom() - height() + 1 - kEdgeMargin;
    } else {
        // auto：吸附到当前距离最近的屏幕边缘。
        const int distanceLeft = geometry().left() - available.left();
        const int distanceRight = available.right() - geometry().right();
        const int distanceTop = geometry().top() - available.top();
        const int distanceBottom = available.bottom() - geometry().bottom();
        int nearest = distanceLeft;
        QString side = QStringLiteral("left");
        if (distanceRight < nearest) {
            nearest = distanceRight;
            side = QStringLiteral("right");
        }
        if (distanceTop < nearest) {
            nearest = distanceTop;
            side = QStringLiteral("top");
        }
        if (distanceBottom < nearest) {
            nearest = distanceBottom;
            side = QStringLiteral("bottom");
        }
        if (side == QStringLiteral("left")) {
            targetX = available.left() + kEdgeMargin;
            targetY = clampSize(y(), available.top() + kEdgeMargin, maxTop);
        } else if (side == QStringLiteral("right")) {
            targetX = available.right() - width() + 1 - kEdgeMargin;
            targetY = clampSize(y(), available.top() + kEdgeMargin, maxTop);
        } else if (side == QStringLiteral("top")) {
            targetX = clampSize(available.center().x() - width() / 2, available.left() + kEdgeMargin, maxLeft);
            targetY = available.top() + kEdgeMargin;
        } else {
            targetX = clampSize(available.center().x() - width() / 2, available.left() + kEdgeMargin, maxLeft);
            targetY = available.bottom() - height() + 1 - kEdgeMargin;
        }
    }

    animateMove(this, QPoint(targetX, targetY), [this]() { emit positionChanged(pos()); });
}

QWidget *QuotaWidget::cardWidget() const
{
    return m_card;
}

void QuotaWidget::showCard()
{
    if (!m_card) {
        m_card = new QuotaCard(this);
        m_card->setAccount(m_account);
        m_card->setDisplayRemaining(m_displayRemaining);
        m_card->setSnapshot(m_snapshot);
        m_card->setWatchedCount(m_watched, m_running, m_failed);
        connect(m_card, &QuotaCard::openMainWindowRequested, this, [this]() {
            if (m_card)
                m_card->dismissAnimated();
            emit openMainWindowRequested();
        });
    }
    m_card->setAccount(m_account);
    m_card->setDisplayRemaining(m_displayRemaining);
    m_card->setSnapshot(m_snapshot);
    m_card->setWatchedCount(m_watched, m_running, m_failed);

    // 把悬浮条朝向屏幕内部的那条边作为卡片锚点。
    QPoint anchor;
    if (m_edge == QStringLiteral("left")) {
        anchor = mapToGlobal(QPoint(width(), height() / 2));
    } else if (m_edge == QStringLiteral("top")) {
        anchor = mapToGlobal(QPoint(width() / 2, height()));
    } else if (m_edge == QStringLiteral("bottom")) {
        anchor = mapToGlobal(QPoint(width() / 2, 0));
    } else if (m_edge == QStringLiteral("right")) {
        anchor = mapToGlobal(QPoint(0, height() / 2));
    } else {
        const QRect available = availableScreenRect();
        if (frameGeometry().center().x() > available.center().x())
            anchor = mapToGlobal(QPoint(0, height() / 2));
        else
            anchor = mapToGlobal(QPoint(width(), height() / 2));
    }
    m_card->popupNear(anchor);
}

void QuotaWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
}

void QuotaWidget::hideCard()
{
    // 收回动画：卡片收缩并淡出到悬浮条位置。
    if (m_card)
        m_card->dismissAnimated();
}

void QuotaWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        setProperty("arcClickCandidate", true);
        setProperty("arcPressGlobal", event->globalPosition().toPoint());
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void QuotaWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint global = event->globalPosition().toPoint();
        const QPoint pressGlobal = property("arcPressGlobal").toPoint();
        if ((global - pressGlobal).manhattanLength() > QApplication::startDragDistance()) {
            setProperty("arcClickCandidate", false);
            hideCard();
        }
        move(global - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void QuotaWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        const bool clickCandidate = property("arcClickCandidate").toBool();
        setProperty("arcClickCandidate", false);
        if (clickCandidate) {
            if (m_card && m_card->isVisible())
                hideCard();
            else
                showCard();
        } else {
            snapToNearestEdge();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void QuotaWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        hideCard();
        emit openMainWindowRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void QuotaWidget::contextMenuEvent(QContextMenuEvent *event)
{
    hideCard();
    QMenu menu(this);
    QAction *openAction = menu.addAction(QStringLiteral("打开主窗口"));
    QAction *settingsAction = menu.addAction(QStringLiteral("设置…"));
    menu.addSeparator();
    QAction *quitAction = menu.addAction(QStringLiteral("退出"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == openAction)
        emit openMainWindowRequested();
    else if (chosen == settingsAction)
        emit settingsRequested();
    else if (chosen == quitAction)
        emit quitRequested();
}

void QuotaWidget::enterEvent(QEnterEvent *event)
{
    if (!m_dragging)
        setCursor(Qt::OpenHandCursor);
    QWidget::enterEvent(event);
}

void QuotaWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);

    if (!m_card || !m_card->isVisible())
        return;
    // 鼠标离开后延迟收起卡片；若移到卡片上则保持显示。
    QTimer::singleShot(320, this, [this]() {
        if (!m_card || !m_card->isVisible())
            return;
        const QPoint global = QCursor::pos();
        if (rect().contains(mapFromGlobal(global)))
            return;
        if (m_card->rect().contains(m_card->mapFromGlobal(global)))
            return;
        m_card->dismissAnimated();
    });
}

void QuotaWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = qMax<qreal>(8.0, width() * 0.18);
    const QRectF barRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(barRect, radius, radius);
    QColor glass = theme::surface();
    glass.setAlphaF(m_opacityPercent / 100.0);
    painter.fillPath(path, glass);
    painter.setPen(QPen(theme::border(), 1.0));
    painter.drawPath(path);

    // 看守计数上方的细分隔线。
    if (m_watchBadge) {
        const qreal separatorGap = qMax<qreal>(2.0, 5.0 * m_scale / 100.0);
        const qreal separatorY = m_watchBadge->geometry().top() - separatorGap;
        if (separatorY > height() * 0.5) {
            painter.setPen(QPen(theme::track(), 1.0));
            painter.drawLine(QPointF(width() * 0.24, separatorY),
                             QPointF(width() * 0.76, separatorY));
        }
    }
}

void QuotaWidget::snapToNearestEdge()
{
    const QRect available = availableScreenRect();
    const int distanceLeft = geometry().left() - available.left();
    const int distanceRight = available.right() - geometry().right();
    const int distanceTop = geometry().top() - available.top();
    const int distanceBottom = available.bottom() - geometry().bottom();

    int nearest = distanceLeft;
    QString side = QStringLiteral("left");
    if (distanceRight < nearest) {
        nearest = distanceRight;
        side = QStringLiteral("right");
    }
    if (distanceTop < nearest) {
        nearest = distanceTop;
        side = QStringLiteral("top");
    }
    if (distanceBottom < nearest) {
        nearest = distanceBottom;
        side = QStringLiteral("bottom");
    }

    if (nearest > kSnapDistance) {
        // 离屏幕边缘较远：保持自由悬浮。
        m_edge = QStringLiteral("auto");
        emit positionChanged(pos());
        return;
    }

    const int maxLeft = qMax(available.left() + kEdgeMargin, available.right() - width() + 1 - kEdgeMargin);
    const int maxTop = qMax(available.top() + kEdgeMargin, available.bottom() - height() + 1 - kEdgeMargin);
    m_edge = side;
    QPoint target = pos();
    if (side == QStringLiteral("left")) {
        target.setX(available.left() + kEdgeMargin);
        target.setY(clampSize(y(), available.top() + kEdgeMargin, maxTop));
    } else if (side == QStringLiteral("right")) {
        target.setX(available.right() - width() + 1 - kEdgeMargin);
        target.setY(clampSize(y(), available.top() + kEdgeMargin, maxTop));
    } else if (side == QStringLiteral("top")) {
        target.setX(clampSize(x(), available.left() + kEdgeMargin, maxLeft));
        target.setY(available.top() + kEdgeMargin);
    } else {
        target.setX(clampSize(x(), available.left() + kEdgeMargin, maxLeft));
        target.setY(available.bottom() - height() + 1 - kEdgeMargin);
    }
    animateMove(this, target, [this]() { emit positionChanged(pos()); });
}

QRect QuotaWidget::availableScreenRect() const
{
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return QRect(0, 0, 1920, 1080);
    return screen->availableGeometry();
}

} // namespace arc