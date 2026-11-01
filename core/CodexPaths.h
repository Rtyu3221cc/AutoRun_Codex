#pragma once

#include <QString>

namespace arc {

// $CODEX_HOME，默认 %USERPROFILE%\.codex
QString codexHome();

// 探测最新的 Codex CLI（codex.exe）。优先环境变量，其次
// %LOCALAPPDATA%\OpenAI\Codex\bin\<hash>\codex.exe（取最新），最后 PATH。
QString findCodexCli();

// 本程序自己的配置目录：%APPDATA%\AutoRun_Codex
QString appDataDir();

} // namespace arc
