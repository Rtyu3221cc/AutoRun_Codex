#pragma once

#include <QColor>
#include <QPixmap>
#include <QWidget>

class QTimer;

namespace arc {

// 极简模式：外细环 = 5h 刷新倒计时，5h 用量环，周用量环，内细环 = 每周刷新倒计时。
// 中心固定显示 ChatGPT logo；底部百分比/刷新时间由 QuotaWidget 交替显示。
class DualRingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DualRingWidget(QWidget *parent = nullptr);

    void setData(int fiveHourUsedPercent, int weeklyUsedPercent,
                 bool fiveHourValid, bool weeklyValid,
                 qint64 fiveHourResetAt, qint64 weeklyResetAt,
                 qint64 fiveHourWindowMins, qint64 weeklyWindowMins);
    void setRemainingMode(bool remaining);
    void setPulseActive(bool active);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void syncPulseTimer();
    void paintCountdownArc(QPainter &painter, const QPointF &center, qreal radius, qreal thickness,
                           qint64 resetAt, qint64 windowMins);

    int m_fiveUsed = 0;
    int m_weeklyUsed = 0;
    bool m_fiveValid = false;
    bool m_weeklyValid = false;
    qint64 m_fiveResetAt = 0;
    qint64 m_weeklyResetAt = 0;
    qint64 m_fiveWindowMins = 300;
    qint64 m_weeklyWindowMins = 10080;
    bool m_remaining = false;
    QPixmap m_logo;
    QTimer *m_timer = nullptr;
    bool m_pulseActive = false;
    bool m_fiveExhausted = false;
    bool m_weeklyExhausted = false;
    qreal m_pulsePhase = 0.0;
    QTimer *m_pulseTimer = nullptr;
};

} // namespace arc