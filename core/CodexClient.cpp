#include "core/CodexClient.h"

#include "core/CodexPaths.h"
#include "core/JsonRpcProcess.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

namespace arc {
namespace {

QString errorText(const QJsonObject &error)
{
    if (error.isEmpty())
        return {};
    QString message = error.value(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = QString::fromUtf8(QJsonDocument(error).toJson(QJsonDocument::Compact));
    return message;
}

RateLimitWindow parseWindow(const QJsonValue &value)
{
    RateLimitWindow window;
    if (!value.isObject())
        return window;
    const QJsonObject object = value.toObject();
    if (!object.contains(QStringLiteral("usedPercent")))
        return window;
    window.valid = true;
    window.usedPercent = object.value(QStringLiteral("usedPercent")).toInt();
    window.windowDurationMins = static_cast<qint64>(object.value(QStringLiteral("windowDurationMins")).toDouble());
    window.resetsAt = static_cast<qint64>(object.value(QStringLiteral("resetsAt")).toDouble());
    return window;
}

} // namespace

CodexClient::CodexClient(QObject *parent)
    : QObject(parent)
    , m_rpc(new JsonRpcProcess(this))
{
    connect(m_rpc, &JsonRpcProcess::processStarted, this, [this]() { initialize(); });
    connect(m_rpc, &JsonRpcProcess::processStopped, this, &CodexClient::onRpcStopped);
    connect(m_rpc, &JsonRpcProcess::errorOccurred, this, [this](const QString &message) {
        m_lastError = message;
        emit logMessage(QStringLiteral("app-server: ") + message);
    });
    connect(m_rpc, &JsonRpcProcess::notification, this, &CodexClient::onRpcNotification);
}

CodexClient::~CodexClient()
{
    stop();
}

void CodexClient::start(const QString &cliPath)
{
    m_stopping = false;
    m_cliPath = cliPath.isEmpty() ? findCodexCli() : cliPath;
    if (m_cliPath.isEmpty()) {
        m_lastError = QStringLiteral("未找到 codex.exe");
        qWarning().noquote() << "[client] 未找到 codex.exe";
        emit connectionStateChanged(false, m_lastError);
        return;
    }
    qInfo().noquote() << "[client] 使用 Codex CLI:" << m_cliPath;
    emit logMessage(QStringLiteral("使用 Codex CLI: ") + m_cliPath);
    m_ready = false;
    m_rpc->setExecutable(m_cliPath);
    if (!m_rpc->start())
        emit connectionStateChanged(false, QStringLiteral("无法启动 app-server"));
}

void CodexClient::stop()
{
    m_stopping = true;
    m_ready = false;
    m_rpc->stop();
}

void CodexClient::initialize()
{
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("auto-run-codex"));
    clientInfo.insert(QStringLiteral("title"), QStringLiteral("AutoRun Codex"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("0.1.0"));

    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("experimentalApi"), true);

    QJsonObject params;
    params.insert(QStringLiteral("clientInfo"), clientInfo);
    params.insert(QStringLiteral("capabilities"), capabilities);

    m_rpc->request(QStringLiteral("initialize"), params, [this](const QJsonObject &, const QJsonObject &error) {
        if (!error.isEmpty()) {
            m_lastError = errorText(error);
            qWarning().noquote() << "[client] initialize 失败:" << m_lastError;
            emit connectionStateChanged(false, QStringLiteral("initialize 失败: ") + m_lastError);
            return;
        }
        m_rpc->notify(QStringLiteral("initialized"));
        m_ready = true;
        m_reconnectAttempts = 0;
        m_lastError.clear();
        qInfo() << "[client] app-server 已连接";
        emit ready();
        emit connectionStateChanged(true, QStringLiteral("已连接"));
        emit logMessage(QStringLiteral("app-server 已连接"));
    }, 20000);
}

void CodexClient::scheduleReconnect()
{
    if (m_stopping)
        return;
    ++m_reconnectAttempts;
    const int delay = qMin(30000, 2000 * m_reconnectAttempts);
    emit logMessage(QStringLiteral("app-server 断开，%1 秒后重连…").arg(delay / 1000));
    QTimer::singleShot(delay, this, [this]() {
        if (!m_stopping && !m_ready)
            m_rpc->start();
    });
}

void CodexClient::readRateLimits(RateLimitsCallback callback)
{
    if (!m_ready) {
        if (callback)
            callback(RateLimitSnapshot{}, QStringLiteral("app-server 未连接"));
        return;
    }
    m_rpc->request(QStringLiteral("account/rateLimits/read"), QJsonObject(),
                   [this, callback](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty()) {
            if (callback)
                callback(RateLimitSnapshot{}, errorText(error));
            return;
        }
        const RateLimitSnapshot snapshot = parseRateLimits(result);
        emit rateLimitsUpdated(snapshot);
        if (callback)
            callback(snapshot, QString());
    }, 30000);
}

