#include "core/JobEngine.h"

#include "core/CodexClient.h"
#include "core/CodexPaths.h"
#include "core/CodexWriterLock.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

namespace arc {
namespace {
QString jobsPath()
{
    return appDataDir() + QStringLiteral("/jobs.json");
}
QString defaultPrompt()
{
    return QStringLiteral("继续执行未完成的任务");
}
bool isUsageLimitText(const QString &text)
{
    return text.contains(QStringLiteral("usageLimitExceeded"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("usage limit"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("rateLimitExceeded"), Qt::CaseInsensitive);
}

// Codex 单写者模型：对话被其他进程（通常是桌面版）加载时，thread/resume 会返回
// "thread ... already has an active writer" / "thread-store conflict"。
bool isWriterLockText(const QString &text)
{
    return text.contains(QStringLiteral("active writer"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("thread-store conflict"), Qt::CaseInsensitive);
}
} // namespace

JobEngine::JobEngine(CodexClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &JobEngine::onTick);
    connect(m_client, &CodexClient::turnCompleted, this, &JobEngine::onTurnCompleted);
    // app-server 退出/重连后，之前加载的会话都不在了，清空“自己持有写锁”的记录。
    connect(m_client, &CodexClient::connectionStateChanged, this,
            [this](bool ready, const QString &) {
                if (!ready)
                    m_ownLoadedThreads.clear();
            });
    m_timer->start();

    // 开发/测试用：缩短等待时间，便于快速验证重试与失败告警。
    if (qEnvironmentVariableIsSet("AR_RETRY_DELAY_SECONDS")) {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue("AR_RETRY_DELAY_SECONDS", &ok);
        if (ok && value >= 0)
            m_retryDelaySeconds = value;
    }
    if (qEnvironmentVariableIsSet("AR_RESET_WAIT_SECONDS")) {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue("AR_RESET_WAIT_SECONDS", &ok);
        if (ok && value >= 0)
            m_resetWaitSeconds = value;
    }
    // 写锁冲突相关的测试开关（缩短轮询/宽限时间，忽略“用户是否离开”）。
    auto envInt = [](const char *name, int fallback) {
        if (!qEnvironmentVariableIsSet(name))
            return fallback;
        bool ok = false;
        const int value = qEnvironmentVariableIntValue(name, &ok);
        return (ok && value >= 0) ? value : fallback;
    };
    m_writerPollSeconds = qMax(1, envInt("AR_WRITER_POLL_SECONDS", m_writerPollSeconds));
    m_writerGraceSeconds = envInt("AR_WRITER_GRACE_SECONDS", m_writerGraceSeconds);
    m_writerIdleSeconds = envInt("AR_WRITER_IDLE_SECONDS", m_writerIdleSeconds);
    if (qEnvironmentVariableIsSet("AR_WRITER_IGNORE_USER_IDLE"))
        m_respectUserIdle = false;
    loadState();
}

void JobEngine::loadState()
{
    m_jobs.clear();
    QFile file(jobsPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QJsonArray array = doc.isArray() ? doc.array() : QJsonArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        ResumeJob job;
        job.threadId = object.value(QStringLiteral("threadId")).toString();
        job.threadName = object.value(QStringLiteral("threadName")).toString();
        job.prompt = object.value(QStringLiteral("prompt")).toString();
        if (job.prompt.isEmpty())
            job.prompt = defaultPrompt();
        job.mode = object.value(QStringLiteral("mode")).toString() == QLatin1String("continuous")
                       ? ResumeMode::Continuous
                       : ResumeMode::Once;
        job.enabled = object.value(QStringLiteral("enabled")).toBool(false);
        job.state = static_cast<JobState>(object.value(QStringLiteral("state")).toInt(static_cast<int>(JobState::Disabled)));
        job.attempts = object.value(QStringLiteral("attempts")).toInt(0);
        job.cycleAttempts = object.value(QStringLiteral("cycleAttempts")).toInt(0);
        job.retryPending = object.value(QStringLiteral("retryPending")).toBool(false);
        job.nextTriggerAt = static_cast<qint64>(object.value(QStringLiteral("nextTriggerAt")).toDouble());
        job.lastRunAt = static_cast<qint64>(object.value(QStringLiteral("lastRunAt")).toDouble());
        job.lastError = object.value(QStringLiteral("lastError")).toString();
        job.writerBlocked = object.value(QStringLiteral("writerBlocked")).toBool(false);
        job.writerBlockedSince =
                static_cast<qint64>(object.value(QStringLiteral("writerBlockedSince")).toDouble());
        job.writerReleaseAttempted =
                object.value(QStringLiteral("writerReleaseAttempted")).toBool(false);
        if (!job.threadId.isEmpty())
            m_jobs.append(job);
    }
}

void JobEngine::saveState() const
{
    QJsonArray array;
    for (const ResumeJob &job : m_jobs) {
        QJsonObject object;
        object.insert(QStringLiteral("threadId"), job.threadId);
        object.insert(QStringLiteral("threadName"), job.threadName);
        object.insert(QStringLiteral("prompt"), job.prompt);
        object.insert(QStringLiteral("mode"), job.mode == ResumeMode::Continuous
                                                   ? QStringLiteral("continuous")
                                                   : QStringLiteral("once"));
        object.insert(QStringLiteral("enabled"), job.enabled);
        object.insert(QStringLiteral("state"), static_cast<int>(job.state));
        object.insert(QStringLiteral("attempts"), job.attempts);
        object.insert(QStringLiteral("cycleAttempts"), job.cycleAttempts);
        object.insert(QStringLiteral("retryPending"), job.retryPending);
        object.insert(QStringLiteral("nextTriggerAt"), static_cast<double>(job.nextTriggerAt));
        object.insert(QStringLiteral("lastRunAt"), static_cast<double>(job.lastRunAt));
        object.insert(QStringLiteral("lastError"), job.lastError);
        object.insert(QStringLiteral("writerBlocked"), job.writerBlocked);
        object.insert(QStringLiteral("writerBlockedSince"), static_cast<double>(job.writerBlockedSince));
        object.insert(QStringLiteral("writerReleaseAttempted"), job.writerReleaseAttempted);
        array.append(object);
    }
    QDir().mkpath(appDataDir());
    QSaveFile file(jobsPath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.commit();
}

ResumeJob JobEngine::jobFor(const QString &threadId) const
{
    for (const ResumeJob &job : m_jobs) {
        if (job.threadId == threadId)
            return job;
    }
    return ResumeJob{};
}

bool JobEngine::hasJob(const QString &threadId) const
{
    for (const ResumeJob &job : m_jobs) {
        if (job.threadId == threadId)
            return true;
    }
    return false;
}

ResumeJob *JobEngine::findJob(const QString &threadId)
{
    for (ResumeJob &job : m_jobs) {
        if (job.threadId == threadId)
            return &job;
    }
    return nullptr;
}

void JobEngine::addOrUpdateJob(const ResumeJob &job)
{
    if (job.threadId.isEmpty())
        return;
    ResumeJob *existing = findJob(job.threadId);
    if (existing) {
        *existing = job;
    } else {
        m_jobs.append(job);
    }
    saveState();
    emit jobsChanged();
}

void JobEngine::removeJob(const QString &threadId)
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).threadId == threadId) {
            m_jobs.removeAt(i);
            saveState();
            emit jobsChanged();
            return;
        }
    }
}

