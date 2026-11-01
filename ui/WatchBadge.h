#pragma once

#include <QColor>
#include <QWidget>

class QPropertyAnimation;

namespace arc {

// “● 看守中 N”徽标：呼吸点带光晕和缓动动画。
class WatchBadge : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal breath READ breath WRITE setBreath)
    Q_PROPERTY(qreal ripple READ ripple WRITE setRipple)

public:
    explicit WatchBadge(QWidget *parent = nullptr);

    void setWatchedCount(int count);
    // 续跑重试耗尽后看守灯由绿变红。
    void setFailedCount(int count);
    // 随悬浮条“缩放”设置整体等比缩放（文字、圆点、呼吸光晕、涟漪一起变），100 = 设计尺寸。
    void setScalePercent(int percent);
    int scalePercent() const { return m_scalePercent; }
    qreal breath() const { return m_breath; }
    void setBreath(qreal value);
    qreal ripple() const { return m_ripple; }
    void setRipple(qreal value);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_watched = 0;
    int m_failed = 0;
    int m_scalePercent = 100;
    qreal m_breath = 0.0;
    qreal m_ripple = 0.0;
    QPropertyAnimation *m_animation = nullptr;
    QPropertyAnimation *m_rippleAnimation = nullptr;
};

} // namespace arc