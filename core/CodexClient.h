#pragma once

#include "core/Types.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace arc {

class JsonRpcProcess;

class CodexClient : public QObject
{
    Q_OBJECT

public:
    using RateLimitsCallback = std::function<void(const RateLimitSnapshot &snapshot, const QString &error)>;
    using ThreadListCallback = std::function<void(const QList<ThreadInfo> &threads, const QString &nextCursor,
                                                  const QString &error)>;
    using SimpleCallback = std::function<void(bool ok, const QString &error)>;
    using TurnStartCallback = std::function<void(bool ok, const QString &turnId, const QString &error)>;

    explicit CodexClient(QObject *parent = nullptr);
    ~CodexClient() override;

    void start(const QString &cliPath = QString());
    void stop();
    bool isReady() const { return m_ready; }
    QString lastError() const { return m_lastError; }

    void readRateLimits(RateLimitsCallback callback);
    void listThreads(int limit, const QString &cursor, ThreadListCallback callback);
    void resumeThread(const QString &threadId, SimpleCallback callback);
    void startTurn(const QString &threadId, const QString &text, TurnStartCallback callback);
    void archiveThread(const QString &threadId, SimpleCallback callback);
    void deleteThread(const QString &threadId, SimpleCallback callback);
    void interruptTurn(const QString &threadId, const QString &turnId, SimpleCallback callback);

    // goal（若用户使用了 Codex 目标功能）
    void getGoal(const QString &threadId, std::function<void(const QJsonObject &goal, const QString &error)> callback);
    void setGoalStatus(const QString &threadId, const QString &status, SimpleCallback callback);

signals:
    void ready();
    void connectionStateChanged(bool ready, const QString &detail);
    void rateLimitsUpdated(const arc::RateLimitSnapshot &snapshot);
    void threadStatusChanged(const QString &threadId, const QString &statusType,
                             const QStringList &activeFlags);
    void turnStarted(const QString &threadId, const QString &turnId);
    void turnCompleted(const QString &threadId, const QJsonObject &turn);
    void logMessage(const QString &message);

private slots:
    void onRpcNotification(const QString &method, const QJsonObject &params);
    void onRpcStopped(int exitCode);

private:
    void initialize();
    void scheduleReconnect();
    RateLimitSnapshot parseRateLimits(const QJsonObject &result) const;
    ThreadInfo parseThread(const QJsonObject &object) const;

    JsonRpcProcess *m_rpc = nullptr;
    QString m_cliPath;
    bool m_ready = false;
    QString m_lastError;
    int m_reconnectAttempts = 0;
    bool m_stopping = false;
};

} // namespace arc
