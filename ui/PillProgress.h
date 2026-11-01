#pragma once

#include <QColor>
#include <QWidget>

class QTimer;

namespace arc {

// 圆角胶囊进度条：轨道 + 纯色填充，两端始终是圆角（不依赖 QSS 圆角）。
// 有进行中对话时，绘制与悬浮环同频的呼吸光晕（光晕画在条形外的留白里，不会被裁掉）。
// 额度耗尽（剩余 0%）时，整条进度条外圈常亮红色呼吸灯，即使没有进行中对话也呼吸。
class PillProgress : public QWidget
{
    Q_OBJECT

public:
    explicit PillProgress(QWidget *parent = nullptr);

    void setValue(int percent);
    void setAccentColor(const QColor &color);
    void setTrackColor(const QColor &color);
    void setPulseActive(bool active);
    void setExhausted(bool exhausted);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void syncPulseTimer();

    static constexpr qreal kBarHeight = 8.0;
    static constexpr qreal kGlowMargin = 3.0;

    int m_value = 0;
    QColor m_accent;
    QColor m_track;
    bool m_pulseActive = false;
    bool m_exhausted = false;
    qreal m_pulsePhase = 0.0;
    QTimer *m_pulseTimer = nullptr;
};

} // namespace arc
