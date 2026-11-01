#include "core/CodexPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace arc {

QString codexHome()
{
    const QString fromEnv = qEnvironmentVariable("CODEX_HOME");
    if (!fromEnv.isEmpty())
        return QDir::cleanPath(fromEnv);
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.codex"));
}

QString findCodexCli()
{
    for (const char *key : {"AR_CODEX_CLI", "CODEX_CLI_PATH"}) {
        const QString value = qEnvironmentVariable(key);
        if (!value.isEmpty() && QFileInfo::exists(value))
            return QDir::cleanPath(value);
    }

    QStringList candidates;
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        QDir binDir(localAppData + QStringLiteral("/OpenAI/Codex/bin"));
        const QFileInfoList dirs = binDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : dirs) {
            const QString exe = info.absoluteFilePath() + QStringLiteral("/codex.exe");
            if (QFileInfo::exists(exe))
                candidates << exe;
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("codex"));
    if (!fromPath.isEmpty())
        candidates << fromPath;

    QString best;
    qint64 bestTime = -1;
    for (const QString &candidate : candidates) {
        const QFileInfo fi(candidate);
        const qint64 modified = fi.lastModified().toMSecsSinceEpoch();
        if (modified > bestTime) {
            bestTime = modified;
            best = fi.absoluteFilePath();
        }
    }
    return best;
}

QString appDataDir()
{
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        return QDir::cleanPath(appData + QStringLiteral("/AutoRun_Codex"));
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

} // namespace arc