RateLimitSnapshot CodexClient::parseRateLimits(const QJsonObject &result) const
{
    RateLimitSnapshot snapshot;
    snapshot.valid = true;
    snapshot.accountId = result.value(QStringLiteral("accountId")).toString();
    snapshot.readAtMs = QDateTime::currentMSecsSinceEpoch();
    if (result.contains(QStringLiteral("ordinaryUsageAllowed"))) {
        snapshot.hasOrdinaryUsageAllowed = true;
        snapshot.ordinaryUsageAllowed = result.value(QStringLiteral("ordinaryUsageAllowed")).toBool(true);
    }

    QJsonObject rateLimits = result.value(QStringLiteral("rateLimits")).toObject();
    if (rateLimits.isEmpty()) {
        const QJsonObject byId = result.value(QStringLiteral("rateLimitsByLimitId")).toObject();
        if (byId.contains(QStringLiteral("codex")))
            rateLimits = byId.value(QStringLiteral("codex")).toObject();
        else if (!byId.isEmpty())
            rateLimits = byId.constBegin().value().toObject();
    }
    snapshot.limitId = rateLimits.value(QStringLiteral("limitId")).toString();
    snapshot.limitName = rateLimits.value(QStringLiteral("limitName")).toString();
    snapshot.planType = rateLimits.value(QStringLiteral("planType")).toString();
    snapshot.primary = parseWindow(rateLimits.value(QStringLiteral("primary")));
    snapshot.secondary = parseWindow(rateLimits.value(QStringLiteral("secondary")));

    const QJsonObject credits = result.value(QStringLiteral("rateLimitResetCredits")).toObject();
    if (!credits.isEmpty()) {
        snapshot.resetCreditsAvailable = credits.value(QStringLiteral("availableCount")).toInt(-1);
        const QJsonArray creditArray = credits.value(QStringLiteral("credits")).toArray();
        for (const QJsonValue &value : creditArray) {
            const QJsonObject object = value.toObject();
            ResetCredit credit;
            credit.id = object.value(QStringLiteral("id")).toString();
            credit.title = object.value(QStringLiteral("title")).toString();
            credit.description = object.value(QStringLiteral("description")).toString();
            credit.status = object.value(QStringLiteral("status")).toString();
            credit.grantedAt = static_cast<qint64>(object.value(QStringLiteral("grantedAt")).toDouble());
            credit.expiresAt = static_cast<qint64>(object.value(QStringLiteral("expiresAt")).toDouble());
            snapshot.resetCredits.append(credit);
        }
    }
    return snapshot;
}

void CodexClient::listThreads(int limit, const QString &cursor, ThreadListCallback callback)
{
    if (!m_ready) {
        if (callback)
            callback({}, QString(), QStringLiteral("app-server 未连接"));
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("limit"), limit);
    if (!cursor.isEmpty())
        params.insert(QStringLiteral("cursor"), cursor);
    m_rpc->request(QStringLiteral("thread/list"), params,
                   [this, callback](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty()) {
            if (callback)
                callback({}, QString(), errorText(error));
            return;
        }
        QList<ThreadInfo> threads;
        const QJsonArray data = result.value(QStringLiteral("data")).toArray();
        for (const QJsonValue &value : data) {
            if (!value.isObject())
                continue;
            ThreadInfo info = parseThread(value.toObject());
            if (!info.id.isEmpty())
                threads.append(info);
        }
        const QString next = result.value(QStringLiteral("nextCursor")).toString();
        if (callback)
            callback(threads, next, QString());
    }, 30000);
}

ThreadInfo CodexClient::parseThread(const QJsonObject &object) const
{
    ThreadInfo info;
    info.id = object.value(QStringLiteral("id")).toString();
    info.name = object.value(QStringLiteral("name")).toString();
    info.preview = object.value(QStringLiteral("preview")).toString();
    info.cwd = object.value(QStringLiteral("cwd")).toString();
    info.model = object.value(QStringLiteral("model")).toString();
    info.source = object.value(QStringLiteral("source")).toString();
    info.updatedAt = static_cast<qint64>(object.value(QStringLiteral("updatedAt")).toDouble());
    info.recencyAt = static_cast<qint64>(object.value(QStringLiteral("recencyAt")).toDouble());
    const QJsonObject status = object.value(QStringLiteral("status")).toObject();
    info.statusType = status.value(QStringLiteral("type")).toString();
    if (info.statusType.isEmpty())
        info.statusType = QStringLiteral("notLoaded");
    return info;
}

