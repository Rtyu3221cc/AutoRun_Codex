#include "core/CodexWriterLock.h"

#include "core/CodexPaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#pragma comment(lib, "user32.lib")
#endif

namespace arc {

namespace {

QString writerLockPath(const QString &threadId)
{
    return QDir(codexHome() + QStringLiteral("/thread-writer-locks"))
            .filePath(threadId + QStringLiteral(".lock"));
}

QString findRolloutPath(const QString &threadId)
{
    if (threadId.isEmpty())
        return QString();
    const QString home = codexHome();
    for (const QString &root : {home + QStringLiteral("/sessions"),
                                home + QStringLiteral("/archived_sessions")}) {
        if (!QDir(root).exists())
            continue;
        QDirIterator it(root, QStringList{QStringLiteral("rollout-*.jsonl")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (QFileInfo(path).fileName().contains(threadId))
                return path;
        }
    }
    return QString();
}

} // namespace

bool isThreadWriterLockHeld(const QString &threadId)
{
#ifdef Q_OS_WIN
    if (threadId.isEmpty())
        return false;
    const QString path = QDir::toNativeSeparators(writerLockPath(threadId));
    if (!QFileInfo::exists(path))
        return false;
    // Codex 用独占句柄持有锁文件：能以“不允许共享”的方式打开 = 当前没有写入者。
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                                      GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
    }
    CloseHandle(handle);
    return false;
#else
    Q_UNUSED(threadId);
    return false;
#endif
}

#ifdef Q_OS_WIN
namespace {

struct ProcessEntry
{
    quint32 pid = 0;
    quint32 parentPid = 0;
    QString name;
    QString path;
};

QList<ProcessEntry> enumerateProcesses()
{
    QList<ProcessEntry> result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessEntry process;
            process.pid = entry.th32ProcessID;
            process.parentPid = entry.th32ParentProcessID;
            process.name = QString::fromWCharArray(entry.szExeFile);
            const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
            if (handle) {
                wchar_t buffer[4096];
                DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
                if (QueryFullProcessImageNameW(handle, 0, buffer, &size))
                    process.path = QString::fromWCharArray(buffer, static_cast<int>(size));
                CloseHandle(handle);
            }
            result.append(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool isAlive(quint32 pid)
{
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle)
        return false;
    DWORD code = 0;
    const bool ok = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return ok;
}

bool terminateProcess(quint32 pid)
{
    const HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!handle)
        return false;
    const bool ok = TerminateProcess(handle, 1) != FALSE;
    CloseHandle(handle);
    return ok;
}

struct CloseWindowData
{
    QSet<quint32> pids;
    int closed = 0;
};

BOOL CALLBACK closeWindowCallback(HWND hwnd, LPARAM lParam)
{
    auto *data = reinterpret_cast<CloseWindowData *>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (data->pids.contains(pid) && IsWindowVisible(hwnd)) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        ++data->closed;
    }
    return TRUE;
}

QSet<quint32> desktopRootProcessIds(const QList<ProcessEntry> &processes)
{
    // 自动化回归用：AR_CODEX_DESKTOP_PID 直接指定“桌面版”进程（只影响本进程，不写配置）。
    const QString pidOverride = qEnvironmentVariable("AR_CODEX_DESKTOP_PID").trimmed();
    if (!pidOverride.isEmpty()) {
        bool ok = false;
        const uint pid = pidOverride.toUInt(&ok);
        QSet<quint32> result;
        if (ok && pid > 0)
            result.insert(static_cast<quint32>(pid));
        return result;
    }
    // 测试/特殊环境可用 AR_CODEX_DESKTOP_PROCESS_NAMES 指定进程名（逗号分隔）。
    const QString override = qEnvironmentVariable("AR_CODEX_DESKTOP_PROCESS_NAMES").trimmed();
    QSet<QString> names;
    if (!override.isEmpty()) {
        for (const QString &name : override.split(QLatin1Char(','), Qt::SkipEmptyParts))
            names.insert(name.trimmed().toLower());
    }
    QSet<quint32> result;
    for (const ProcessEntry &process : processes) {
        if (process.pid == GetCurrentProcessId())
            continue;
        if (!names.isEmpty()) {
            if (names.contains(process.name.toLower()))
                result.insert(process.pid);
            continue;
        }
        const QString path = QDir::fromNativeSeparators(process.path).toLower();
        if (path.contains(QStringLiteral("/windowsapps/openai.codex")))
            result.insert(process.pid);
    }
    return result;
}

} // namespace

