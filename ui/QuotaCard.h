#pragma once

#include "core/AccountProfile.h"
#include "core/Types.h"

#include <QPoint>
#include <QWidget>

class QGraphicsOpacityEffect;
class QParallelAnimationGroup;

namespace arc {

// 悬浮条弹出的用量详情卡片。
// 打开时从悬浮条位置展开、关闭时收回悬浮条（窗口几何 + 内容淡入淡出）。
class QuotaCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal reveal READ reveal WRITE setReveal)

public:
    explicit QuotaCard(QWidget *parent = nullptr);

    void setSnapshot(const RateLimitSnapshot &snapshot);
    void setAccount(const AccountProfile &account);
    void setDisplayRemaining(bool remaining);
    void setBackgroundOpacity(int percent);
    void setWatchedCount(int count, int runningCount, int failedCount = 0);
    void setPulseActive(bool active);
    // 深浅色主题切换时调用：重建内部控件，让所有文字/分隔线颜色立刻跟上主题。
    void applyThemeStyles();

    // 在锚点附近弹出：从悬浮条位置展开到最终位置。
    void popupNear(const QPoint &globalPos);
    // 收回悬浮条：收缩 + 淡出后隐藏。
    void dismissAnimated();

    qreal reveal() const { return m_reveal; }
    void setReveal(qreal value);

signals:
    void openMainWindowRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildUi();
    QRect anchorRect() const;
    void stopAnimation();
    void startOpenAnimation(const QRect &target);
    void startCloseAnimation(const QPoint &anchor);

    RateLimitSnapshot m_snapshot;
    AccountProfile m_account;
    bool m_displayRemaining = false;
    int m_opacityPercent = 92;
    qreal m_reveal = 1.0;

    int m_watched = 0;
    int m_running = 0;
    int m_failed = 0;
    bool m_pulseActive = false;

    QWidget *m_content = nullptr;
    QGraphicsOpacityEffect *m_contentOpacity = nullptr;
    QParallelAnimationGroup *m_animation = nullptr;
    QPoint m_anchor;
    bool m_opening = false;
    bool m_closing = false;
};

} // namespace arc