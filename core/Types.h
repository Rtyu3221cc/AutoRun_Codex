#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace arc {

inline qint64 nowSec() { return QDateTime::currentSecsSinceEpoch(); }

inline QString formatEpoch(qint64 seconds)
{
    if (seconds <= 0)
        return QStringLiteral("-");
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString formatEpochShort(qint64 seconds)
{
    if (seconds <= 0)
        return QStringLiteral("-");
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("MM-dd HH:mm"));
}

inline QString formatCountdown(qint64 targetSec)
{
    const qint64 diff = targetSec - nowSec();
    if (diff <= 0)
        return QStringLiteral("已到期");
    const qint64 mins = (diff + 59) / 60;
    if (mins < 60)
        return QStringLiteral("%1 分钟").arg(mins);
    if (mins < 24 * 60) {
        // 60 分钟以上、24 小时以内：用一位小数的小时表示，例如 "2.5 小时"
        if (mins % 60 == 0)
            return QStringLiteral("%1 小时").arg(mins / 60);
        return QStringLiteral("%1 小时").arg(QString::number(mins / 60.0, 'f', 1));
    }
    // 超过 24 小时：显示天 + 小时，例如 "6 天 3 小时"
    const qint64 days = mins / (24 * 60);
    const qint64 hours = (mins % (24 * 60)) / 60;
    if (hours == 0)
        return QStringLiteral("%1 天").arg(days);
    return QStringLiteral("%1 天 %2 小时").arg(days).arg(hours);
}

// ---------------------------------------------------------------------------
// Codex quota
// ---------------------------------------------------------------------------

struct RateLimitWindow
{
    bool valid = false;
    int usedPercent = 0;
    qint64 windowDurationMins = 0; // 300 = 5 小时窗口, 10080 = 每周窗口
    qint64 resetsAt = 0;           // epoch seconds
};

struct ResetCredit
{
    QString id;
    QString title;
    QString description;
    QString status;
    qint64 grantedAt = 0;
    qint64 expiresAt = 0;
};

struct RateLimitSnapshot
{
    bool valid = false;
    QString accountId;
    QString planType;
    QString limitId;
    QString limitName;
    bool ordinaryUsageAllowed = true;
    bool hasOrdinaryUsageAllowed = false;
    RateLimitWindow primary;   // 通常为 5h 窗口
    RateLimitWindow secondary; // 通常为每周窗口
    int resetCreditsAvailable = -1;
    QList<ResetCredit> resetCredits;
    qint64 readAtMs = 0;
};

// ---------------------------------------------------------------------------
// 重置卡到期预警
// ---------------------------------------------------------------------------

// 最近一张“可用”重置卡的到期时间（epoch 秒）；没有可用到期信息时返回 0。
inline qint64 nearestResetCreditExpiry(const RateLimitSnapshot &snapshot)
{
    qint64 nearest = 0;
    for (const ResetCredit &credit : snapshot.resetCredits) {
        const QString status = credit.status.trimmed().toLower();
        if (!status.isEmpty() && status != QStringLiteral("available"))
            continue;
        if (credit.expiresAt <= 0)
            continue;
        if (nearest <= 0 || credit.expiresAt < nearest)
            nearest = credit.expiresAt;
    }
    return nearest;
}

// 重置卡到期紧急程度：按最近一张可用重置卡的剩余时间分档。
//   < 2 天 Critical（“可用 x 张”红色 + 周用量旁橙色小点）
//   < 7 天 Near（黄色，无小点）
//   >= 7 天 Far（绿色，无小点）
enum class ResetCreditUrgency { Unknown, Far, Near, Critical };

inline ResetCreditUrgency resetCreditUrgency(const RateLimitSnapshot &snapshot)
{
    if (snapshot.resetCreditsAvailable <= 0)
        return ResetCreditUrgency::Unknown;
    const qint64 expiry = nearestResetCreditExpiry(snapshot);
    if (expiry <= 0)
        return ResetCreditUrgency::Unknown;
    const qreal daysLeft = static_cast<qreal>(expiry - nowSec()) / 86400.0;
    if (daysLeft < 2.0)
        return ResetCreditUrgency::Critical;
    if (daysLeft < 7.0)
        return ResetCreditUrgency::Near;
    return ResetCreditUrgency::Far;
}

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------

