#include "WatchBadge.h"

#include "Theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPropertyAnimation>

namespace arc {

WatchBadge::WatchBadge(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_animation = new QPropertyAnimation(this, "breath", this);
    m_animation->setDuration(3000);
    m_animation->setStartValue(0.0);
    m_animation->setKeyValueAt(0.5, 1.0);
    m_animation->setEndValue(0.0);
    m_animation->setEasingCurve(QEasingCurve::InOutSine);
    m_animation->setLoopCount(-1);

    // 外圈：只扩散、不收缩的涟漪动画。
    m_rippleAnimation = new QPropertyAnimation(this, "ripple", this);
    m_rippleAnimation->setDuration(1800);
    m_rippleAnimation->setStartValue(0.0);
    m_rippleAnimation->setEndValue(1.0);
    m_rippleAnimation->setLoopCount(-1);
}

void WatchBadge::setWatchedCount(int count)
{
    m_watched = qMax(0, count);
    if (m_watched > 0) {
        if (m_animation->state() != QAbstractAnimation::Running)
            m_animation->start();
        if (m_rippleAnimation->state() != QAbstractAnimation::Running)
            m_rippleAnimation->start();
    } else {
        m_animation->stop();
        m_rippleAnimation->stop();
        m_breath = 0.0;
        m_ripple = 0.0;
    }
    update();
}

void WatchBadge::setFailedCount(int count)
{
    m_failed = qMax(0, count);
    update();
}

void WatchBadge::setScalePercent(int percent)
{
    const int clamped = qBound(50, percent, 150);
    if (m_scalePercent == clamped)
        return;
    m_scalePercent = clamped;
    updateGeometry();
    update();
}

void WatchBadge::setBreath(qreal value)
{
    m_breath = qBound<qreal>(0.0, value, 1.0);
    update();
}

void WatchBadge::setRipple(qreal value)
{
    m_ripple = qBound<qreal>(0.0, value, 1.0);
    update();
}

QSize WatchBadge::sizeHint() const
{
    const qreal scale = m_scalePercent / 100.0;
    return QSize(qRound(100.0 * scale), qMax(12, qRound(17.0 * scale)));
}

void WatchBadge::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // 徽标内的所有尺寸都按悬浮条“缩放”设置等比缩放：文字、圆点、呼吸光晕、
    // 涟漪一起变大/变小（100 = 设计尺寸），不再写死像素。
    const qreal scale = m_scalePercent / 100.0;
    const bool active = m_watched > 0;
    const bool failed = m_failed > 0;
    const QColor dotColor = active ? (failed ? theme::red() : theme::green())
                                   : (theme::isDark() ? QColor(92, 97, 107) : QColor(148, 154, 164));
    const qreal breath = active ? m_breath : 0.0;
    const qreal ripple = active ? m_ripple : 0.0;
    const qreal cy = height() / 2.0;
    // 圆点固定尺寸（按缩放换算），避免文字左右跳动；布局按最大光晕预留，文字永远不动。
    const qreal dotRadius = 3.6 * scale;
    const qreal layoutDotRadius = 5.0 * scale;

    QFont textFont = font();
    textFont.setPixelSize(qBound(8, qRound(12.0 * scale), 20));
    const QFontMetricsF metrics(textFont);
    const QString text = QStringLiteral("看守中 %1").arg(m_watched);
    const qreal textWidth = metrics.horizontalAdvance(text);
    const qreal gap = 7.0 * scale;
    const qreal contentWidth = layoutDotRadius * 2.0 + gap + textWidth;
    const qreal startX = qMax<qreal>(0.0, (width() - contentWidth) / 2.0);
    const qreal dotX = startX + layoutDotRadius;
    const qreal textX = dotX + layoutDotRadius + gap;

    if (active) {
        // 呼吸光晕：一个扩张并淡出的软光斑 + 一圈向外扩散的细环（半径随缩放走）。
        const qreal glowRadius = (5.0 + 9.0 * breath) * scale;
        QRadialGradient gradient(QPointF(dotX, cy), glowRadius);
        QColor glow = dotColor;
        glow.setAlphaF(0.32 + 0.18 * (1.0 - breath));
        QColor mid = dotColor;
        mid.setAlphaF(0.22);
        QColor transparent = dotColor;
        transparent.setAlphaF(0.0);
        gradient.setColorAt(0.0, glow);
        gradient.setColorAt(0.5, mid);
        gradient.setColorAt(1.0, transparent);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawEllipse(QPointF(dotX, cy), glowRadius, glowRadius);

        // 涟漪：只向外扩散并淡出（不收缩），到边界后重新开始。
        const qreal ringRadius = (5.0 + 10.0 * ripple) * scale;
        QColor ringColor = dotColor;
        ringColor.setAlphaF(0.6 * (1.0 - ripple));
        QPen ringPen(ringColor, qMax<qreal>(0.7, 1.6 * scale));
        painter.setPen(ringPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(dotX, cy), ringRadius, ringRadius);
    }

    painter.setPen(Qt::NoPen);
    QColor core = dotColor;
    core.setAlphaF(active ? 0.9 : 0.85);
    painter.setBrush(core);
    painter.drawEllipse(QPointF(dotX, cy), dotRadius, dotRadius);

    painter.setFont(textFont);
    painter.setPen(theme::textMuted());
    const qreal baseline = (height() + metrics.ascent() - metrics.descent()) / 2.0;
    painter.drawText(QPointF(textX, baseline), text);
}

} // namespace arc