void JobEngine::cancelJob(const QString &threadId)
{
    ResumeJob *job = findJob(threadId);
    if (!job) {
        removeJob(threadId);
        return;
    }
    const bool wasRunning = job->state == JobState::Running;
    const QString turnId = job->turnId;
    removeJob(threadId);
    if (wasRunning) {
        --m_runningCount;
        if (m_runningCount < 0)
            m_runningCount = 0;
        if (m_client && m_client->isReady() && !turnId.isEmpty()) {
            m_client->interruptTurn(threadId, turnId, [this, threadId](bool ok, const QString &error) {
                emit logMessage(ok ? QStringLiteral("已中断正在运行的续跑（%1）").arg(threadId.left(8))
                                   : QStringLiteral("中断续跑失败：%1").arg(error));
            });
        } else {
            emit logMessage(QStringLiteral("已取消看守（当前 turn 无法中断）"));
        }
    }
}

void JobEngine::setJobEnabled(const QString &threadId, bool enabled)
{
    ResumeJob *job = findJob(threadId);
    if (!job)
        return;
    job->enabled = enabled;
    job->state = enabled ? (job->state == JobState::Disabled ? JobState::Ready : job->state)
                         : JobState::Disabled;
    if (!enabled)
        job->nextTriggerAt = 0;
    saveState();
    emit jobsChanged();
}

