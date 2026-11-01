#pragma once

#include <QString>
#include <QStringList>

namespace arc {

struct AppSettings
{
    QString codexCliPath; // 空 = 自动探测
    QString defaultPrompt = QStringLiteral("继续执行未完成的任务");
    bool continuousByDefault = false;
    int jitterSeconds = 150;         // 额度重置后先等待多少秒再发续跑（默认 2.5 分钟）
    int retryAttempts = 3;           // 每轮最多尝试次数（=首次+重试；失败间隔 2 分钟，用尽后看守灯转红）
    bool autoStart = false;          // 登录自启
    bool alwaysOnTop = true;         // 悬浮条置顶
    bool showFloatingWidget = true;
    QString dockEdge = QStringLiteral("right"); // left / right / top / bottom / auto
    QString floatingStyle = QStringLiteral("reference"); // reference（数字在环下）/ classic（数字在环内）
    int widgetScale = 100;           // 百分比
    bool minimizeToTray = true;
    bool displayRemaining = false; // true=按剩余额度显示进度，false=按已用显示
    QString uiTheme = QStringLiteral("dark"); // dark / light
    int widgetOpacity = 92;                   // 悬浮窗背景不透明度（30-100）
    QStringList ignoredThreadIds; // 用户选择“忽略限额提示”的对话
};

class SettingsStore
{
public:
    static QString settingsPath();
    static AppSettings load();
    static void save(const AppSettings &settings);
};

} // namespace arc