# AutoRun Codex · 对话交接文档（换对话前先读我）

> 用途：换新的 AI 对话时，把本文件路径发给新对话，就能接着干。
> 最后更新：2026-11-02 18:35（JST）
> 项目路径：`D:\QtWorkspace\AutoRun_Codex`
> 代码仓库：<https://github.com/Rtyu3221cc/AutoRun_Codex>
> 当前版本：**v0.1.11**（已 build + deploy 到 `dist\AutoRun_Codex.exe`；运行中的程序还是 v0.1.10，需重启一次；GitHub Release 已发布）
> 技术细节见同目录 `HANDOFF.md`；本文件只记录“这个对话做了什么、现在停在哪、下一段对话怎么接”。

---

## 0. 一句话背景

Windows 桌面小工具：通过本机 Codex CLI 的 app-server 显示 5h / 每周额度，并在额度重置后自动续跑指定对话；悬浮条底部有“对话活动指示灯”。

---

## 1. 当前实机状态（交接时：2026-11-02 18:35 JST）

| 项 | 状态 |
|---|---|
| 运行中的程序 | `D:\QtWorkspace\AutoRun_Codex\dist\AutoRun_Codex.exe`，PID 18676，2026-11-02 17:17:10 启动（**v0.1.10；已部署 v0.1.11，需退出重启后生效**） |
| 数据目录 | `%APPDATA%\AutoRun_Codex\`（settings.json / jobs.json / auto-run-codex.log） |
| 当前主题 / 缩放 | 深色；悬浮条缩放 90%；不透明度 85%；参考样式 |
| 活动指示灯 | 17:17 启动日志：`活动指示灯：无活动对话`（当时没有对话在跑） |
| 看守队列 | 1 条：线程 `01a19b1f-e721-7d83-ab34-19a2882ee8aa`「生成云间日麻测试 APK」，单次模式、state=Waiting、`nextTriggerAt=0`（只登记，等下次被限额打断再排期） |
| dist 目录 | v0.1.11 exe + `AutoRun_Codex.exe.old-v010`（旧版 v0.1.10 备份，可删） |
| 工作区 git | 最新提交为 v0.1.11（源码 + 文档，`git log -1`）；tags：v0.1.8 / v0.1.9 / v0.1.10 / v0.1.11 |
| GitHub Release | v0.1.11：<https://github.com/Rtyu3221cc/AutoRun_Codex/releases/tag/v0.1.11>（asset/SHA256 见 Release 页面或 `release\AutoRun_Codex-v0.1.11-win64.zip.sha256`） |
| 最近额度（17:01 主窗口截图） | 5h 剩余约 41%（11-02 18:08 重置）、每周剩余约 8%（11-04 15:37 重置）、plan=plus、重置卡 3 张 |

> 注意：额度数据会过期，不要拿上面的数字做验收；以程序实时显示为准。

---

## 2. 本对话做了什么（v0.1.8 → v0.1.11，全部已 build / deploy / 发布）

### 2.1 v0.1.8：看守徽标随悬浮条缩放
- 问题：把“悬浮条缩放”调到 50% 时，底部“看守中 N”文字和呼吸灯不跟着缩小（写死 12px/3.6px、行高下限 30px）。
- 修复：`WatchBadge::setScalePercent()`，文字/圆点/光晕/涟漪/间距全部乘缩放系数；`QuotaWidget` 看守行行高与分隔线间距随缩放。100% 时与旧版一致。
- 截图：`docs/widget-scale-50.png`（前后对比）。

### 2.2 v0.1.9：写锁冲突自愈（解决“续跑失败亮红灯”）
- 根因：**Codex 桌面版打开着某个对话时会独占该对话的 thread 写锁（会话级，不是 turn 级）**，本程序独立 app-server 的 `thread/resume` 必然报 `already has an active writer`，旧版重试 3 次后亮红灯。
- 修复要点：
  - 新增 `core/CodexWriterLock.*`：锁探测（`CreateFileW` 独占打开）、rollout 最近活动、系统空闲时间、关闭 Codex 桌面版（WM_CLOSE → 8 秒后 TerminateProcess 进程树）。
  - `JobEngine`：写锁冲突**不计失败次数、不亮红灯**，进入 `writerBlocked` 等待状态并轮询，锁释放立即续跑；`m_ownLoadedThreads` 排除“自己 app-server 已加载线程”的误判。
  - 设置新增 `autoReleaseWriterLock`（默认 true）：冲突宽限 60s + 目标对话空闲 90s + 系统闲置 ≥5min 时，自动关闭桌面版释放写锁。
  - 顺手修：主窗口“立即续跑”不重置 `cycleAttempts`；`showLog` 现在同时写 `auto-run-codex.log`。
- 验证：隔离 APPDATA + 伪造锁文件（只等待 / 自动关闭两条路径），真实 ChatGPT 桌面版进程数前后一致（15→15）。

### 2.3 v0.1.10：活动指示灯 + 写锁提示改进
- 用户反馈：①“等待写锁释放”太抽象，要直接告诉用户去桌面版关闭/切换对话；②“看守中 N”呼吸灯意义不大，改成**当前对话活动指示灯**。
- 活动指示灯（`ui/WatchBadge.*` 重写）：
  | 状态 | 颜色 | 动效 | 判定 |
  |---|---|---|---|
  | 运行中 | 绿黄 | 呼吸+涟漪 | 任一对话 `info.running`（rollout 最近仍在写入） |
  | 等待回复 | 黄色 | 呼吸+涟漪 | 非 running 但 app-server thread 状态 `active`（等模型回复/长思考） |
  | 异常中断 | 红色 | 呼吸+涟漪 | 任一对话 `info.interrupted`（限额中断等） |
  | 完成 | 蓝色 | 呼吸 | 最近 5 分钟内有正常 `task_complete` |
  | 最近结束 | 绿色 | 呼吸 | 最近 5 分钟活动停止但无法确认完成（兜底） |
  | 无活动对话 | 灰色 | 不呼吸 | 以上都没有 |
  - 优先级：异常中断 > 运行中 > 等待回复 > 完成 > 最近结束 > 空闲；被 `ignoredThreadIds` 忽略的对话不参与；5 分钟窗口可用 `AR_ACTIVITY_RECENT_SECONDS` 改。
  - 数据：`RolloutState/ThreadInfo` 新增 `completedAt`（RolloutScanner 解析正常完成时间）；防御了“未来坏时间戳导致永远显示完成”。
  - 主窗口摘要追加「当前：…」；状态变化写 `活动指示灯：…` 日志；截图环境变量 `AR_SCREENSHOT_FAKE_ACTIVITY=`running|waiting|interrupted|completed|recent|idle`。
  - 截图：`docs/activity-indicator.png`（6 状态；100% 与 50% 缩放都检查过文字不溢出）。