void JobEngine::requestRunNow(const QString &threadId)
{
    ResumeJob *job = findJob(threadId);
    if (!job)
        return;
    job->enabled = true;
    job->state = JobState::Ready;
    job->nextTriggerAt = 0;
    job->cycleAttempts = 0;
    job->retryPending = false;
    job->writerBlocked = false;
    job->writerReleaseAttempted = false;
    saveState();
    emit jobsChanged();
    emit logMessage(QStringLiteral("立即续跑：%1").arg(job->threadName.isEmpty() ? job->threadId : job->threadName));
}

void JobEngine::stopAll()
{
    for (ResumeJob &job : m_jobs) {
        if (job.state == JobState::Running)
            continue;
        job.enabled = false;
        job.state = JobState::Paused;
        job.nextTriggerAt = 0;
    }
    saveState();
    emit jobsChanged();
}

void JobEngine::updateRateLimits(const RateLimitSnapshot &snapshot)
{
    m_limits = snapshot;
}

void JobEngine::setResetWaitSeconds(int seconds)
{
    m_resetWaitSeconds = qMax(0, seconds);
}

void JobEngine::setRetryAttempts(int attempts)
{
    m_retryAttempts = qMax(1, attempts);
}

void JobEngine::setAutoReleaseWriterLock(bool enabled)
{
    m_autoReleaseWriterLock = enabled;
}

void JobEngine::notifyThreadState(const ThreadInfo &info)
{
    ResumeJob *job = findJob(info.id);
    if (!job || !job->enabled)
        return;
    if (job->state == JobState::Running)
        return;
    // 连续重试失败后转为红色告警，不再自动重排；等待用户「立即续跑」或重新加入看守。
    if (job->state == JobState::Failed)
        return;
    // 正在等待“失败重试”时，不要被线程状态刷新重新算成 resetAt 计划（否则会提前/无限重试）。
    if (job->retryPending)
        return;
    // 正在等待写锁释放时同样不要被刷新重排（由 processWriterBlocked 自己决定何时续跑）。
    if (job->writerBlocked)
        return;
    if (!info.interrupted)
        return;
    qint64 reset = info.resetAt;
    if (reset <= 0 && m_limits.primary.valid)
        reset = m_limits.primary.resetsAt;
    if (reset <= 0)
        return;
    const qint64 target = reset + qMax(60, m_resetWaitSeconds);
    if (job->nextTriggerAt == target && job->state == JobState::Waiting)
        return;
    job->state = JobState::Waiting;
    job->nextTriggerAt = target;
    job->cycleAttempts = 0;
    job->retryPending = false;
    saveState();
    emit logMessage(QStringLiteral("线程「%1」因限额中断，计划 %2 续跑（重置后再等 %3 秒，避免重置延迟）")
                        .arg(job->threadName.isEmpty() ? job->threadId : job->threadName,
                             formatEpoch(target))
                        .arg(m_resetWaitSeconds));
    emit jobsChanged();
}

qint64 JobEngine::nextResetFromLimits() const
{
    if (m_limits.primary.valid && m_limits.primary.resetsAt > 0)
        return m_limits.primary.resetsAt;
    return 0;
}

void JobEngine::onTick()
{
    if (m_runningCount >= 1)
        return; // 默认串行，避免同时续跑多个对话
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (ResumeJob &job : m_jobs) {
        if (!job.enabled)
            continue;
        if (job.state == JobState::Running)
            continue;
        if (job.state == JobState::Succeeded || job.state == JobState::Failed)
            continue;
        if (job.writerBlocked) {
            processWriterBlocked(job);
            break;
        }
        if (job.state == JobState::Waiting && job.nextTriggerAt > now)
            continue;
        if (job.state == JobState::Waiting && job.nextTriggerAt <= 0)
            continue;
        execute(job);
        break;
    }
}

bool JobEngine::isWriterLockError(const QString &error)
{
    return isWriterLockText(error);
}

// 进入/保持在“等待写锁释放”状态：不计失败次数、不亮红灯。
void JobEngine::handleWriterBlocked(ResumeJob &job, const QString &error, bool attemptCounted)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const bool first = !job.writerBlocked;
    if (first) {
        job.writerBlocked = true;
        job.writerBlockedSince = now;
        job.writerReleaseAttempted = false;
    }
    // 写锁冲突不是续跑失败：把 execute() 里刚加上的本轮计数回滚。
    if (attemptCounted && job.cycleAttempts > 0)
        --job.cycleAttempts;
    job.state = JobState::Waiting;
    job.retryPending = false;
    job.nextTriggerAt = now + m_writerPollSeconds;
    job.lastError = error;
    if (first) {
        emit logMessage(QStringLiteral(
                                "「%1」的对话正被 Codex 桌面版占用（thread 写锁），本次不计失败次数；"
                                "锁释放后会自动续跑%2")
                                .arg(job.threadName.isEmpty() ? job.threadId : job.threadName,
                                     m_autoReleaseWriterLock
                                             ? QStringLiteral("（若你离开电脑，将自动关闭桌面版释放写锁）")
                                             : QString()));
    }
    saveState();
    emit jobStateChanged(job.threadId);
    emit jobsChanged();
}

