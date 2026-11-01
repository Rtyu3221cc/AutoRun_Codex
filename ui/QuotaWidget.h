#pragma once

#include "core/AccountProfile.h"
#include "core/Types.h"

#include <QPixmap>
#include <QPoint>
#include <QWidget>

class QLabel;
class QShowEvent;
class QTimer;

namespace arc {

class QuotaCard;
class DualRingWidget;
class WatchBadge;

// 贴在屏幕边缘的悬余额度条（两个环：5h 窗口 / 每周窗口）。
class QuotaWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QuotaWidget(QWidget *parent = nullptr);

    void setSnapshot(const RateLimitSnapshot &snapshot);
    void setAccountProfile(const AccountProfile &account);
    void setDisplayRemaining(bool remaining);
    void setFloatingStyle(const QString &style); // reference / classic / minimal
    void setBackgroundOpacity(int percent);
    void applyThemeStyles();
    void setWatchedCount(int count, int runningCount, int failedCount = 0);
    void setAlwaysOnTopHintEnabled(bool on);
    void setScalePercent(int percent);
    void applyDockEdge(const QString &edge); // left/right/top/bottom/auto
    void showCard();
    QWidget *cardWidget() const;
    void hideCard();

signals:
    void openMainWindowRequested();
    void settingsRequested();
    void quitRequested();
    void positionChanged(const QPoint &pos);

protected:
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void snapToNearestEdge();
    QRect availableScreenRect() const;

    QuotaCard *m_card = nullptr;
    RateLimitSnapshot m_snapshot;
    AccountProfile m_account;
    bool m_displayRemaining = false;
    int m_opacityPercent = 92;
    QString m_style = QStringLiteral("reference");
    int m_watched = 0;
    int m_running = 0;
    int m_failed = 0;
    bool m_dragging = false;
    QPoint m_dragOffset;
    int m_scale = 100;
    QString m_edge = QStringLiteral("right");
    bool m_alwaysOnTop = true;
    WatchBadge *m_watchBadge = nullptr;

    DualRingWidget *m_dualRing = nullptr;
    QLabel *m_minFiveLabel = nullptr;
    QLabel *m_minFiveTime = nullptr;
    QLabel *m_minWeeklyLabel = nullptr;
    QLabel *m_minWeeklyTime = nullptr;
    QTimer *m_minimalTimer = nullptr;
    bool m_minShowFive = true;
    void updateMinimalInfo();

    // 重置卡临期时，在“周用量百分比”旁显示橙色小点（参考样式贴在百分比文字右侧，
    // 经典样式贴在周环右上角；极简样式直接拼在“周 · 剩余 x%”文字里）。
    QLabel *m_creditDot = nullptr;
    void updateCreditDot();
    bool creditDotVisible() const;
};

} // namespace arc