bool isCodexDesktopAppRunning()
{
    return !desktopRootProcessIds(enumerateProcesses()).isEmpty();
}

quint64 systemIdleSeconds()
{
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info))
        return 0;
    const DWORD elapsed = GetTickCount() - info.dwTime;
    return static_cast<quint64>(elapsed / 1000);
}

bool closeCodexDesktopApp(QString *detail)
{
    const QList<ProcessEntry> processes = enumerateProcesses();
    const QSet<quint32> roots = desktopRootProcessIds(processes);
    if (roots.isEmpty()) {
        if (detail)
            *detail = QStringLiteral("未找到 Codex 桌面版进程");
        return false;
    }

    // 收集 root 的全部后代（桌面版拉起的 codex app-server 才是真正持有写锁的进程）。
    QSet<quint32> targets = roots;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const ProcessEntry &process : processes) {
            if (process.pid == GetCurrentProcessId())
                continue;
            if (targets.contains(process.parentPid) && !targets.contains(process.pid)) {
                targets.insert(process.pid);
                changed = true;
            }
        }
    }

    // 1) 先发 WM_CLOSE，尽量优雅退出（Codex 的对话数据本身是落盘的）。
    CloseWindowData windowData;
    windowData.pids = targets;
    EnumWindows(closeWindowCallback, reinterpret_cast<LPARAM>(&windowData));

    QElapsedTimer timer;
    timer.start();
    auto anyAlive = [&targets]() {
        for (const quint32 pid : targets) {
            if (isAlive(pid))
                return true;
        }
        return false;
    };
    while (timer.elapsed() < 8000 && anyAlive())
        QThread::msleep(120);

    // 2) 优雅退出失败：强制结束整个进程树（先子进程再父进程）。
    int killed = 0;
    if (anyAlive()) {
        QList<quint32> ordered = targets.values();
        std::sort(ordered.begin(), ordered.end(), [&processes](quint32 a, quint32 b) {
            auto depth = [&processes](quint32 pid) {
                int d = 0;
                quint32 current = pid;
                for (int i = 0; i < 8; ++i) {
                    const ProcessEntry *found = nullptr;
                    for (const ProcessEntry &process : processes) {
                        if (process.pid == current) {
                            found = &process;
                            break;
                        }
                    }
                    if (!found || found->parentPid == 0)
                        break;
                    current = found->parentPid;
                    ++d;
                }
                return d;
            };
            return depth(a) > depth(b);
        });
        for (const quint32 pid : ordered) {
            if (isAlive(pid) && terminateProcess(pid))
                ++killed;
        }
        QElapsedTimer killTimer;
        killTimer.start();
        while (killTimer.elapsed() < 3000 && anyAlive())
            QThread::msleep(100);
    }

    if (detail) {
        *detail = QStringLiteral("已结束 %1 个 Codex 桌面版进程（共 %2 个，窗口关闭 %3 个）")
                          .arg(killed)
                          .arg(targets.size())
                          .arg(windowData.closed);
    }
    return true;
}
#else
bool isCodexDesktopAppRunning()
{
    return false;
}

quint64 systemIdleSeconds()
{
    return 0;
}

bool closeCodexDesktopApp(QString *detail)
{
    if (detail)
        *detail = QStringLiteral("仅支持 Windows");
    return false;
}
#endif

bool isThreadRecentlyActive(const QString &threadId, int withinSeconds)
{
    const QString path = findRolloutPath(threadId);
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    if (!info.exists())
        return false;
    const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - info.lastModified().toMSecsSinceEpoch();
    return ageMs >= 0 && ageMs < static_cast<qint64>(withinSeconds) * 1000;
}

} // namespace arc
