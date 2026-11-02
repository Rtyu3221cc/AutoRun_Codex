#pragma once

#include "core/Types.h"

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QTimer;

namespace arc {

class CodexClient;

// 看守队列：记录每个对话的续跑设置，按额度重置时间调度续跑。
class JobEngine : public QObject
{
    Q_OBJECT

public:
    explicit JobEngine(CodexClient *client, QObject *parent = nullptr);

    void loadState();
    void saveState() const;

    QList<ResumeJob> jobs() const { return m_jobs; }
    ResumeJob jobFor(const QString &threadId) const;
    bool hasJob(const QString &threadId) const;

    void addOrUpdateJob(const ResumeJob &job);
    void removeJob(const QString &threadId);
    void cancelJob(const QString &threadId); // 取消看守；若正在续跑则先中断当前 turn
    void setJobEnabled(const QString &threadId, bool enabled);
    void requestRunNow(const QString &threadId);
    void stopAll();

    void updateRateLimits(const RateLimitSnapshot &snapshot);
    // 额度重置后先等待多少秒再发续跑（默认 150 秒 ≈ 2.5 分钟）。
    void setResetWaitSeconds(int seconds);
    // 每轮最多尝试次数（默认 3；用尽后看守灯转红）。
    void setRetryAttempts(int attempts);
    // 写锁被 Codex 桌面版占用时，是否在用户离开后自动关闭桌面版释放写锁。
    void setAutoReleaseWriterLock(bool enabled);
    // 收到 ThreadCatalog 的线程状态后调用：若线程被限额打断且已启用看守，则安排到 resetAt。
    void notifyThreadState(const ThreadInfo &info);

signals:
    void jobsChanged();
    void logMessage(const QString &message);
    // 需要弹 Windows 托盘通知的关键事件（续跑完成/失败、写锁等待/释放、自动关闭桌面版）。
    void notifyRequested(const QString &title, const QString &message, bool warning);
    void jobStateChanged(const QString &threadId);

private slots:
    void onTick();
    void onTurnCompleted(const QString &threadId, const QJsonObject &turn);

private:
    ResumeJob *findJob(const QString &threadId);
    void execute(ResumeJob &job);
    void handleFailure(ResumeJob &job, const QString &error);
    void armForSnapshot(ResumeJob &job, qint64 resetAt);
    qint64 nextResetFromLimits() const;

    // thread-writer-lock 被 Codex 桌面版占用：不计失败次数，等待释放/按设置关闭桌面版。
    static bool isWriterLockError(const QString &error);
    void handleWriterBlocked(ResumeJob &job, const QString &error, bool attemptCounted);
    void processWriterBlocked(ResumeJob &job);

    CodexClient *m_client = nullptr;
    QTimer *m_timer = nullptr;
    QList<ResumeJob> m_jobs;
    RateLimitSnapshot m_limits;
    int m_runningCount = 0;
    int m_resetWaitSeconds = 150;
    int m_retryAttempts = 3;
    int m_retryDelaySeconds = 120;

    // 本进程的 app-server 已加载（因此自己持有写锁）的线程：用于区分“被桌面版占用”
    // 与“自己的 app-server 还开着会话”，避免把自己的锁误判成冲突。
    QSet<QString> m_ownLoadedThreads;

    // 写锁冲突处理参数（可用 AR_* 环境变量缩短，便于测试）。
    bool m_autoReleaseWriterLock = true;
    bool m_respectUserIdle = true;
    int m_writerPollSeconds = 30;    // 等待写锁释放的轮询间隔
    int m_writerGraceSeconds = 60;   // 检测到写锁冲突后先等这么久再考虑关闭桌面版
    int m_writerIdleSeconds = 300;   // 系统闲置达到该秒数才自动关闭桌面版
};

} // namespace arc