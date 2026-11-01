#include "core/ThreadCatalog.h"

#include "core/CodexClient.h"
#include "core/CodexPaths.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QSet>

namespace arc {
namespace {

constexpr int kPageLimit = 100; // 每页请求的 thread 数量

} // namespace

ThreadCatalog::ThreadCatalog(CodexClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_scanner(codexHome())
{
}

void ThreadCatalog::refresh(int maxThreads)
{
    if (m_refreshing)
        return; // 刷新中重复调用直接忽略
    if (!m_client) {
        emit errorOccurred(QStringLiteral("Codex 客户端不可用"));
        return;
    }

    m_refreshing = true;
    fetchPage(kPageLimit, QString(), QList<ThreadInfo>(), qMax(1, maxThreads));
}

void ThreadCatalog::fetchPage(int limit, const QString &cursor, QList<ThreadInfo> accumulated,
                              int remaining)
{
    if (!m_client) {
        m_refreshing = false;
        emit errorOccurred(QStringLiteral("Codex 客户端不可用"));
        return;
    }

    const int pageLimit = remaining > 0 ? qMin(limit, remaining) : limit;
    if (pageLimit <= 0) {
        finishRefresh(accumulated);
        return;
    }

    // 回调可能晚于 ThreadCatalog 析构，用 QPointer 保护。
    QPointer<ThreadCatalog> self(this);
    m_client->listThreads(
        pageLimit, cursor,
        [self, limit, remaining, accumulated](const QList<ThreadInfo> &page,
                                              const QString &nextCursor, const QString &error) {
            if (!self)
                return;
            if (!error.isEmpty()) {
                self->m_refreshing = false;
                emit self->errorOccurred(error);
                return;
            }

            QList<ThreadInfo> combined = accumulated;
            for (const ThreadInfo &info : page)
                combined.append(info);

            const int left = remaining - page.size();
            if (page.isEmpty() || nextCursor.isEmpty() || left <= 0) {
                self->finishRefresh(combined);
                return;
            }
            self->fetchPage(limit, nextCursor, combined, left);
        });
}

void ThreadCatalog::finishRefresh(QList<ThreadInfo> threads)
{
    // 分页过程中服务端数据可能移动，按 id 去重并保持首见顺序。
    QSet<QString> seen;
    QList<ThreadInfo> unique;
    unique.reserve(threads.size());
    for (const ThreadInfo &info : threads) {
        if (info.id.isEmpty() || seen.contains(info.id))
            continue;
        seen.insert(info.id);
        unique.append(info);
    }

    // 合并 rollout 扫描得到的“限额中断 / 运行中 / 额度重置”状态。
    const QHash<QString, RolloutState> states = m_scanner.scan();
    for (ThreadInfo &info : unique) {
        const QHash<QString, RolloutState>::const_iterator it = states.constFind(info.id);
        if (it == states.constEnd())
            continue;
        const RolloutState &state = it.value();
        info.interrupted = state.interrupted;
        info.interruptedAt = state.interruptedAt;
        info.resetAt = state.resetAt;
        info.running = state.running;
        info.lastEventAt = state.lastEventAt;
        info.lastLimitMessage = state.lastLimitMessage;
    }

    m_threads = unique;
    m_lastRefreshAt = nowSec();
    m_refreshing = false;
    emit threadsUpdated(m_threads);
}

ThreadInfo ThreadCatalog::threadById(const QString &id) const
{
    for (const ThreadInfo &info : m_threads) {
        if (info.id == id)
            return info;
    }
    return ThreadInfo{};
}

} // namespace arc
