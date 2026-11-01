#include "RingWidget.h"

#include "ui/Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

#include <cmath>

namespace arc {

namespace {

const QColor kDefaultAccent = theme::green();

} // namespace

RingWidget::RingWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMinimumSize(36, 36);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    m_pulseTimer = new QTimer(this);
    m_pulseTimer->setInterval(40);
    connect(m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulsePhase += 0.18;
        if (m_pulsePhase > 6.283185307)
            m_pulsePhase -= 6.283185307;
        update();
    });
}

void RingWidget::syncPulseTimer()
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

void RingWidget::setPulseActive(bool active)
{
    if (m_pulseActive == active)
        return;
    m_pulseActive = active;
    syncPulseTimer();
    update();
}

void RingWidget::setExhausted(bool exhausted)
{
    if (m_exhausted == exhausted)
        return;
    m_exhausted = exhausted;
    syncPulseTimer();
    update();
}

void RingWidget::setPercent(int percent)
{
    const int value = qBound(0, percent, 100);
    if (m_percent == value)
        return;
    m_percent = value;
    update();
}

void RingWidget::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    update();
}

void RingWidget::setSubtitle(const QString &subtitle)
{
    if (m_subtitle == subtitle)
        return;
    m_subtitle = subtitle;
    update();
}

void RingWidget::setTitleEmphasis(bool emphasis)
{
    if (m_titleEmphasis == emphasis)
        return;
    m_titleEmphasis = emphasis;
    update();
}

void RingWidget::setPercentTextVisible(bool visible)
{
    if (m_percentTextVisible == visible)
        return;
    m_percentTextVisible = visible;
    update();
}

void RingWidget::setRemainingMode(bool remaining)
{
    if (m_remainingMode == remaining)
        return;
    m_remainingMode = remaining;
    update();
}

void RingWidget::setAccentColor(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    update();
}

QSize RingWidget::sizeHint() const
{
    return QSize(m_diameter + 8, m_diameter + 8);
}

QSize RingWidget::minimumSizeHint() const
{
    return QSize(40, 40);
}

void RingWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int side = qMin(width(), height());
    if (side <= 12)
        return;

    const qreal thickness = qMax<qreal>(4.0, side * 0.10);
    const QRectF outer((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    // 预留光晕空间，避免呼吸光效被控件边界裁掉。
    const qreal margin = thickness * 1.15;
    const QRectF ringRect = outer.adjusted(margin, margin, -margin, -margin);

    // 背景环
    QPen backgroundPen(theme::track(), thickness);
    backgroundPen.setCapStyle(Qt::RoundCap);
    painter.setPen(backgroundPen);
    painter.drawArc(ringRect, 0, 360 * 16);

    if (m_exhausted) {
        // 额度耗尽：整个圆环外圈常亮红色呼吸灯（不依赖是否有进行中对话）。
        const qreal breath = 0.5 + 0.5 * std::sin(m_pulsePhase);

        QColor glowColor = theme::red();
        glowColor.setAlphaF(0.11 + 0.19 * breath);
        QPen glowPen(glowColor, thickness * 2.1);
        glowPen.setCapStyle(Qt::RoundCap);
        painter.setPen(glowPen);
        painter.drawArc(ringRect, 0, 360 * 16);

        QColor redRing = theme::red();
        redRing.setAlphaF(0.50 + 0.30 * breath);
        QPen redPen(redRing, thickness);
        redPen.setCapStyle(Qt::RoundCap);
        painter.setPen(redPen);
        painter.drawArc(ringRect, 0, 360 * 16);
    } else {
        // 进度环：默认按已用百分比渐变；显式设置过强调色时使用强调色。
        const int usedPercent = m_remainingMode ? 100 - m_percent : m_percent;
        const QColor ringColor = (m_accent == kDefaultAccent) ? theme::quotaColor(usedPercent) : m_accent;

        // 有正在运行的对话时，进度弧外圈加一层呼吸光晕。
        if (m_pulseActive && m_percent > 0) {
            const qreal glow = 0.5 + 0.5 * std::sin(m_pulsePhase);
            QColor glowColor = ringColor;
            glowColor.setAlphaF(0.07 + 0.15 * glow);
            QPen glowPen(glowColor, thickness * 2.1);
            glowPen.setCapStyle(Qt::RoundCap);
            painter.setPen(glowPen);
            const int glowSpan = qRound(360.0 * 16.0 * (qBound(0, m_percent, 100) / 100.0));
            painter.drawArc(ringRect, 90 * 16, -glowSpan);
        }

        if (m_percent > 0) {
            QPen progressPen(ringColor, thickness);
            progressPen.setCapStyle(Qt::RoundCap);
            painter.setPen(progressPen);
            const int span = qRound(360.0 * 16.0 * (qBound(0, m_percent, 100) / 100.0));
            painter.drawArc(ringRect, 90 * 16, -span);
        }
    }

    // 中心文字：百分比 / 标题 / 副标题
    const bool hasTitle = !m_title.isEmpty();
    const bool hasSubtitle = !m_subtitle.isEmpty();

    QFont percentFont = font();
    percentFont.setPixelSize(qMax(10, qRound(side * 0.26)));
    percentFont.setBold(true);

    QFont titleFont = font();
    titleFont.setPixelSize(qMax(8, qRound(side * (m_titleEmphasis ? 0.21 : 0.135))));
    titleFont.setBold(m_titleEmphasis);

    QFont subtitleFont = font();
    subtitleFont.setPixelSize(qMax(7, qRound(side * 0.12)));
    subtitleFont.setBold(false);

    const QFontMetrics percentMetrics(percentFont);
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics subtitleMetrics(subtitleFont);

    int totalHeight = 0;
    if (m_percentTextVisible)
        totalHeight += percentMetrics.height();
    if (hasTitle)
        totalHeight += titleMetrics.height();
    if (hasSubtitle)
        totalHeight += subtitleMetrics.height();

    const qreal textWidth = qMin<qreal>(width(), side * 0.94);
    const qreal textX = (width() - textWidth) / 2.0;
    qreal textY = rect().center().y() - totalHeight / 2.0;

    if (m_percentTextVisible) {
        painter.setFont(percentFont);
        painter.setPen(theme::text());
        painter.drawText(QRectF(textX, textY, textWidth, percentMetrics.height()),
                         static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter),
                         QStringLiteral("%1%").arg(m_percent));
        textY += percentMetrics.height();
    }

    if (hasTitle) {
        painter.setFont(titleFont);
        painter.setPen(m_titleEmphasis ? theme::text() : theme::textMuted());
        painter.drawText(QRectF(textX, textY, textWidth, titleMetrics.height()),
                         static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter),
                         titleMetrics.elidedText(m_title, Qt::ElideRight, qRound(textWidth)));
        textY += titleMetrics.height();
    }

    if (hasSubtitle) {
        painter.setFont(subtitleFont);
        painter.setPen(theme::textDim());
        painter.drawText(QRectF(textX, textY, textWidth, subtitleMetrics.height()),
                         static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter),
                         subtitleMetrics.elidedText(m_subtitle, Qt::ElideRight, qRound(textWidth)));
    }
}

} // namespace arc