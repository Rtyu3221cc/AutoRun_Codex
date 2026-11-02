#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace arc {

struct RolloutState
{
    bool valid = false;
    bool interrupted = false; // 最后一次 task_complete 是 usage limit
    qint64 interruptedAt = 0; // epoch 秒
    qint64 resetAt = 0;       // 预计 5h 窗口恢复时间（epoch 秒）
    QString lastLimitMessage;
    bool running = false;     // 最近事件是 turn/task 开始且尚未结束
    qint64 lastEventAt = 0;   // epoch 秒
    qint64 completedAt = 0;   // 最后一次正常完成（非限额失败）的 turn 时间，epoch 秒
    bool hasRateLimits = false;
    qint64 fiveHourResetAt = 0;
    qint64 weeklyResetAt = 0;
    int fiveHourUsedPercent = 0;
    int weeklyUsedPercent = 0;
};

// 只读扫描 CODEX_HOME/sessions 与 archived_sessions 下的 rollout JSONL，
// 提取“因用量限额中断/正在运行”的线程状态。key = threadId。
class RolloutScanner
{
public:
    explicit RolloutScanner(QString codexHomePath = QString());
    QHash<QString, RolloutState> scan(int maxAgeHours = 48, int maxFiles = 600) const;

    static QString threadIdFromPath(const QString &path);

private:
    QString m_home;
};

} // namespace arc
