#pragma once

#include "core/Types.h"

#include <QColor>
#include <QWidget>

class QPropertyAnimation;

namespace arc {

// 悬浮条底部“● 活动状态”指示灯（全局，不是单个看守任务的状态）：
//   运行中（绿黄呼吸）/ 等待回复（黄色呼吸）/ 异常中断（红色呼吸）/
//   刚完成（蓝色常亮）/ 最近结束（绿色常亮）/ 无活动对话（灰色、不呼吸）。
// 文字+呼吸点随“悬浮条缩放”设置整体等比缩放（100 = 设计尺寸）。
class WatchBadge : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal breath READ breath WRITE setBreath)
    Q_PROPERTY(qreal ripple READ ripple WRITE setRipple)

public:
    explicit WatchBadge(QWidget *parent = nullptr);

    void setActivity(ActivityState state, int count);
    ActivityState activity() const { return m_state; }

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
    QColor stateColor() const;
    static bool stateBreathes(ActivityState state);

    ActivityState m_state = ActivityState::Idle;
    int m_count = 0;
    int m_scalePercent = 100;
    qreal m_breath = 0.0;
    qreal m_ripple = 0.0;
    QPropertyAnimation *m_animation = nullptr;
    QPropertyAnimation *m_rippleAnimation = nullptr;
};

} // namespace arc
