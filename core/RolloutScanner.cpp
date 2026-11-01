#include "core/RolloutScanner.h"

#include "core/CodexPaths.h"
#include "core/Types.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>

#include <algorithm>

namespace arc {
namespace {

constexpr qint64 kTailBytes = 2LL * 1024LL * 1024LL;     // 只读文件尾部 2MB
constexpr qint64 kEpochMillisThreshold = 100000000000LL; // 大于该值视为毫秒时间戳

qint64 normalizeEpoch(qint64 value)
{
    if (value <= 0)
        return 0;
    if (value > kEpochMillisThreshold)
        return value / 1000;
    return value;
}

qint64 isoToSecs(const QString &text)
{
    if (text.isEmpty())
        return 0;
    QDateTime dateTime = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dateTime.isValid())
        dateTime = QDateTime::fromString(text, Qt::ISODate);
    if (!dateTime.isValid())
        return 0;
    return normalizeEpoch(dateTime.toSecsSinceEpoch());
}

// 事件发生时间：优先 payload 内的 completed_at / started_at（epoch 秒），
// 其次退回到记录外层的 ISO-8601 timestamp。
qint64 eventSeconds(const QJsonObject &payload, const QJsonObject &record)
{
    const QJsonValue completed = payload.value(QStringLiteral("completed_at"));
    if (completed.isDouble()) {
        const qint64 secs = normalizeEpoch(static_cast<qint64>(completed.toDouble()));
        if (secs > 0)
            return secs;
    }
    const QJsonValue started = payload.value(QStringLiteral("started_at"));
    if (started.isDouble()) {
        const qint64 secs = normalizeEpoch(static_cast<qint64>(started.toDouble()));
        if (secs > 0)
            return secs;
    }
    return isoToSecs(record.value(QStringLiteral("timestamp")).toString());
}

struct ErrorInfo
{
    bool present = false;
    QString code;
    QString message;
};

ErrorInfo parseErrorInfo(const QJsonValue &value)
{
    ErrorInfo info;
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        info.present = !object.isEmpty();
        info.message = object.value(QStringLiteral("message")).toString();
        info.code = object.value(QStringLiteral("codex_error_info")).toString();
        if (info.code.isEmpty())
            info.code = object.value(QStringLiteral("code")).toString();
        if (info.message.isEmpty())
            info.message = object.value(QStringLiteral("description")).toString();
    } else if (value.isString()) {
        info.present = true;
        info.message = value.toString();
    }
    return info;
}

bool isUsageLimitError(const ErrorInfo &error)
{
    if (!error.present)
        return false;
    if (error.code.compare(QStringLiteral("usage_limit_exceeded"), Qt::CaseInsensitive) == 0)
        return true;
    if (error.code.contains(QStringLiteral("usage_limit"), Qt::CaseInsensitive)
        || error.code.contains(QStringLiteral("usageLimitExceeded"), Qt::CaseInsensitive))
        return true;
    return error.message.contains(QStringLiteral("usage limit"), Qt::CaseInsensitive)
        || error.message.contains(QStringLiteral("usage_limit"), Qt::CaseInsensitive)
        || error.message.contains(QStringLiteral("usageLimitExceeded"), Qt::CaseInsensitive);
}