void CodexClient::resumeThread(const QString &threadId, SimpleCallback callback)
{
    if (!m_ready) {
        if (callback)
            callback(false, QStringLiteral("app-server 未连接"));
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    m_rpc->request(QStringLiteral("thread/resume"), params,
                   [callback](const QJsonObject &, const QJsonObject &error) {
        if (callback)
            callback(error.isEmpty(), errorText(error));
    }, 60000);
}

void CodexClient::startTurn(const QString &threadId, const QString &text, TurnStartCallback callback)
{
    if (!m_ready) {
        if (callback)
            callback(false, QString(), QStringLiteral("app-server 未连接"));
        return;
    }
    QJsonObject input;
    input.insert(QStringLiteral("type"), QStringLiteral("text"));
    input.insert(QStringLiteral("text"), text);
    QJsonArray inputs;
    inputs.append(input);

    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    params.insert(QStringLiteral("input"), inputs);
    params.insert(QStringLiteral("approvalPolicy"), QStringLiteral("never"));

    m_rpc->request(QStringLiteral("turn/start"), params,
                   [callback](const QJsonObject &result, const QJsonObject &error) {
        if (!error.isEmpty()) {
            if (callback)
                callback(false, QString(), errorText(error));
            return;
        }
        const QString turnId = result.value(QStringLiteral("turn")).toObject()
                                   .value(QStringLiteral("id")).toString();
        if (callback)
            callback(true, turnId, QString());
    }, 60000);
}

void CodexClient::archiveThread(const QString &threadId, SimpleCallback callback)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    m_rpc->request(QStringLiteral("thread/archive"), params,
                   [callback](const QJsonObject &, const QJsonObject &error) {
        if (callback)
            callback(error.isEmpty(), errorText(error));
    }, 30000);
}

void CodexClient::deleteThread(const QString &threadId, SimpleCallback callback)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    m_rpc->request(QStringLiteral("thread/delete"), params,
                   [callback](const QJsonObject &, const QJsonObject &error) {
        if (callback)
            callback(error.isEmpty(), errorText(error));
    }, 30000);
}

void CodexClient::interruptTurn(const QString &threadId, const QString &turnId, SimpleCallback callback)
{
    if (!m_ready) {
        if (callback)
            callback(false, QStringLiteral("app-server 未连接"));
        return;
    }
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    params.insert(QStringLiteral("turnId"), turnId);
    m_rpc->request(QStringLiteral("turn/interrupt"), params,
                   [callback](const QJsonObject &, const QJsonObject &error) {
        if (callback)
            callback(error.isEmpty(), errorText(error));
    }, 30000);
}

void CodexClient::getGoal(const QString &threadId,
                          std::function<void(const QJsonObject &, const QString &)> callback)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    m_rpc->request(QStringLiteral("thread/goal/get"), params,
                   [callback](const QJsonObject &result, const QJsonObject &error) {
        if (callback)
            callback(result, errorText(error));
    }, 30000);
}

void CodexClient::setGoalStatus(const QString &threadId, const QString &status, SimpleCallback callback)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    params.insert(QStringLiteral("status"), status);
    m_rpc->request(QStringLiteral("thread/goal/set"), params,
                   [callback](const QJsonObject &, const QJsonObject &error) {
        if (callback)
            callback(error.isEmpty(), errorText(error));
    }, 30000);
}

void CodexClient::onRpcNotification(const QString &method, const QJsonObject &params)
{
    if (method == QLatin1String("account/rateLimits/updated")) {
        readRateLimits(nullptr);
    } else if (method == QLatin1String("thread/status/changed")) {
        const QString threadId = params.value(QStringLiteral("threadId")).toString();
        const QJsonObject status = params.value(QStringLiteral("status")).toObject();
        const QString statusType = status.value(QStringLiteral("type")).toString();
        QStringList flags;
        const QJsonArray activeFlags = status.value(QStringLiteral("activeFlags")).toArray();
        for (const QJsonValue &value : activeFlags)
            flags.append(value.toString());
        emit threadStatusChanged(threadId, statusType, flags);
    } else if (method == QLatin1String("turn/started")) {
        const QString threadId = params.value(QStringLiteral("threadId")).toString();
        const QString turnId = params.value(QStringLiteral("turn")).toObject()
                                   .value(QStringLiteral("id")).toString();
        emit turnStarted(threadId, turnId);
    } else if (method == QLatin1String("turn/completed")) {
        const QString threadId = params.value(QStringLiteral("threadId")).toString();
        const QJsonObject turn = params.value(QStringLiteral("turn")).toObject();
        emit turnCompleted(threadId, turn);
    } else if (method == QLatin1String("thread/goal/updated")) {
        emit logMessage(QStringLiteral("goal 状态更新"));
    }
}

void CodexClient::onRpcStopped(int exitCode)
{
    const bool wasReady = m_ready;
    m_ready = false;
    qWarning() << "[client] app-server 已退出, code =" << exitCode;
    emit connectionStateChanged(false, QStringLiteral("app-server 已退出 (code %1)").arg(exitCode));
    if (wasReady)
        emit logMessage(QStringLiteral("app-server 已退出，准备重连"));
    scheduleReconnect();
}

} // namespace arc