// 等待写锁期间：轮询锁是否释放；必要时（无人值守且已离开）关闭 Codex 桌面版。
void JobEngine::processWriterBlocked(ResumeJob &job)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    if (!isThreadWriterLockHeld(job.threadId)) {
        // 锁已释放：立刻恢复续跑。
        job.writerBlocked = false;
        job.writerReleaseAttempted = false;
        job.state = JobState::Ready;
        job.nextTriggerAt = 0;
        job.lastError.clear();
        saveState();
        emit jobsChanged();
        emit logMessage(QStringLiteral("「%1」的写锁已释放，立即续跑")
                                .arg(job.threadName.isEmpty() ? job.threadId : job.threadName));
        execute(job);
        return;
    }

    // 仍在锁中：保持轮询（下一次 tick 继续检查，锁释放可秒级恢复）。
    if (!m_autoReleaseWriterLock) {
        job.nextTriggerAt = now + m_writerPollSeconds;
        saveState();
        return;
    }
    if (job.writerReleaseAttempted) {
        // 已尝试过关闭桌面版：继续等锁自然释放，不再重复杀进程。
        job.nextTriggerAt = now + m_writerPollSeconds;
        saveState();
        return;
    }
    if (now - job.writerBlockedSince < m_writerGraceSeconds)
        return; // 宽限期内先等用户自己关闭

    // 目标对话仍在跑 turn（rollout 仍在写）时不能抢锁，否则会打断桌面版正在执行的任务。
    if (isThreadRecentlyActive(job.threadId, 90)) {
        if (now - job.writerBlockedSince >= m_writerGraceSeconds
            && (now - job.writerBlockedSince) % qMax(1, m_writerPollSeconds * 4) < 2) {
            emit logMessage(QStringLiteral("「%1」仍在 Codex 桌面版里运行，暂不关闭桌面版，等待其空闲")
                                    .arg(job.threadName.isEmpty() ? job.threadId : job.threadName));
        }
        job.nextTriggerAt = now + m_writerPollSeconds;
        saveState();
        return;
    }

    // 用户还在电脑前时不要突然关掉桌面版（可用 AR_WRITER_IGNORE_USER_IDLE 测试时跳过）。
    if (m_respectUserIdle && systemIdleSeconds() < static_cast<quint64>(m_writerIdleSeconds)) {
        job.nextTriggerAt = now + m_writerPollSeconds;
        saveState();
        return;
    }

    job.writerReleaseAttempted = true;
    saveState();
    emit logMessage(QStringLiteral("「%1」的写锁被 Codex 桌面版占用且已无人使用，尝试自动关闭桌面版以释放写锁…")
                            .arg(job.threadName.isEmpty() ? job.threadId : job.threadName));
    QString detail;
    const bool closed = closeCodexDesktopApp(&detail);
    if (closed) {
        emit logMessage(QStringLiteral("已自动关闭 Codex 桌面版（%1），等待写锁释放后续跑").arg(detail));
    } else {
        emit logMessage(QStringLiteral("自动关闭 Codex 桌面版失败（%1），继续等待写锁释放").arg(detail));
    }
    job.nextTriggerAt = now + 5; // 关完马上回来检查锁是否释放
    saveState();
    emit jobsChanged();
}

void JobEngine::handleFailure(ResumeJob &job, const QString &error)
{
    job.lastError = error;
    job.turnId.clear();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString name = job.threadName.isEmpty() ? job.threadId : job.threadName;
    if (job.cycleAttempts < m_retryAttempts) {
        job.state = JobState::Waiting;
        job.retryPending = true;
        job.nextTriggerAt = now + m_retryDelaySeconds;
        emit logMessage(QStringLiteral("「%1」续跑失败（本轮第 %2/%3 次）：%4；%5 秒后自动重试")
                            .arg(name)
                            .arg(job.cycleAttempts)
                            .arg(m_retryAttempts)
                            .arg(error)
                            .arg(m_retryDelaySeconds));
    } else {
        job.state = JobState::Failed;
        job.retryPending = false;
        job.nextTriggerAt = 0;
        emit logMessage(QStringLiteral("「%1」连续 %2 次续跑失败，已停止自动重试，看守灯转为红色：%3")
                            .arg(name)
                            .arg(job.cycleAttempts)
                            .arg(error));
    }
    saveState();
    emit jobStateChanged(job.threadId);
    emit jobsChanged();
}

