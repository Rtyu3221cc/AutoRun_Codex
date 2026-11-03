# AutoRun Codex · 对话交接文档（换对话前先读我）

> 用途：换新的 AI 对话时，把本文件路径发给新对话，就能接着干。
> 最后更新：2026-11-03 15:30（JST）
> 项目路径：`D:\QtWorkspace\AutoRun_Codex`
> 代码仓库：<https://github.com/Rtyu3221cc/AutoRun_Codex>
> 当前版本：**v0.1.12**（已 build + deploy；用户已于 2026-11-03 01:07 重启，程序正在运行 v0.1.12；GitHub Release 已发布）
> 技术细节见同目录 `HANDOFF.md`；本文件只记录“这个对话做了什么、现在停在哪、下一段对话怎么接”。

---

## 0. 一句话背景

Windows 桌面小工具：通过本机 Codex CLI 的 app-server 显示 5h / 每周额度，并在额度重置后自动续跑指定对话；悬浮条底部有“对话活动指示灯”。

---

## 1. 当前实机状态（交接时：2026-11-03 15:30 JST）

| 项 | 状态 |
|---|---|
| 运行中的程序 | `D:\QtWorkspace\AutoRun_Codex\dist\AutoRun_Codex.exe`，PID 6844，2026-11-03 01:07:38 启动（**v0.1.12**，已重启生效） |
| 数据目录 | `%APPDATA%\AutoRun_Codex\`（settings.json / jobs.json / auto-run-codex.log） |
| 当前设置 | 深色主题；悬浮条缩放 90%；不透明度 85%；参考样式；`autoReleaseWriterLock=true`；`trayNotifications=true` |
| 看守队列 | **空**（原本“生成云间日麻测试 APK”的看守已在 2026-11-03 01:07:55 被用户取消；jobs.json = `[]`） |
| 活动指示灯（最近） | 15:17 日志：`活动指示灯：运行中`；此前 14:16 完成 → 14:21 无活动对话。说明用户此刻可能在用 Codex |
| dist 目录 | 只有一个 `AutoRun_Codex.exe`（v0.1.12，11,422,208 B，SHA256 前缀 `28A0B8DA`）；旧备份 .old-v010/.old-v011 已清理 |
| 工作区 git | `main = abfb590`（v0.1.12），tags：v0.1.8 / v0.1.9 / v0.1.10 / v0.1.11 / v0.1.12；working tree clean |
| GitHub Release | v0.1.12：<https://github.com/Rtyu3221cc/AutoRun_Codex/releases/tag/v0.1.12>（asset 45,928,148 B，SHA256 `63AAF019...A1C6CAF`） |

> 额度数据会过期，交接时不记录具体百分比；以程序实时显示为准。日志里 `failed to refresh available models: request timed out` 是 app-server 拉模型目录超时（网络抖动），可忽略。

### 交接前最后一段生产证据（重要）

- **写锁等待在真实环境跑过**：2026-11-02 21:10 该看守因限额中断，计划 23:16 续跑；2026-11-03 01:07 重启后首次尝试时 Codex 桌面版正打开着该对话，日志出现“被 Codex 桌面版占用，本次不计失败次数……在桌面版里关闭这个对话/退出桌面版，锁释放后自动继续”，**没有亮红灯、没有消耗重试次数**；01:07:55 用户选择取消该看守（不是失败）。
- **活动指示灯在生产环境多次切换**：运行中 / 完成 / 最近结束 / 异常中断 / 无活动对话都出现过，日志有对应 `[job] 活动指示灯：…` 记录。
- 说明：v0.1.9（写锁自愈）与 v0.1.10~v0.1.12（活动灯/通知/经典样式）改动均已在真实运行中生效；托盘气泡是否可见取决于 Windows 通知设置（代码路径已跑过，无崩溃）。

---

## 2. 本对话做了什么（v0.1.8 → v0.1.12，全部已 build / deploy / 发布）

### 2.1 v0.1.8：看守徽标随悬浮条缩放
- 50% 缩放时底部“看守中 N”文字/呼吸灯不缩小（写死像素+行高下限）。改为按 `widgetScale` 等比缩放文字/圆点/光晕/涟漪/行高。截图 `docs/widget-scale-50.png`。
- 该徽标在 v0.1.10 被整体替换为“活动指示灯”（见 2.3）。

### 2.2 v0.1.9：写锁冲突自愈（解决“续跑失败亮红灯”）
- 根因：**Codex 桌面版打开着某个对话时会独占其 thread 写锁（会话级，不是 turn 级）**，独立 app-server 的 `thread/resume` 必然报 `already has an active writer`，旧版重试 3 次后亮红灯。
- 新增 `core/CodexWriterLock.*`：锁探测、rollout 最近活动、系统空闲时间、关闭 Codex 桌面版（WM_CLOSE → 8s 后结束进程树）。
- `JobEngine`：写锁冲突不计失败次数/不亮红灯，进入 `writerBlocked` 轮询等待，锁释放立即续跑；`m_ownLoadedThreads` 排除自身 app-server 已加载线程的误判；设置 `autoReleaseWriterLock`（默认开，用户离开 + 对话空闲时自动关桌面版）。
- 顺手修：主窗口“立即续跑”不重置 `cycleAttempts`；job 日志落盘到 `auto-run-codex.log`。

### 2.3 v0.1.10：活动指示灯 + 写锁提示改进
- 用户反馈：①“等待写锁释放”太抽象，要直接告诉用户去桌面版关闭/切换对话；②“看守中 N”呼吸灯意义不大，改成**当前对话活动指示灯**。
- `ui/WatchBadge.*` 重写：状态=运行中（绿黄呼吸）/ 等待回复（黄呼吸）/ 异常中断（红呼吸）/ 完成（蓝）/ 最近结束（绿）/ 无活动对话（灰，不呼吸）；优先级 异常中断 > 运行中 > 等待回复 > 完成 > 最近结束 > 空闲；“完成/最近结束”为 5 分钟窗口，可用 `AR_ACTIVITY_RECENT_SECONDS` 调；被 `ignoredThreadIds` 忽略的对话不参与。
- 数据：`RolloutState/ThreadInfo` 新增 `completedAt`（RolloutScanner 解析正常完成时间），并防御未来坏时间戳。
- 写锁提示：主窗口详情明确写“在桌面版里关闭这个对话或切换到其他对话/退出桌面版”；日志每约 5 分钟提醒。
- 截图 `docs/activity-indicator.png`。

### 2.4 v0.1.11：活动灯微调
- “刚完成”→“完成”；呼吸规则改为“除灰色外全部呼吸”（完成/最近结束从常亮改为呼吸+涟漪）。

### 2.5 v0.1.12：托盘通知 + 经典样式小尺寸修复
- **Windows 托盘通知**：`JobEngine::notifyRequested(title, message, warning)` 信号，续跑完成 / 失败重试 / 连续失败红灯 / 写锁等待 / 写锁释放 / 自动关闭桌面版时弹气泡；`MainWindow` 用 `m_tray->showMessage` 显示；设置 `trayNotifications`（默认开）。
- **经典样式小尺寸修复**：`RingWidget` 文字宽度改为按圆环净内径省略；`QuotaWidget` 在经典样式且 `widgetScale < 70%` 时用短标题（“5h/周”）并隐藏环内倒计时，70%+ 保持原样；截图 `docs/classic-widget-50.png`。

### 2.6 发布与仓库（GitHub 上传 / 发版流程）
- 项目从无 .git 到上传 <https://github.com/Rtyu3221cc/AutoRun_Codex>（public，main），已发 v0.1.8 ~ v0.1.12 五个 Release。
- 发布脚本：`release.cmd` → `packaging/make-release.ps1`（从 `dist\` 生成 zip + sha256，版本号读 `main.cpp`）；`packaging/publish-release.ps1`（创建 tag/Release + 上传 zip，凭据取 git credential manager，不落盘）。
- **发版顺序（重要）：改版本号 → build.cmd → deploy.cmd → git commit/push → release.cmd → publish-release.ps1**。先 publish 再 push 会导致 tag 指向旧 commit（v0.1.9 踩过，后来用 GitHub API 修正）。
- 仓库 ignore：`build/ build-dsh/ dist/ release/ .qtcreator/ ref/`；`resources/`（含 8.4MB 字体）必须提交。

---

## 3. 待用户验收 / 下一步建议

1. **活动指示灯验收**（当前已在运行 v0.1.12，可直接看）：悬浮条底部——无对话活动灰色“无活动对话”不呼吸；Codex 运行时绿黄“运行中”；结束后 5 分钟内蓝“完成”/绿“最近结束”（都呼吸）；有限额中断对话时红“异常中断”。对照 `docs/activity-indicator.png`。
2. **托盘通知验收**：留意续跑完成/失败、写锁等待/释放时是否弹 Windows 托盘气泡；不想被打扰可在设置关“续跑/写锁状态变化时显示 Windows 托盘通知”。
3. **经典样式 50% 复查**：样式设为“经典”、缩放 50%，环内应是 百分比 + “5h/周”短标题（不再拥挤/压描边）；70%+ 恢复正常标题+倒计时。对照 `docs/classic-widget-50.png`。
4. 可选打磨（按用户反馈优先级）：
   - 失败重试间隔做成设置项；周额度耗尽显式判断并提示；
   - 开机自启（设置项已存在但未写注册表）；
   - 单测/协议回归脚本；
   - 活动灯“等待回复”判定可再细化；
   - 托盘通知频率如觉得吵可加“仅红灯通知”开关。

---

## 4. 关键决策与背景（避免重复踩坑）

1. **为什么自己起 app-server**：桌面 app 没有可连的控制 socket；独立 app-server 是官方 CLI 路径。代价是单写者模型 → 写锁冲突（v0.1.9 已处理）。
2. **写锁是会话级**：桌面版只要打开着对话就持有锁（即使没在跑 turn）；等 turn 跑完不会释放锁，必须关闭该对话/退出桌面版，或让程序自动关闭桌面版。
3. **自动关闭桌面版是无人值守妥协**：仅当用户离开（系统闲置 ≥5min）且目标对话空闲 ≥90s 时触发；如果桌面版同时在其它对话跑任务会被一起关掉（Codex 没有逐对话释放写锁的接口）。可在设置关闭。
4. **活动指示灯是推断**：“运行中/等待回复”依赖 rollout 文件活动 + app-server 状态；“完成/最近结束”是 5 分钟窗口。
5. **旧版遗留 writer-lock 红灯（state=Failed）不会自动重试**：避免擅自续跑用户可能已手动完成的对话；点一次“立即续跑”即可进入等待写锁逻辑（当前队列为空，已无此问题）。
6. **托盘通知可关**：续跑/写锁事件都发生在用户不在电脑前时，只写日志难被看到；但通知频繁会打扰，所以默认开、可关；写锁等待只在首次进入时通知。
7. **用户偏好**：非常在意视觉细节（会截图指出）；要中文交流、结论优先；**不要擅自测试正在使用的对话 / 正在跑任务的 Codex 桌面版**（测试用隔离 APPDATA + 假锁/假进程）。

---

## 5. 常用命令 & 调试入口

```powershell
# 构建（MSVC2019 + Qt 6.6.2，NMake）
cmd /c D:\QtWorkspace\AutoRun_Codex\build.cmd
# 打包到 dist（先退出运行中的实例，否则 exe 被锁）
Get-Process AutoRun_Codex | Stop-Process -Force
cmd /c D:\QtWorkspace\AutoRun_Codex\deploy.cmd
# 生成发布 zip + sha256
cmd /c D:\QtWorkspace\AutoRun_Codex\release.cmd
# 上传 GitHub Release（需已 git push、本机 GitHub 凭据可用）
powershell -NoProfile -ExecutionPolicy Bypass -File D:\QtWorkspace\AutoRun_Codex\packaging\publish-release.ps1
```

截图 / 调试环境变量：
- `AR_SCREENSHOT_DIR=<目录>`：启动 4 秒后抓主窗口/悬浮条/弹卡 PNG 并退出；
- `AR_UI_THEME=dark|light`、`AR_SCREENSHOT_FORCE_PULSE=1`、`AR_SCREENSHOT_FAKE_REMAINING=0,37`、`AR_SCREENSHOT_FAKE_CREDIT_DAYS=1.5`、`AR_SCREENSHOT_DIALOGS=1` 等见 HANDOFF 第 10 节；
- `AR_SCREENSHOT_FAKE_ACTIVITY=running|waiting|interrupted|completed|recent|idle[:N]`：强制活动指示灯状态；
- `AR_ACTIVITY_RECENT_SECONDS=300`：活动灯“完成/最近结束”窗口；
- 写锁测试：`AR_WRITER_POLL_SECONDS` / `AR_WRITER_GRACE_SECONDS` / `AR_WRITER_IGNORE_USER_IDLE` / `AR_CODEX_DESKTOP_PID=<测试进程PID>` / `AR_AUTOTEST_EXIT_SECONDS=N`（详见 HANDOFF 5.10）；
- 测试务必用隔离 APPDATA（复制一份 settings.json、jobs.json 写 `[]`），避免触发真实看守。

日志：`%APPDATA%\AutoRun_Codex\auto-run-codex.log`（UTF-8；v0.1.9 起调度日志以 `[job]` 前缀落盘）。

---

## 6. 关键文件地图

- `core/JobEngine.*`：调度/等待/重试/红灯状态机；写锁等待（`writerBlocked`、`processWriterBlocked()`、`m_ownLoadedThreads`）；v0.1.12 新增 `notifyRequested` 托盘通知信号。
- `core/CodexWriterLock.*`（v0.1.9）：锁探测、rollout 最近活动、系统空闲、关闭 Codex 桌面版。
- `core/RolloutScanner.*` / `core/ThreadCatalog.*`：解析 interrupted/running/resetAt/completedAt。
- `core/Types.h`：`ThreadInfo`、`ResumeJob`、`ActivityState`（活动灯状态与文案）。
- `core/SettingsStore.*` / `ui/SettingsDialog.*`：`autoReleaseWriterLock`、`trayNotifications` 等设置项。
- `ui/WatchBadge.*`：悬浮条底部活动指示灯（6 状态颜色/呼吸策略，随缩放等比）。
- `ui/QuotaWidget.*`：悬浮条布局/缩放/经典样式短标题逻辑；`setActivityStatus()` 转发给徽标。
- `ui/RingWidget.*`：额度环绘制；v0.1.12 环内文字按净内径省略。
- `ui/MainWindow.*`：主窗口；`computeActivityState()`；托盘通知连接；“立即续跑”重置重试计数；日志落盘。
- `ui/Theme.*`：颜色口径（含 `blue()`）。
- `core/CodexClient.*`：app-server JSON-RPC。
- `packaging/` + `release.cmd`：发布打包/上传脚本；`docs/` 下有 activity-indicator、widget-scale-50、classic-widget-50 等截图。

---

## 7. 已知问题 / 限制

1. **活动灯为推断**：超长工具调用、rollout 尾部截断、坏时间戳可能让“运行中/等待回复/完成”有偏差；已加时间戳防御。
2. **自动关闭桌面版的边界**：只在用户离开 + 目标对话空闲时触发；桌面版其它对话正在跑任务时会被一起关掉；可在设置关闭（纯等待模式）。
3. **旧 writer-lock 红灯不自动重试**：需要手动点“立即续跑”；刻意设计，避免擅自续跑已手动完成的对话。
4. **周额度耗尽**：JobEngine 没有专门判断 secondary 100%（仍按 5h resetAt 触发，重试后红灯）。
5. **开机自启未实现**：设置项只保存字段，未写注册表。
6. **没有单元测试工程**：只有隔离 APPDATA 的 ad-hoc 回归。
7. 日志偶发 `QPainter::end: Painter ended with N saved states`（v0.1.0 起既有，可忽略）；偶发 `codex app-server failed to refresh available models: request timed out`（网络抖动，可忽略）。
8. 托盘气泡依赖 Windows 通知设置（专注助手/勿扰模式可能静默丢弃）；气泡不出现不代表功能没执行（看日志/主窗口详情）。

---

## 8. 交接后第一件事（Checklist）

1. 读同目录 `HANDOFF.md` 的 5.8（字体）/ 5.9（缩放）/ 5.10（写锁自愈）/ 5.11（活动灯）/ 5.12（托盘通知 + 经典样式）/ 6.3（当前状态）。
2. 确认运行版本：`Get-Process AutoRun_Codex | Select Id,StartTime,Path`，对比 dist exe 与 build-dsh 哈希。
3. 打开主窗口确认“已连接”、额度正常、对话列表有数据；看悬浮条活动指示灯是否符合预期。
4. 改视觉/行为前：隔离 APPDATA + `AR_SCREENSHOT_*` 先截图验证，再 build → deploy（deploy 前先退出运行实例）→ 让用户重启。
5. 发新版按“改版本号 → build → deploy → git commit/push → release.cmd → publish-release.ps1”顺序，别把 tag 指到旧 commit。
6. 用户很在意细节，改动后给截图/对比图，并说明“改的是哪个元素、什么场景触发”。

---

## 9. 本对话环境 / 协作注意事项（踩过的坑）

- **部署时旧实例锁 exe**：稳妥做法是把 `dist\AutoRun_Codex.exe` 改名为 `.old-vXXX`、复制新 exe 进去，再请用户从托盘退出重启（沙箱里结束进程的请求可能被授权策略拦下并挂起）。
- **PowerShell 5.1 编码**：含中文的 .ps1 若以 UTF-8 无 BOM 保存会被按 GBK 解析；脚本用 ASCII-only + 中文内容放 UTF-8 文本文件读取。
- **System.Drawing 变量大小写**：PowerShell 变量名大小写不敏感（`$w` 与 `$W` 是同一个），合成截图时用 `$bw/$bh` 这类名字避免互相覆盖。
- **Qt/CMake 坑**：`QPropertyAnimation` 没有 `setDeleteWhenStopped`（用 `start(DeleteWhenStopped)`）；局部变量别叫 `x/y`（遮蔽 `QWidget::x()/y()`）；中文统一 `QStringLiteral`、源文件 UTF-8。
- **DSH 工具异常（本次会话）**：`edit`/`write` 工具曾因 auto-review 账户余额不足报错“Insufficient Balance”，文件修改改用 `run_code` + Node.js `fs`（或 pwsh）完成；如果新对话遇到同样报错，用同样方式改文件即可。
- 用户可能正在用 Codex 桌面版跑任务；**不要擅自 resume 用户正在用的对话**，测试只用隔离数据/假锁/假进程。

---

## 10. 一句话交接

v0.1.12 已上线并在运行（v0.1.8~v0.1.12）：活动指示灯（运行中/等待回复/中断/完成/最近结束/空闲）、写锁冲突自愈且提示清晰、续跑/写锁事件托盘通知、经典样式小尺寸不拥挤；当前看守队列为空，下一个对话接手时先确认版本与运行状态，再按用户新反馈做增量修改 + 隔离测试 + 发版。