struct ThreadInfo
{
    QString id;
    QString name;
    QString preview;
    QString cwd;
    QString statusType; // notLoaded / idle / active / systemError
    QString model;
    QString source;
    qint64 updatedAt = 0;
    qint64 recencyAt = 0;
    bool archived = false;

    // 由 RolloutScanner 补充
    bool interrupted = false; // 最后一次 turn 因用量限额失败
    qint64 interruptedAt = 0;
    qint64 resetAt = 0; // 预计额度恢复时间（epoch 秒）
    bool running = false;
    qint64 lastEventAt = 0;
    qint64 completedAt = 0; // 最后一次“正常完成”的 turn 时间（epoch 秒；0=未知）
    QString lastLimitMessage;

    bool hasJob = false;
    QString jobSummary;
};

// ---------------------------------------------------------------------------
// 悬浮窗“活动指示灯”
// ---------------------------------------------------------------------------

// 全局活动状态（按优先级取最高的一项；不是每个对话单独显示）。
enum class ActivityState {
    Idle,          // 没有在活动的对话（灰色、不呼吸）
    Running,       // 有对话正在运行/执行（绿黄呼吸）
    WaitingReply,  // 有对话已开始 turn、正在等模型回复/长时间思考（黄色呼吸）
    Interrupted,   // 有对话异常中断（限额中断等，红色呼吸）
    Completed,     // 最近有对话正常结束（蓝色常亮）
    RecentlyEnded  // 最近有对话活动停止、但无法确认是否正常完成（绿色常亮，兜底）
};

inline QString activityStateText(ActivityState state, int count)
{
    const int n = qMax(0, count);
    switch (state) {
    case ActivityState::Running:
        return n > 1 ? QStringLiteral("运行中 %1").arg(n) : QStringLiteral("运行中");
    case ActivityState::WaitingReply:
        return QStringLiteral("等待回复");
    case ActivityState::Interrupted:
        return n > 1 ? QStringLiteral("异常中断 %1").arg(n) : QStringLiteral("异常中断");
    case ActivityState::Completed:
        return n > 1 ? QStringLiteral("刚完成 %1").arg(n) : QStringLiteral("刚完成");
    case ActivityState::RecentlyEnded:
        return n > 1 ? QStringLiteral("最近结束 %1").arg(n) : QStringLiteral("最近结束");
    case ActivityState::Idle:
        break;
    }
    return QStringLiteral("无活动对话");
}

// ---------------------------------------------------------------------------
// Jobs
// ---------------------------------------------------------------------------

enum class ResumeMode { Once, Continuous };
enum class JobState { Disabled, Waiting, Ready, Running, Succeeded, Failed, Paused };

inline QString resumeModeText(ResumeMode mode)
{
    return mode == ResumeMode::Continuous ? QStringLiteral("持续看守") : QStringLiteral("单次");
}

inline QString jobStateText(JobState state)
{
    switch (state) {
    case JobState::Disabled: return QStringLiteral("未启用");
    case JobState::Waiting:  return QStringLiteral("等待重置");
    case JobState::Ready:    return QStringLiteral("就绪");
    case JobState::Running:  return QStringLiteral("续跑中");
    case JobState::Succeeded:return QStringLiteral("已续跑");
    case JobState::Failed:   return QStringLiteral("失败");
    case JobState::Paused:   return QStringLiteral("已暂停");
    }
    return QStringLiteral("未知");
}

struct ResumeJob
{
    QString threadId;
    QString threadName;
    QString prompt;
    ResumeMode mode = ResumeMode::Once;
    bool enabled = false;
    JobState state = JobState::Disabled;
    int attempts = 0;      // 累计续跑次数
    int cycleAttempts = 0; // 本轮触发内的尝试次数（失败重试用）
    bool retryPending = false; // 当前是“失败后的等待重试”，期间不再被线程状态刷新重排
    qint64 nextTriggerAt = 0;
    qint64 lastRunAt = 0;
    QString lastError;
    QString turnId;

    // Codex 桌面版占着 thread-writer-lock 时进入“等待写锁释放”状态：
    // 不计入失败次数、不亮红灯；锁一释放就自动续跑。
    bool writerBlocked = false;
    qint64 writerBlockedSince = 0;
    bool writerReleaseAttempted = false; // 本轮阻塞是否已经尝试关闭过桌面版
};

} // namespace arc