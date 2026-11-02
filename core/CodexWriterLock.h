#pragma once

#include <QString>

namespace arc {

// Codex 的 thread store 是“单写者”模型：某个进程（通常是 Codex 桌面版）
// 把对话加载进会话后，会独占 $CODEX_HOME/thread-writer-locks/<threadId>.lock。
// 另一个进程（本程序自己的 codex app-server）再去 thread/resume 就会得到
//   thread-store conflict: thread <id> already has an active writer
//
// 本文件封装了“检测写锁 / 检测对话是否仍在跑 / 关闭 Codex 桌面版释放写锁”。

// 该线程的 writer 锁当前是否被其他进程独占。
bool isThreadWriterLockHeld(const QString &threadId);

// Codex 桌面版（ChatGPT.exe / MSIX OpenAI.Codex）是否在运行。
bool isCodexDesktopAppRunning();

// 该对话的 rollout 文件最近 withinSeconds 秒内是否仍在写入（仍在跑 turn）。
bool isThreadRecentlyActive(const QString &threadId, int withinSeconds);

// 距离上一次键盘/鼠标输入的秒数（用户离开电脑多久）。
quint64 systemIdleSeconds();

// 关闭 Codex 桌面版并释放其写锁：
//   1) 先向桌面版所有可见窗口发 WM_CLOSE（优雅退出）；
//   2) 最多等 8 秒；
//   3) 仍有残留进程则强制结束整个进程树（含桌面版拉起的 codex app-server）。
// 返回 false 表示没找到桌面版进程或关闭失败；detail 用于日志。
bool closeCodexDesktopApp(QString *detail = nullptr);

} // namespace arc
