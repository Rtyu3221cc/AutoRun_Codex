#pragma once

#include "core/Types.h"

#include <QList>
#include <QObject>
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
    // 收到 ThreadCatalog 的线程状态后调用：若线程被限额打断且已启用看守，则安排到 resetAt。
    void notifyThreadState(const ThreadInfo &info);

signals:
    void jobsChanged();
    void logMessage(const QString &message);
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

    CodexClient *m_client = nullptr;
    QTimer *m_timer = nullptr;
    QList<ResumeJob> m_jobs;
    RateLimitSnapshot m_limits;
    int m_runningCount = 0;
    int m_resetWaitSeconds = 150;
    int m_retryAttempts = 3;
    int m_retryDelaySeconds = 120;
};

} // namespace arc