#include "core/JsonRpcProcess.h"

#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#include <QDebug>

namespace arc {

JsonRpcProcess::JsonRpcProcess(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &JsonRpcProcess::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &JsonRpcProcess::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) { onProcessFinished(exitCode); });
    connect(m_process, &QProcess::started, this, &JsonRpcProcess::processStarted);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray err = m_process->readAllStandardError().trimmed();
        if (!err.isEmpty())
            qWarning().noquote() << "[codex app-server]" << QString::fromUtf8(err.right(400));
    });
}

JsonRpcProcess::~JsonRpcProcess()
{
    stop();
}

void JsonRpcProcess::setExecutable(const QString &path)
{
    m_executable = path;
}

bool JsonRpcProcess::start()
{
    if (isRunning())
        return true;
    if (m_executable.isEmpty()) {
        emit errorOccurred(QStringLiteral("未找到 Codex CLI（codex.exe），请在设置中指定路径"));
        return false;
    }
    m_buffer.clear();
    m_process->setProgram(m_executable);
    m_process->setArguments({QStringLiteral("app-server"), QStringLiteral("--stdio")});
    m_process->start();
    qInfo().noquote() << "[rpc] 启动 app-server:" << m_executable;
    return true;
}

void JsonRpcProcess::stop()
{
    if (!m_process)
        return;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
    m_buffer.clear();
    failAllPending(QStringLiteral("连接已关闭"));
}

bool JsonRpcProcess::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void JsonRpcProcess::request(const QString &method, const QJsonObject &params, ResponseCallback callback,
                             int timeoutMs)
{
    if (!isRunning()) {
        if (callback)
            callback(QJsonObject(), QJsonObject{{QStringLiteral("message"), QStringLiteral("app-server 未运行")}});
        return;
    }
    const qint64 id = m_nextId++;
    m_pending.insert(id, PendingCall{std::move(callback), nullptr});

    QJsonObject message;
    message.insert(QStringLiteral("id"), id);
    message.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);
    QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_process->write(data);

    QTimer::singleShot(timeoutMs, this, [this, id]() {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        ResponseCallback cb = std::move(it->callback);
        m_pending.erase(it);
        if (cb)
            cb(QJsonObject(), QJsonObject{{QStringLiteral("message"), QStringLiteral("请求超时")}});
    });
}

void JsonRpcProcess::notify(const QString &method, const QJsonObject &params)
{
    if (!isRunning())
        return;
    QJsonObject message;
    message.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);
    QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_process->write(data);
}

void JsonRpcProcess::onReadyRead()
{
    m_buffer.append(m_process->readAllStandardOutput());
    int index = -1;
    while ((index = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(index).trimmed();
        m_buffer.remove(0, index + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning().noquote() << "[jsonrpc] 无法解析:" << QString::fromUtf8(line.left(200));
            continue;
        }
        handleMessage(doc.object());
    }
}

void JsonRpcProcess::handleMessage(const QJsonObject &message)
{
    const QJsonValue idValue = message.value(QStringLiteral("id"));
    if (message.contains(QStringLiteral("method"))) {
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (idValue.isDouble()) {
            // 服务端发起的请求（审批/交互），当前版本统一拒绝，避免无人值守时卡住。
            QJsonObject reply;
            reply.insert(QStringLiteral("id"), idValue);
            QJsonObject error;
            error.insert(QStringLiteral("code"), -32601);
            error.insert(QStringLiteral("message"), QStringLiteral("client does not support interactive requests"));
            reply.insert(QStringLiteral("error"), error);
            QByteArray data = QJsonDocument(reply).toJson(QJsonDocument::Compact);
            data.append('\n');
            m_process->write(data);
            qWarning().noquote() << "[jsonrpc] 拒绝服务端请求:" << method;
        }
        emit notification(method, params);
        return;
    }

    if (!idValue.isDouble())
        return;
    const qint64 id = static_cast<qint64>(idValue.toDouble());
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    ResponseCallback callback = std::move(it->callback);
    m_pending.erase(it);
    if (!callback)
        return;
    if (message.contains(QStringLiteral("error"))) {
        callback(QJsonObject(), message.value(QStringLiteral("error")).toObject());
    } else {
        callback(message.value(QStringLiteral("result")).toObject(), QJsonObject());
    }
}

void JsonRpcProcess::onProcessError()
{
    qWarning().noquote() << "[rpc] QProcess error:" << m_process->errorString();
    emit errorOccurred(m_process->errorString());
}

void JsonRpcProcess::onProcessFinished(int exitCode)
{
    m_buffer.clear();
    failAllPending(QStringLiteral("app-server 已退出（code %1）").arg(exitCode));
    emit processStopped(exitCode);
}

void JsonRpcProcess::failAllPending(const QString &error)
{
    const auto pending = m_pending;
    m_pending.clear();
    for (const PendingCall &call : pending) {
        if (call.callback)
            call.callback(QJsonObject(), QJsonObject{{QStringLiteral("message"), error}});
    }
}

} // namespace arc
