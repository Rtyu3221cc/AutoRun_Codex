#include "ui/MainWindow.h"

#include "core/CodexPaths.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QNetworkProxyFactory>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace {
QFile *g_logFile = nullptr;
QMutex g_logMutex;

void fileMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (!g_logFile)
        return;
    QMutexLocker locker(&g_logMutex);
    const char *level = "INFO";
    if (type == QtWarningMsg)
        level = "WARN";
    else if (type == QtCriticalMsg)
        level = "ERROR";
    else if (type == QtFatalMsg)
        level = "FATAL";
    QTextStream stream(g_logFile);
    stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
           << " [" << level << "] " << msg << "\n";
    stream.flush();
}

void installFileLogger()
{
    const QString directory = arc::appDataDir();
    QDir().mkpath(directory);
    g_logFile = new QFile(directory + QStringLiteral("/auto-run-codex.log"));
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete g_logFile;
        g_logFile = nullptr;
        return;
    }
    qInstallMessageHandler(fileMessageHandler);
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AutoRun Codex"));
    QApplication::setOrganizationName(QStringLiteral("AutoRunCodex"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.8"));
    QApplication::setQuitOnLastWindowClosed(false);

    installFileLogger();
    qInfo().noquote() << "==== AutoRun Codex 启动 ====";

    // 读取账号资料/头像时走系统代理（与 Codex 桌面版一致；不影响 app-server）。
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    // 统一使用随程序打包的 HarmonyOS Sans SC TW（加载失败自动回退微软雅黑）。
    arc::theme::installBundledFont();
    arc::theme::applyApplicationTheme(app);

    arc::MainWindow w;
    w.show();
    return QApplication::exec();
}