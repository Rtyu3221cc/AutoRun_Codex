#pragma once

#include "core/RolloutScanner.h"
#include "core/Types.h"

#include <QList>
#include <QObject>
#include <QString>

namespace arc {

class CodexClient;

// 合并 app-server 的 thread/list 元数据与 rollout 扫描结果，
// 供 UI 展示“进行中 / 待续跑 / 历史”对话。
class ThreadCatalog : public QObject
{
    Q_OBJECT

public:
    explicit ThreadCatalog(CodexClient *client, QObject *parent = nullptr);

    void refresh(int maxThreads = 120);
    QList<ThreadInfo> threads() const { return m_threads; }
    ThreadInfo threadById(const QString &id) const;
    qint64 lastRefreshAt() const { return m_lastRefreshAt; }

signals:
    void threadsUpdated(const QList<arc::ThreadInfo> &threads);
    void errorOccurred(const QString &message);

private:
    void fetchPage(int limit, const QString &cursor, QList<ThreadInfo> accumulated, int remaining);
    void finishRefresh(QList<ThreadInfo> threads);

    CodexClient *m_client = nullptr;
    RolloutScanner m_scanner;
    QList<ThreadInfo> m_threads;
    qint64 m_lastRefreshAt = 0;
    bool m_refreshing = false;
};

} // namespace arc
