#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QTimer;

namespace arc {

// 圆形额度环：进度弧 + 中心文字。
class RingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RingWidget(QWidget *parent = nullptr);

    void setPercent(int percent); // 0-100，表示“已用”百分比
    void setTitle(const QString &title);
    void setTitleEmphasis(bool emphasis); // 标题放大加粗（参考样式环中心文字）
    void setSubtitle(const QString &subtitle);
    void setPercentTextVisible(bool visible);
    void setAccentColor(const QColor &color);
    // true 时百分比表示“剩余量”，进度环按剩余量填充、颜色仍按已用量取色。
    void setRemainingMode(bool remaining);
    void setPulseActive(bool active);
    // 额度耗尽（剩余 0%）：整个圆环外圈常亮红色呼吸灯。
    void setExhausted(bool exhausted);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void syncPulseTimer();

    int m_percent = 0;
    QString m_title;
    QString m_subtitle;
    bool m_percentTextVisible = true;
    bool m_remainingMode = false;
    bool m_titleEmphasis = false;
    bool m_pulseActive = false;
    bool m_exhausted = false;
    qreal m_pulsePhase = 0.0;
    QTimer *m_pulseTimer = nullptr;
    QColor m_accent = QColor(45, 212, 191);
    int m_diameter = 74;
};

} // namespace arc