void JobEngine::execute(ResumeJob &job)
{
    // 先探测写锁（不依赖 app-server）：被 Codex 桌面版占用时直接进入“等待写锁释放”，
    // 不发起注定失败的 thread/resume，也不消耗重试次数。
    // 注意：如果是本进程 app-server 自己加载过该线程（自己持有写锁），不能算冲突。
    if (isThreadWriterLockHeld(job.threadId) && !m_ownLoadedThreads.contains(job.threadId)) {
        handleWriterBlocked(job,
                            QStringLiteral("thread %1 already has an active writer（被 Codex 桌面版占用）")
                                    .arg(job.threadId),
                            false);
        return;
    }
    if (!m_client || !m_client->isReady()) {
        job.lastError = QStringLiteral("app-server 未连接");
        job.nextTriggerAt = QDateTime::currentSecsSinceEpoch() + 30;
        job.state = JobState::Waiting;
        job.retryPending = true;
        saveState();
        emit logMessage(QStringLiteral("app-server 未连接，30 秒后重试「%1」").arg(job.threadName));
        return;
    }

    job.state = JobState::Running;
    job.retryPending = false;
    ++job.attempts;
    ++job.cycleAttempts;
    job.lastRunAt = QDateTime::currentSecsSinceEpoch();
    job.lastError.clear();
    saveState();
    ++m_runningCount;
    emit jobStateChanged(job.threadId);
    emit jobsChanged();
    emit logMessage(QStringLiteral("开始续跑「%1」（本轮第 %2/%3 次，累计 %4 次）")
                        .arg(job.threadName.isEmpty() ? job.threadId : job.threadName)
                        .arg(job.cycleAttempts)
                        .arg(m_retryAttempts)
                        .arg(job.attempts));

    const QString threadId = job.threadId;
    const QString prompt = job.prompt.isEmpty() ? defaultPrompt() : job.prompt;

    m_client->resumeThread(threadId, [this, threadId, prompt](bool ok, const QString &error) {
        ResumeJob *job = findJob(threadId);
        if (!job)
            return;
        if (!ok) {
            --m_runningCount;
            if (isWriterLockError(error))
                handleWriterBlocked(*job, QStringLiteral("恢复线程失败：%1").arg(error), true);
            else
                handleFailure(*job, QStringLiteral("恢复线程失败：%1").arg(error));
            return;
        }
        // 本进程的 app-server 现在加载了该线程（自己持有写锁），记录下来避免误判为桌面版冲突。
        m_ownLoadedThreads.insert(threadId);
        m_client->startTurn(threadId, prompt, [this, threadId](bool turnOk, const QString &turnId,
                                                              const QString &turnError) {
            ResumeJob *job = findJob(threadId);
            if (!job)
                return;
            if (!turnOk) {
                --m_runningCount;
                handleFailure(*job, QStringLiteral("发送续跑消息失败：%1").arg(turnError));
                return;
            }
            job->turnId = turnId;
            saveState();
            emit logMessage(QStringLiteral("续跑消息已发送（turn %1）").arg(turnId.left(8)));
        });
    });
}

void JobEngine::onTurnCompleted(const QString &threadId, const QJsonObject &turn)
{
    ResumeJob *job = findJob(threadId);
    if (!job || job->state != JobState::Running)
        return;
    --m_runningCount;
    if (m_runningCount < 0)
        m_runningCount = 0;
    job->writerBlocked = false;
    job->writerReleaseAttempted = false;

    const QString status = turn.value(QStringLiteral("status")).toString();
    const QJsonObject error = turn.value(QStringLiteral("error")).toObject();
    const QString errorJson = QString::fromUtf8(QJsonDocument(error).toJson(QJsonDocument::Compact));
    const bool usageLimit = isUsageLimitText(errorJson)
        || (status.contains(QStringLiteral("limit"), Qt::CaseInsensitive));

    if (usageLimit) {
        handleFailure(*job, QStringLiteral("额度用完/仍被限额"));
        return;
    }
    job->state = JobState::Succeeded;
    job->lastError.clear();
    job->cycleAttempts = 0;
    job->retryPending = false;
    emit logMessage(QStringLiteral("「%1」续跑完成").arg(job->threadName));
    saveState();
    emit jobStateChanged(threadId);
    emit jobsChanged();
}

} // namespace arc