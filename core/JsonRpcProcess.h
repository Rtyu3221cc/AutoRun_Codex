#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

namespace arc {

// 用 QProcess 启动 codex app-server --stdio，逐行 JSON 收发。
class JsonRpcProcess : public QObject
{
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(const QJsonObject &result, const QJsonObject &error)>;

    explicit JsonRpcProcess(QObject *parent = nullptr);
    ~JsonRpcProcess() override;

    void setExecutable(const QString &path);
    QString executable() const { return m_executable; }

    bool start();
    void stop();
    bool isRunning() const;

    // 发送 JSON-RPC 请求；回调在超时或收到响应时触发（只触发一次）。
    void request(const QString &method, const QJsonObject &params, ResponseCallback callback,
                 int timeoutMs = 60000);
    void notify(const QString &method, const QJsonObject &params = QJsonObject());

signals:
    void processStarted();
    void processStopped(int exitCode);
    void errorOccurred(const QString &message);
    void notification(const QString &method, const QJsonObject &params);

private slots:
    void onReadyRead();
    void onProcessError();
    void onProcessFinished(int exitCode);

private:
    struct PendingCall
    {
        ResponseCallback callback;
        QTimer *timer = nullptr;
    };

    void handleMessage(const QJsonObject &message);
    void failAllPending(const QString &error);

    QProcess *m_process = nullptr;
    QString m_executable;
    QByteArray m_buffer;
    qint64 m_nextId = 1;
    QHash<qint64, PendingCall> m_pending;
};

} // namespace arc