// 错误文本里没有 rate_limits.resets_at 时的兜底：
// "try again at 1:51 AM" 按中断发生的本地日期换算成 epoch 秒（已过则顺延到次日）。
qint64 fallbackResetFromMessage(const QString &message, qint64 baseSec)
{
    if (message.isEmpty())
        return 0;

    static const QRegularExpression pattern(
        QStringLiteral("try again at\\s+(\\d{1,2}):(\\d{2})\\s*(AM|PM)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(message);
    if (!match.hasMatch())
        return 0;

    int hour = match.captured(1).toInt();
    const int minute = match.captured(2).toInt();
    const QString suffix = match.captured(3).toUpper();
    if (hour < 1 || hour > 12 || minute < 0 || minute > 59)
        return 0;

    if (suffix == QStringLiteral("AM")) {
        if (hour == 12)
            hour = 0;
    } else if (hour != 12) {
        hour += 12;
    }

    const QDateTime base = baseSec > 0 ? QDateTime::fromSecsSinceEpoch(baseSec)
                                       : QDateTime::currentDateTime();
    QDateTime candidate(base.date(), QTime(hour, minute));
    if (!candidate.isValid())
        return 0;
    if (candidate.toSecsSinceEpoch() <= base.toSecsSinceEpoch())
        candidate = candidate.addDays(1);
    return candidate.toSecsSinceEpoch();
}

void applyRateLimitWindow(const QJsonObject &window, bool primarySlot, RolloutState &state,
                          bool &sawWindow)
{
    if (window.isEmpty())
        return;
    sawWindow = true;

    const qint64 windowMinutes =
        static_cast<qint64>(window.value(QStringLiteral("window_minutes")).toDouble());
    int usedPercent = qRound(window.value(QStringLiteral("used_percent")).toDouble());
    if (usedPercent < 0)
        usedPercent = 0;
    if (usedPercent > 100)
        usedPercent = 100;

    qint64 resetsAt =
        normalizeEpoch(static_cast<qint64>(window.value(QStringLiteral("resets_at")).toDouble()));
    if (resetsAt <= 0) {
        // 兼容只给相对秒数的快照
        const qint64 resetsIn =
            static_cast<qint64>(window.value(QStringLiteral("resets_in_seconds")).toDouble());
        if (resetsIn > 0)
            resetsAt = nowSec() + resetsIn;
    }

    const bool fiveHour = windowMinutes == 300 || (windowMinutes <= 0 && primarySlot);
    if (fiveHour) {
        state.fiveHourResetAt = resetsAt;
        state.fiveHourUsedPercent = usedPercent;
    } else {
        state.weeklyResetAt = resetsAt;
        state.weeklyUsedPercent = usedPercent;
    }
}

void applyRateLimits(const QJsonObject &rateLimits, RolloutState &state)
{
    if (rateLimits.isEmpty())
        return;
    bool sawWindow = false;
    applyRateLimitWindow(rateLimits.value(QStringLiteral("primary")).toObject(), true, state,
                         sawWindow);
    applyRateLimitWindow(rateLimits.value(QStringLiteral("secondary")).toObject(), false, state,
                         sawWindow);
    if (sawWindow)
        state.hasRateLimits = true;
}

bool parseRolloutFile(const QString &path, qint64 modifiedAt, RolloutState &state)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const qint64 fileSize = file.size();
    if (fileSize <= 0)
        return false;

    const qint64 begin = fileSize > kTailBytes ? fileSize - kTailBytes : 0;
    if (!file.seek(begin)) {
        file.close();
        return false;
    }
    QByteArray tail = file.read(fileSize - begin);
    file.close();
    if (tail.isEmpty())
        return false;

    if (begin > 0) {
        const qsizetype firstNewline = tail.indexOf('\n');
        if (firstNewline < 0)
            return false; // 尾部只有半行，无法得到完整事件
        tail.remove(0, firstNewline + 1);
    }

    bool parsedAny = false;
    bool sawLifecycle = false;
    bool lastLifecycleWasStart = false;
    bool lastRecordWasTerminal = false;
    qint64 fallbackResetAt = 0;

    const QList<QByteArray> lines = tail.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError jsonError{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &jsonError);
        if (jsonError.error != QJsonParseError::NoError || !document.isObject())
            continue; // 容错：坏行直接跳过

        const QJsonObject record = document.object();
        const QJsonObject payload = record.value(QStringLiteral("payload")).toObject();
        const QString payloadType = payload.value(QStringLiteral("type")).toString();
        const bool hasRateLimitsField = payload.contains(QStringLiteral("rate_limits"));
        if (payloadType.isEmpty() && !hasRateLimitsField)
            continue; // session_meta 等非事件记录

        parsedAny = true;
        const qint64 eventSec = eventSeconds(payload, record);
        if (eventSec > state.lastEventAt)
            state.lastEventAt = eventSec;

        if (payloadType == QStringLiteral("task_started")
            || payloadType == QStringLiteral("turn_started")) {
            sawLifecycle = true;
            lastLifecycleWasStart = true;
            lastRecordWasTerminal = false;
            state.running = true;
            state.interrupted = false; // 新 turn 已开始，说明从限额中断中恢复
        } else if (payloadType == QStringLiteral("task_complete")
                   || payloadType == QStringLiteral("turn_complete")) {
            sawLifecycle = true;
            lastLifecycleWasStart = false;
            lastRecordWasTerminal = true;
            state.running = false;
            const ErrorInfo error = parseErrorInfo(payload.value(QStringLiteral("error")));
            if (isUsageLimitError(error)) {
                state.interrupted = true;
                if (eventSec > 0)
                    state.interruptedAt = eventSec;
                if (!error.message.isEmpty())
                    state.lastLimitMessage = error.message.trimmed();
                const qint64 fallback =
                    fallbackResetFromMessage(error.message, state.interruptedAt);
                if (fallback > 0)
                    fallbackResetAt = fallback;
            } else {
                state.interrupted = false;
            }
        } else if (payloadType == QStringLiteral("turn_aborted")) {
            sawLifecycle = true;
            lastLifecycleWasStart = false;
            lastRecordWasTerminal = true;
            state.running = false;
        } else {
            // 工具调用/推理/token 等事件：turn 仍在进行中，如果尾部没有
            // 生命周期事件（大文件只读尾部，可能错过 task_started），
            // 用“最后一条不是结束事件 + 文件最近仍在写入”来推断运行中。
            lastRecordWasTerminal = false;
        }

        if (hasRateLimitsField)
            applyRateLimits(payload.value(QStringLiteral("rate_limits")).toObject(), state);
    }

    if (!parsedAny)
        return false;

    if (!sawLifecycle && !lastRecordWasTerminal && modifiedAt > 0) {
        const qint64 age = nowSec() - modifiedAt;
        if (age >= 0 && age <= 180) {
            state.running = true;
            state.interrupted = false;
        }
    }

    state.valid = true;
    state.resetAt = state.fiveHourResetAt;
    if (state.resetAt <= 0)
        state.resetAt = fallbackResetAt;
    if (state.resetAt <= 0)
        state.resetAt = state.weeklyResetAt;

    // 5h 窗口快照可能来自上一个窗口：若早于中断时间，说明已过期，
    // 此时优先使用错误文本里的“try again at …”，否则退回周窗口/置零。
    if (state.interrupted && state.interruptedAt > 0 && state.resetAt > 0
        && state.resetAt < state.interruptedAt) {
        if (fallbackResetAt > state.interruptedAt)
            state.resetAt = fallbackResetAt;
        else if (state.weeklyResetAt > state.interruptedAt)
            state.resetAt = state.weeklyResetAt;
        else
            state.resetAt = 0;
    }
    return true;
}

} // namespace

