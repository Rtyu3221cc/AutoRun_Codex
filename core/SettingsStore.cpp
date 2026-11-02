#include "core/SettingsStore.h"

#include "core/CodexPaths.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace arc {
namespace {

// 缺字段或类型不符时使用默认值，尽量不因为某个字段损坏而丢失整份配置。
QString readString(const QJsonObject &object, const QString &key, const QString &fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isString())
        return value.toString();
    return fallback;
}

bool readBool(const QJsonObject &object, const QString &key, bool fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble() != 0.0;
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QStringLiteral("true") || text == QStringLiteral("1"))
            return true;
        if (text == QStringLiteral("false") || text == QStringLiteral("0"))
            return false;
    }
    return fallback;
}

int readInt(const QJsonObject &object, const QString &key, int fallback)
{
    const QJsonValue value = object.value(key);
    if (value.isDouble())
        return qRound(value.toDouble());
    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().toInt(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

int clampInt(int value, int minimum, int maximum)
{
    return qBound(minimum, value, maximum);
}

QString normalizeDockEdge(const QString &value, const QString &fallback)
{
    const QString edge = value.trimmed().toLower();
    if (edge == QStringLiteral("left") || edge == QStringLiteral("right")
        || edge == QStringLiteral("top") || edge == QStringLiteral("bottom")
        || edge == QStringLiteral("auto"))
        return edge;
    return fallback;
}

} // namespace

QString SettingsStore::settingsPath()
{
    const QString directory = appDataDir(); // %APPDATA%\AutoRun_Codex
    if (directory.isEmpty())
        return QStringLiteral("settings.json");
    return QDir(directory).filePath(QStringLiteral("settings.json"));
}

AppSettings SettingsStore::load()
{
    const AppSettings defaults;
    AppSettings settings = defaults;

    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly))
        return settings; // 首次运行：文件不存在，返回默认配置

    const QByteArray payload = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return settings; // 文件损坏：使用默认值，不主动覆盖用户文件

    const QJsonObject object = document.object();

    settings.codexCliPath =
        readString(object, QStringLiteral("codexCliPath"), defaults.codexCliPath).trimmed();
    settings.defaultPrompt =
        readString(object, QStringLiteral("defaultPrompt"), defaults.defaultPrompt);
    if (settings.defaultPrompt.trimmed().isEmpty())
        settings.defaultPrompt = defaults.defaultPrompt;

    settings.continuousByDefault =
        readBool(object, QStringLiteral("continuousByDefault"), defaults.continuousByDefault);
    settings.jitterSeconds = clampInt(
        readInt(object, QStringLiteral("jitterSeconds"), defaults.jitterSeconds), 120, 600);
    settings.retryAttempts = clampInt(
        readInt(object, QStringLiteral("retryAttempts"), defaults.retryAttempts), 1, 5);
    settings.autoReleaseWriterLock = readBool(object, QStringLiteral("autoReleaseWriterLock"),
                                              defaults.autoReleaseWriterLock);
    settings.trayNotifications = readBool(object, QStringLiteral("trayNotifications"),
                                          defaults.trayNotifications);
    settings.autoStart = readBool(object, QStringLiteral("autoStart"), defaults.autoStart);
    settings.alwaysOnTop =
        readBool(object, QStringLiteral("alwaysOnTop"), defaults.alwaysOnTop);
    settings.showFloatingWidget =
        readBool(object, QStringLiteral("showFloatingWidget"), defaults.showFloatingWidget);
    settings.dockEdge = normalizeDockEdge(
        readString(object, QStringLiteral("dockEdge"), defaults.dockEdge), defaults.dockEdge);
    const QString style = readString(object, QStringLiteral("floatingStyle"),
                                     defaults.floatingStyle).trimmed().toLower();
    if (style == QStringLiteral("classic") || style == QStringLiteral("minimal"))
        settings.floatingStyle = style;
    else
        settings.floatingStyle = QStringLiteral("reference");
    settings.widgetScale = clampInt(
        readInt(object, QStringLiteral("widgetScale"), defaults.widgetScale), 25, 400);
    settings.minimizeToTray =
        readBool(object, QStringLiteral("minimizeToTray"), defaults.minimizeToTray);
    settings.displayRemaining =
        readBool(object, QStringLiteral("displayRemaining"), defaults.displayRemaining);
    const QString themeName = readString(object, QStringLiteral("uiTheme"),
                                         defaults.uiTheme).trimmed().toLower();
    settings.uiTheme = themeName == QStringLiteral("light") ? QStringLiteral("light")
                                                            : QStringLiteral("dark");
    settings.widgetOpacity = clampInt(
        readInt(object, QStringLiteral("widgetOpacity"), defaults.widgetOpacity), 30, 100);

    const QJsonArray ignored = object.value(QStringLiteral("ignoredThreadIds")).toArray();
    for (const QJsonValue &value : ignored) {
        if (value.isString() && !value.toString().isEmpty())
            settings.ignoredThreadIds.append(value.toString());
    }

    return settings;
}

void SettingsStore::save(const AppSettings &settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("codexCliPath"), settings.codexCliPath);
    object.insert(QStringLiteral("defaultPrompt"), settings.defaultPrompt);
    object.insert(QStringLiteral("continuousByDefault"), settings.continuousByDefault);
    object.insert(QStringLiteral("jitterSeconds"), settings.jitterSeconds);
    object.insert(QStringLiteral("retryAttempts"), settings.retryAttempts);
    object.insert(QStringLiteral("autoReleaseWriterLock"), settings.autoReleaseWriterLock);
    object.insert(QStringLiteral("trayNotifications"), settings.trayNotifications);
    object.insert(QStringLiteral("autoStart"), settings.autoStart);
    object.insert(QStringLiteral("alwaysOnTop"), settings.alwaysOnTop);
    object.insert(QStringLiteral("showFloatingWidget"), settings.showFloatingWidget);
    object.insert(QStringLiteral("dockEdge"), settings.dockEdge);
    object.insert(QStringLiteral("floatingStyle"), settings.floatingStyle);
    object.insert(QStringLiteral("widgetScale"), settings.widgetScale);
    object.insert(QStringLiteral("minimizeToTray"), settings.minimizeToTray);
    object.insert(QStringLiteral("displayRemaining"), settings.displayRemaining);
    object.insert(QStringLiteral("uiTheme"), settings.uiTheme);
    object.insert(QStringLiteral("widgetOpacity"), settings.widgetOpacity);
    QJsonArray ignored;
    for (const QString &id : settings.ignoredThreadIds)
        ignored.append(id);
    object.insert(QStringLiteral("ignoredThreadIds"), ignored);

    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)); // UTF-8，缩进 2
    file.commit();
}

} // namespace arc