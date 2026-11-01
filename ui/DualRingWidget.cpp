#include "DualRingWidget.h"

#include "Theme.h"

#include <QDateTime>
#include <QPainter>
#include <QTimer>

#include <cmath>

namespace arc {

DualRingWidget::DualRingWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_logo = QPixmap(QStringLiteral(":/resources/openai-logo-white.png"));
    if (m_logo.isNull())
        m_logo = QPixmap(QStringLiteral(":/openai-logo-white.png"));

    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, [this]() { update(); });
    m_timer->start();

    m_pulseTimer = new QTimer(this);
    m_pulseTimer->setInterval(40);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulsePhase += 0.18;
        if (m_pulsePhase > 6.283185307)
            m_pulsePhase -= 6.283185307;
        update();
    });
}

void DualRingWidget::syncPulseTimer()
{
    const bool shouldRun = m_pulseActive || m_fiveExhausted || m_weeklyExhausted;
    if (shouldRun) {
        if (!m_pulseTimer->isActive())
            m_pulseTimer->start();
    } else {
        m_pulseTimer->stop();
        m_pulsePhase = 0.0;
    }
}

void DualRingWidget::setPulseActive(bool active)
{
    if (m_pulseActive == active)
        return;
    m_pulseActive = active;
    syncPulseTimer();
    update();
}

void DualRingWidget::setData(int fiveHourUsedPercent, int weeklyUsedPercent,
                             bool fiveHourValid, bool weeklyValid,
                             qint64 fiveHourResetAt, qint64 weeklyResetAt,
                             qint64 fiveHourWindowMins, qint64 weeklyWindowMins)
{
    m_fiveUsed = qBound(0, fiveHourUsedPercent, 100);
    m_weeklyUsed = qBound(0, weeklyUsedPercent, 100);
    m_fiveValid = fiveHourValid;
    m_weeklyValid = weeklyValid;
    m_fiveResetAt = fiveHourResetAt;
    m_weeklyResetAt = weeklyResetAt;
    m_fiveWindowMins = fiveHourWindowMins > 0 ? fiveHourWindowMins : 300;
    m_weeklyWindowMins = weeklyWindowMins > 0 ? weeklyWindowMins : 10080;
    m_fiveExhausted = fiveHourValid && m_fiveUsed >= 100;
    m_weeklyExhausted = weeklyValid && m_weeklyUsed >= 100;
    syncPulseTimer();
    update();
}

void DualRingWidget::setRemainingMode(bool remaining)
{
    if (m_remaining == remaining)
        return;
    m_remaining = remaining;
    update();
}

QSize DualRingWidget::sizeHint() const
{
    return QSize(72, 72);
}

void DualRingWidget::paintCountdownArc(QPainter &painter, const QPointF &center, qreal radius,
                                       qreal thickness, qint64 resetAt, qint64 windowMins)
{
    if (radius <= thickness || resetAt <= 0 || windowMins <= 0)
        return;
    const QRectF ringRect(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);

    QPen track(theme::track(), thickness);
    track.setCapStyle(Qt::RoundCap);
    painter.setPen(track);
    painter.drawEllipse(ringRect);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qreal windowSec = static_cast<qreal>(windowMins) * 60.0;
    const qreal remainingSec = qBound<qreal>(0.0, static_cast<qreal>(resetAt - now), windowSec);
    const qreal fraction = remainingSec / windowSec;
    if (fraction <= 0.0)
        return;

    QColor arcColor = theme::text();
    arcColor.setAlpha(215);
    QPen progress(arcColor, thickness);
    progress.setCapStyle(Qt::RoundCap);
    painter.setPen(progress);
    painter.drawArc(ringRect, 90 * 16, qRound(-360.0 * 16.0 * fraction));
}

void DualRingWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const qreal side = qMin(width(), height());
    if (side <= 24)
        return;
    const QPointF center(width() / 2.0, height() / 2.0);

    const qreal outerRadius = side / 2.0 - 3.0;
    const qreal countThickness = qMax<qreal>(1.5, side * 0.028);
    const qreal usageThickness = qMax<qreal>(3.2, side * 0.075);
    const qreal innerUsageThickness = qMax<qreal>(3.0, side * 0.068);
    const qreal gap = qMax<qreal>(2.0, side * 0.032);

    const qreal fiveUsageRadius = outerRadius - countThickness - gap;
    const qreal weeklyUsageRadius = fiveUsageRadius - usageThickness - gap;
    const qreal weeklyCountdownRadius = weeklyUsageRadius - innerUsageThickness - gap;

    // 外圈细白弧：5h 刷新倒计时
    paintCountdownArc(painter, center, outerRadius, countThickness, m_fiveResetAt, m_fiveWindowMins);

    auto drawUsageRing = [&](qreal radius, qreal thickness, int usedPercent, bool valid) {
        if (radius <= thickness)
            return;
        const QRectF ringRect(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
        QPen track(theme::track(), thickness);
        track.setCapStyle(Qt::RoundCap);
        painter.setPen(track);
        painter.drawEllipse(ringRect);
        if (!valid)
            return;
        if (usedPercent >= 100) {
            // 额度耗尽：整个圆环常亮红色呼吸灯。
            const qreal breath = 0.5 + 0.5 * std::sin(m_pulsePhase);
            QColor glowColor = theme::red();
            glowColor.setAlphaF(0.11 + 0.19 * breath);
            QPen glowPen(glowColor, thickness * 1.9);
            glowPen.setCapStyle(Qt::RoundCap);
            painter.setPen(glowPen);
            painter.drawEllipse(ringRect);

            QColor redRing = theme::red();
            redRing.setAlphaF(0.50 + 0.30 * breath);
            QPen redPen(redRing, thickness);
            redPen.setCapStyle(Qt::RoundCap);
            painter.setPen(redPen);
            painter.drawEllipse(ringRect);
            return;
        }
        if (usedPercent <= 0)
            return;
        const int displayPercent = m_remaining ? 100 - usedPercent : usedPercent;
        if (displayPercent <= 0)
            return;
        if (m_pulseActive) {
            const qreal glow = 0.5 + 0.5 * std::sin(m_pulsePhase);
            QColor glowColor = theme::quotaColor(usedPercent);
            glowColor.setAlphaF(0.07 + 0.15 * glow);
            QPen glowPen(glowColor, thickness * 1.9);
            glowPen.setCapStyle(Qt::RoundCap);
            painter.setPen(glowPen);
            painter.drawArc(ringRect, 90 * 16, qRound(-360.0 * 16.0 * (displayPercent / 100.0)));
        }
        QPen progress(theme::quotaColor(usedPercent), thickness);
        progress.setCapStyle(Qt::RoundCap);
        painter.setPen(progress);
        painter.drawArc(ringRect, 90 * 16, qRound(-360.0 * 16.0 * (displayPercent / 100.0)));
    };

    // 5h 用量（外侧粗环）、每周用量（内侧粗环）
    drawUsageRing(fiveUsageRadius, usageThickness, m_fiveUsed, m_fiveValid);
    drawUsageRing(weeklyUsageRadius, innerUsageThickness, m_weeklyUsed, m_weeklyValid);

    // 最内圈细白弧：每周刷新倒计时
    paintCountdownArc(painter, center, weeklyCountdownRadius, countThickness, m_weeklyResetAt,
                      m_weeklyWindowMins);

    // 中心固定 logo
    if (!m_logo.isNull()) {
        const qreal logoSize = side * 0.36;
        const QRectF logoRect(center.x() - logoSize / 2.0, center.y() - logoSize / 2.0,
                              logoSize, logoSize);
        painter.drawPixmap(logoRect, m_logo, QRectF(m_logo.rect()));
    }
}

} // namespace arc