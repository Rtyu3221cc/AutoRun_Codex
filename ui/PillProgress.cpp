#include "PillProgress.h"

#include "Theme.h"

#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

#include <cmath>

namespace arc {

PillProgress::PillProgress(QWidget *parent)
    : QWidget(parent)
    , m_accent(theme::green())
    , m_track(theme::track())
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int totalHeight = qRound(kBarHeight + kGlowMargin * 2.0);
    setMinimumHeight(totalHeight);
    setMaximumHeight(totalHeight);

    m_pulseTimer = new QTimer(this);
    m_pulseTimer->setInterval(40);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulsePhase += 0.18;
        if (m_pulsePhase > 6.283185307)
            m_pulsePhase -= 6.283185307;
        update();
    });
}

void PillProgress::syncPulseTimer()
{
    // 进行中对话的呼吸光晕，以及额度耗尽的红色呼吸灯，共用同一个相位/定时器。
    const bool shouldRun = m_pulseActive || m_exhausted;
    if (shouldRun) {
        if (!m_pulseTimer->isActive())
            m_pulseTimer->start();
    } else {
        m_pulseTimer->stop();
        m_pulsePhase = 0.0;
    }
}

void PillProgress::setPulseActive(bool active)
{
    if (m_pulseActive == active)
        return;
    m_pulseActive = active;
    syncPulseTimer();
    update();
}

void PillProgress::setExhausted(bool exhausted)
{
    if (m_exhausted == exhausted)
        return;
    m_exhausted = exhausted;
    syncPulseTimer();
    update();
}

void PillProgress::setValue(int percent)
{
    const int value = qBound(0, percent, 100);
    if (m_value == value)
        return;
    m_value = value;
    update();
}

void PillProgress::setAccentColor(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    update();
}

void PillProgress::setTrackColor(const QColor &color)
{
    if (m_track == color)
        return;
    m_track = color;
    update();
}

QSize PillProgress::sizeHint() const
{
    return QSize(180, qRound(kBarHeight + kGlowMargin * 2.0));
}

void PillProgress::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const qreal radius = kBarHeight / 2.0;
    const qreal top = (height() - kBarHeight) / 2.0;
    // 左右两端也留出 kGlowMargin：否则呼吸光晕会在控件边界被裁掉，看起来“突然中断”。
    const QRectF trackRect(kGlowMargin, top, qMax<qreal>(0.0, width() - kGlowMargin * 2.0), kBarHeight);
    painter.setBrush(m_track);
    painter.drawRoundedRect(trackRect, radius, radius);

    // 额度耗尽：整条进度条外侧全显示为红色呼吸灯（不依赖是否有进行中对话）。
    if (m_exhausted) {
        const qreal breath = 0.5 + 0.5 * std::sin(m_pulsePhase);
        QColor glow = theme::red();
        glow.setAlphaF(0.11 + 0.20 * breath);
        const QRectF glowRect = trackRect.adjusted(-kGlowMargin, -kGlowMargin, kGlowMargin, kGlowMargin);
        painter.setBrush(glow);
        painter.drawRoundedRect(glowRect, radius + kGlowMargin, radius + kGlowMargin);

        QColor edge = theme::red();
        edge.setAlphaF(0.50 + 0.30 * breath);
        painter.setPen(QPen(edge, 1.4));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(trackRect.adjusted(0.8, 0.8, -0.8, -0.8), radius, radius);
        painter.setPen(Qt::NoPen);
    }

    if (m_value <= 0)
        return;
    const qreal fillWidth = qMax(kBarHeight, trackRect.width() * (m_value / 100.0));
    const QRectF fillRect(trackRect.x(), top, qMin<qreal>(fillWidth, trackRect.width()), kBarHeight);

    if (m_pulseActive && !m_exhausted) {
        // 与 RingWidget 同频（40ms tick、约 1.4s 一个周期），强度参照圆形环的呼吸光晕。
        const qreal glow = 0.5 + 0.5 * std::sin(m_pulsePhase);
        QColor glowColor = m_accent;
        glowColor.setAlphaF(0.09 + 0.20 * glow);
        const QRectF glowRect = fillRect.adjusted(-kGlowMargin, -kGlowMargin, kGlowMargin, kGlowMargin);
        painter.setBrush(glowColor);
        painter.drawRoundedRect(glowRect, radius + kGlowMargin, radius + kGlowMargin);
    }

    painter.setBrush(m_accent);
    painter.drawRoundedRect(fillRect, radius, radius);
}

} // namespace arc