- 写锁提示（`JobEngine` + 主窗口详情）：明确写「在 Codex 桌面版里关闭这个对话或切换到其他对话/退出桌面版，锁释放后会自动继续」；日志每约 5 分钟提醒一次；旧版 writer-lock 红灯（state=Failed）也会显示处理方法。

### 2.4 v0.1.11：活动指示灯微调
- 用户反馈：“刚完成”改成“完成”；呼吸灯除了灰色都呼吸。
- 改动：`ActivityState::Completed` 文案改为「完成」；`WatchBadge::stateBreathes()` 改为“非 Idle 即呼吸”——完成（蓝）和最近结束（绿）从常亮改为呼吸+涟漪；只有灰色“无活动对话”不呼吸。
- 重新生成 `docs/activity-indicator.png`（六状态截图，含蓝色/绿色呼吸光晕）。

### 2.5 发布与仓库（GitHub 上传 / 发版流程）
- 项目从“没有 .git”到上传 <https://github.com/Rtyu3221cc/AutoRun_Codex>（public，main 分支），并发了 v0.1.8 / v0.1.9 / v0.1.10 / v0.1.11 四个 Release。
- 新增发布脚本：
  - `release.cmd` → `packaging/make-release.ps1`：从 `dist\` 生成 `release\AutoRun_Codex-vX.Y.Z-win64.zip` + `.sha256`（版本号默认从 `main.cpp` 读取）。
  - `packaging/publish-release.ps1`：从 git credential manager 取 GitHub 凭据（不落盘/不打印），创建 tag/Release 并上传 zip。
  - **发版顺序（重要）：改版本号 → `build.cmd` → `deploy.cmd` → `git commit/push` → `release.cmd` → `publish-release.ps1`**。如果先 publish 再 push，tag 会指向旧 commit（v0.1.9 就踩过，后来用 GitHub API 把 tag 移正）。
- 仓库已 ignore：`build/ build-dsh/ dist/ release/ .qtcreator/ ref/`；`resources/`（含 8.4MB 字体）必须提交，否则别人构建出的界面不对。

---

## 3. 待用户验收 / 下一步建议

1. **活动指示灯验收**：重启后看悬浮条底部——无对话活动应为灰色“无活动对话”不呼吸；Codex 跑对话时绿黄“运行中”；对话结束后 5 分钟内蓝“完成”/绿“最近结束”（都带呼吸）；有历史限额中断对话时为红“异常中断”。对照 `docs/activity-indicator.png`。
2. **写锁提示验收**：对一条看守点“立即续跑”，若桌面版正打开该对话，主窗口详情应显示“在桌面版里关闭这个对话或切换/退出桌面版”，且不亮红灯；关闭对话或离开电脑 5 分钟后自动续跑。
3. **悬浮条缩放复查**（上一轮）：50% 下徽标/文字不溢出，100% 与旧版一致。
4. 可选打磨（按用户反馈优先级）：
   - 写锁等待/自动关闭桌面版加 Windows 托盘通知（现在只有日志）；
   - 活动指示灯“等待回复”判定可再细化（目前 active 状态 + rollout 写入间隔推断）；
   - 经典样式在 50% 时环内中文标题略挤（RingWidget 既有问题）；
   - 失败重试间隔做成设置项；周额度耗尽显式判断；
   - 开机自启（设置项已存在但未写注册表）、单测/协议回归脚本。

---

## 4. 关键决策与背景（避免重复踩坑）

1. **为什么自己起 app-server**：桌面 app 没有可连的控制 socket；独立 app-server 是官方 CLI 路径。代价是单写者模型 → 写锁冲突（v0.1.9 已处理）。
2. **写锁是会话级**：桌面版只要打开着对话就持有锁，即使对话没在跑 turn；所以“等 turn 跑完”不会释放锁，必须关闭该对话/退出桌面版（或程序自动关闭桌面版）。
3. **自动关闭桌面版是“无人值守”妥协**：仅当用户离开（系统闲置 ≥5min）且目标对话空闲 ≥90s 时触发；如果桌面版同时在其它对话跑任务会被一起关掉（Codex 无逐对话释放接口）。可在设置关闭。
4. **活动指示灯是推断**：“运行中/等待回复”依赖 rollout 文件活动 + app-server 状态；“完成/最近结束”是 5 分钟窗口。被忽略的对话不参与；很久以前限额中断没处理的对话会让红灯一直亮（提示去处理）。
5. **旧版遗留 writer-lock 红灯（state=Failed）不会自动重试**：刻意避免擅自续跑用户可能已手动完成的对话；点一次“立即续跑”即可进入等待写锁逻辑。
6. **用户偏好**：非常在意视觉细节（会截图指出）；要中文交流、结论优先；**不要擅自测试正在使用的对话/正在跑任务的 Codex 桌面版**（本对话里做测试时都是隔离 APPDATA + 假锁/假进程）。

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

截图/调试（开发用，环境变量）：
- `AR_SCREENSHOT_DIR=<目录>`：启动 4 秒后抓主窗口/悬浮条/弹卡 PNG 并退出；
- `AR_UI_THEME=dark|light`、`AR_SCREENSHOT_FORCE_PULSE=1`、`AR_SCREENSHOT_FAKE_REMAINING=0,37`、`AR_SCREENSHOT_FAKE_CREDIT_DAYS=1.5` 等见 HANDOFF 第 10 节；
- `AR_SCREENSHOT_FAKE_ACTIVITY=running|waiting|interrupted|completed|recent|idle[:N]`：强制活动指示灯状态；
- `AR_ACTIVITY_RECENT_SECONDS=300`：活动灯“完成/最近结束”窗口；
- 写锁测试：`AR_WRITER_POLL_SECONDS`、`AR_WRITER_GRACE_SECONDS`、`AR_WRITER_IGNORE_USER_IDLE`、`AR_CODEX_DESKTOP_PID=<测试进程PID>`、`AR_AUTOTEST_EXIT_SECONDS=N`（详见 HANDOFF 5.10）；
- 测试务必用隔离 APPDATA（复制一份 settings.json、jobs.json 写 `[]`），避免触发真实看守。

日志：`%APPDATA%\AutoRun_Codex\auto-run-codex.log`（UTF-8；v0.1.9 起看守调度日志以 `[job]` 前缀落盘）。

---

## 6. 关键文件地图

- `core/JobEngine.*`：调度/等待/重试/红灯状态机；v0.1.9 写锁等待（`writerBlocked`、`processWriterBlocked()`、`m_ownLoadedThreads`）。
- `core/CodexWriterLock.*`（v0.1.9）：thread 写锁探测、rollout 最近活动、系统空闲、关闭 Codex 桌面版。
- `core/RolloutScanner.*` / `core/ThreadCatalog.*`：解析 rollout 的 interrupted/running/resetAt，v0.1.10 新增 `completedAt`。
- `core/Types.h`：`ThreadInfo`、`ResumeJob`、`ActivityState`（活动灯状态枚举与文案）。
- `core/SettingsStore.*` / `ui/SettingsDialog.*`：设置读写与设置对话框（v0.1.9 加 `autoReleaseWriterLock`）。
- `ui/WatchBadge.*`（v0.1.10 重写）：悬浮条底部活动指示灯（6 状态颜色/呼吸策略，随缩放等比）。
- `ui/QuotaWidget.*`：悬浮条布局/缩放/看守行；`setActivityStatus()` 转发给徽标。
- `ui/MainWindow.*`：主窗口；`computeActivityState()` 汇总活动灯状态；“立即续跑”重置重试计数；`showLog()` 写文件日志。
- `ui/Theme.*`：颜色口径（v0.1.10 加 `blue()`）。
- `core/CodexClient.*`：app-server JSON-RPC（resume/startTurn/threadList/rateLimits…）。
- `packaging/` + `release.cmd`：发布打包/上传脚本；`docs/activity-indicator.png`、`docs/widget-scale-50.png` 为对应截图。

---

## 7. 已知问题 / 限制

1. **活动灯为推断**：超长工具调用、rollout 尾部截断、坏时间戳可能让“运行中/等待回复/完成”有偏差；已加时间戳防御。
2. **自动关闭桌面版的边界**：只会在用户离开 + 目标对话空闲时触发；桌面版其它对话正在跑任务时会被一起关掉；可在设置关闭（纯等待模式）。
3. **写锁等待时没有托盘通知**：目前只有主窗口详情 + 日志；用户要求可再加气泡提醒。
4. **经典样式 50% 文字拥挤**：RingWidget 环内中文标题在小尺寸贴边（参考/极简不受影响），未修。
5. **旧 writer-lock 红灯不自动重试**：需要手动点“立即续跑”；这是刻意设计，避免擅自续跑已手动完成的对话。
6. 周额度耗尽时 JobEngine 没有专门判断 secondary 100%（仍按 5h resetAt 触发，重试后红灯）。
7. 开机自启设置项未实现（只保存字段，未写注册表）。
8. 没有单元测试工程；弹性测试都是隔离 APPDATA 的 ad-hoc 回归。
9. 日志偶发 `QPainter::end: Painter ended with N saved states`（v0.1.0 起既有，可忽略）。

---

## 8. 交接后第一件事（Checklist）

1. 读同目录 `HANDOFF.md` 的 5.8（字体）/ 5.9（缩放）/ 5.10（写锁自愈）/ 5.11（活动灯）/ 6.3（当前状态）。
2. 确认程序在跑哪个版本：`Get-Process AutoRun_Codex | Select Id,StartTime,Path`；dist exe 哈希对比 build-dsh。
3. 打开主窗口确认“已连接”、额度正常、对话列表有数据；看悬浮条活动指示灯是否符合预期。
4. 如果用户要改视觉/行为：先在隔离 APPDATA 用 `AR_SCREENSHOT_*` 截图验证，再 build → deploy（deploy 前要退出运行实例）→ 让用户重启。
5. 如果发新版：按“改版本号 → build → deploy → git commit/push → release.cmd → publish-release.ps1”的顺序，别把 tag 指到旧 commit。
6. 用户很在意细节，改动后尽量给截图/对比图，并在回复里说明“改的是哪个元素、什么场景会触发”。

---

## 9. 本对话环境 / 协作注意事项（踩过的坑）

- **部署时旧实例锁 exe**：沙箱里结束进程的请求可能被授权策略拦下并挂起；稳妥做法是把 `dist\AutoRun_Codex.exe` 改名为 `.old-vXXX`、复制新 exe 进去，然后请用户从托盘退出重启。
- **PowerShell 5.1 编码**：含中文的 `.ps1` 若以 UTF-8 无 BOM 保存会被按 GBK 解析（乱码/语法错误）；本次改为“脚本 ASCII-only + 中文内容放 UTF-8 文本文件读取”（见 `packaging/make-release.ps1` 读取 `README.txt`）。
- **System.Drawing 变量大小写**：PowerShell 变量名大小写不敏感，`$w` 和 `$W` 是同一个变量，合成截图时曾因此把图拉变形；用 `$bw/$bh` 之类区分。
- **Qt/CMake 坑**：`QPropertyAnimation` 没有 `setDeleteWhenStopped`（用 `start(DeleteWhenStopped)`）；局部变量别叫 `x/y` 会遮蔽 `QWidget::x()/y()`；中文字符串统一 `QStringLiteral`、源文件 UTF-8。
- **没有 .git 的时期**已经过去；现在仓库在 GitHub，改动记得 commit/push（`.gitignore` 已排除构建产物）。
- 用户可能在用 Codex 桌面版跑任务；**不要擅自 resume 用户正在用的对话**，测试只用隔离数据/假锁/假进程。

---

## 10. 一句话交接

v0.1.11 已上线：悬浮条现在是对话活动指示灯（运行中/等待回复/中断/完成/最近结束/空闲；除灰色外都呼吸），写锁冲突自愈且提示清晰；下一个对话接手时先确认版本与运行状态，再按用户新反馈做增量修改 + 隔离测试 + 发版。