RolloutScanner::RolloutScanner(QString codexHomePath)
    : m_home(codexHomePath.isEmpty() ? arc::codexHome() : QDir::cleanPath(codexHomePath))
{
}

QHash<QString, RolloutState> RolloutScanner::scan(int maxAgeHours, int maxFiles) const
{
    QHash<QString, RolloutState> states;
    if (m_home.isEmpty() || maxFiles <= 0)
        return states;

    struct Candidate
    {
        QString path;
        qint64 modifiedAt = 0;
    };

    const qint64 cutoff =
        maxAgeHours > 0 ? nowSec() - static_cast<qint64>(maxAgeHours) * 3600 : 0;
    QList<Candidate> candidates;

    const QStringList subdirectories{QStringLiteral("sessions"),
                                     QStringLiteral("archived_sessions")};
    for (const QString &subdirectory : subdirectories) {
        const QString root = QDir(m_home).filePath(subdirectory);
        if (!QFileInfo(root).isDir())
            continue;
        QDirIterator iterator(root, QStringList() << QStringLiteral("*.jsonl"), QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            iterator.next();
            const QFileInfo info = iterator.fileInfo();
            const qint64 modifiedAt = info.lastModified().toSecsSinceEpoch();
            if (cutoff > 0 && modifiedAt < cutoff)
                continue;
            Candidate candidate;
            candidate.path = info.absoluteFilePath();
            candidate.modifiedAt = modifiedAt;
            candidates.append(candidate);
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &left, const Candidate &right) {
                         return left.modifiedAt > right.modifiedAt;
                     });
    if (candidates.size() > maxFiles)
        candidates = candidates.mid(0, maxFiles);

    for (const Candidate &candidate : candidates) {
        const QString threadId = threadIdFromPath(candidate.path);
        if (threadId.isEmpty() || states.contains(threadId))
            continue; // 同一 thread 只保留 mtime 最新的 rollout

        RolloutState state;
        if (!parseRolloutFile(candidate.path, candidate.modifiedAt, state))
            continue;
        if (state.lastEventAt <= 0)
            state.lastEventAt = candidate.modifiedAt; // 事件缺少时间戳时退回文件 mtime
        states.insert(threadId, state);
    }

    return states;
}

QString RolloutScanner::threadIdFromPath(const QString &path)
{
    const QString baseName = QFileInfo(path).completeBaseName();
    if (baseName.isEmpty())
        return QString();

    // rollout-<date>-<uuid>.jsonl：取文件名里最后一个 UUID，不依赖读取文件头。
    static const QRegularExpression uuidPattern(QStringLiteral(
        "([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})"));
    QString threadId;
    QRegularExpressionMatchIterator matches = uuidPattern.globalMatch(baseName);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        threadId = match.captured(1);
    }
    if (!threadId.isEmpty())
        return threadId;
    return baseName;
}

} // namespace arc
