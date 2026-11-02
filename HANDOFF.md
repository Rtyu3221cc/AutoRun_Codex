# AutoRun Codex 对话交接文档

> 最后更新：2026-11-02 18:30（JST）
> 当前版本：v0.1.11（开发中，可运行；活动指示灯微调（完成文案/全状态呼吸）+ 写锁提示改进 + 写锁冲突自愈 + 悬浮条缩放适配 + HarmonyOS 统一字体 + 账号头像/昵称 + 重置卡到期预警 + UI 配色微调 + 续跑重试策略）
> 项目路径：`D:\QtWorkspace\AutoRun_Codex`
> 运行产物：`D:\QtWorkspace\AutoRun_Codex\dist\AutoRun_Codex.exe`
> 代码仓库：<https://github.com/Rtyu3221cc/AutoRun_Codex>（v0.1.8 起；发布包见 GitHub Releases）

> 换对话/新接手请先读 `CONVERSATION_HANDOFF.md`（本对话做了什么、当前停在哪、下一步）。

---

## 1. 项目是什么

一个 Windows 桌面小工具，围绕本机 Codex（ChatGPT 账号）做两件事：

1. **用量显示**：5 小时窗口 / 每周窗口的额度百分比、重置时间、plan、重置卡数量，以及最近一张重置卡的到期剩余时间预警（颜色 + 橙色小点）；弹卡与主窗口标题显示当前用户头像 + 昵称；支持悬浮条、悬浮弹卡、主窗口三条展示路径。
2. **额度重置后自动续跑**：选择任意 Codex 对话加入“看守队列”，在被 5h/周额度打断后，按重置时间自动 `thread/resume` + `turn/start` 继续任务；支持单次 / 持续看守。

项目不自带账号、不代理网络，完全依赖本机已安装的 Codex CLI（`codex.exe`）及其 app-server 协议。

---

## 2. 环境与依赖（已在本机验证）

| 项 | 值 |
|---|---|
| 操作系统 | Windows 10 Pro 19045 x64 |
| Qt | 6.6.2 msvc2019_64（`D:\Qt\6.6.2\msvc2019_64`） |
| 编译器 | MSVC 2019 14.29（`D:\msvc\2019`） |
| 构建工具 | CMake（`D:\Qt\Tools\CMake_64`）、Qt Creator |
| Codex CLI | 自动探测 `%LOCALAPPDATA%\OpenAI\Codex\bin\*\codex.exe`（当前 hash 目录会随更新变化，所以必须自动探测） |
| Codex 版本 | 0.165.0-alpha.1.2（ChatGPT/Codex Desktop 26.1023） |
| CODEX_HOME | `%USERPROFILE%\.codex` |
| 本程序数据目录 | `%APPDATA%\AutoRun_Codex`（settings.json / jobs.json / auto-run-codex.log） |
| 运行权限 | 普通用户即可；无需管理员（开机自启尚未实现） |

---

## 3. 构建与运行

### 3.1 Qt Creator（日常开发）
打开 `CMakeLists.txt`，选择 kit `Desktop Qt 6.6.2 MSVC2019 64bit`，构建运行。

### 3.2 命令行构建
```powershell
cmd /c D:\QtWorkspace\AutoRun_Codex\build.cmd
```
输出：`build-dsh\AutoRun_Codex.exe`。

### 3.3 打包成可双击运行
```powershell
cmd /c D:\QtWorkspace\AutoRun_Codex\deploy.cmd
```
产物：`dist\AutoRun_Codex.exe` + windeployqt 依赖。

> ⚠️ 运行中的实例会锁住 exe，**先退出所有 AutoRun_Codex 进程再 deploy**：
> `Get-Process AutoRun_Codex | Stop-Process -Force`

### 3.4 日志
`%APPDATA%\AutoRun_Codex\auto-run-codex.log`（UTF-8；PowerShell 直接 Get-Content 可能显示乱码，用 UTF-8 打开即可）。

### 3.5 发布打包（v0.1.8 新增）
```powershell
# 先退出运行实例，再生成 dist
cmd /c D:\QtWorkspace\AutoRun_Codex\build.cmd
cmd /c D:\QtWorkspace\AutoRun_Codex\deploy.cmd
# 打发布 zip（默认版本号从 main.cpp 的 setApplicationVersion 读取，也可显式传参）
cmd /c D:\QtWorkspace\AutoRun_Codex\release.cmd
```
- 产物：`release\AutoRun_Codex-vX.Y.Z-win64\`（dist 内容 + `README.txt` 使用说明）、`release\AutoRun_Codex-vX.Y.Z-win64.zip`、同名 `.sha256`。
- 脚本：`release.cmd` → `packaging\make-release.ps1`；使用说明模板在 `packaging\README.txt`（打包时复制为包内 `README.txt`）。
- 发布：`powershell -NoProfile -ExecutionPolicy Bypass -File packaging\publish-release.ps1`（默认读 `main.cpp` 版本、用 `release\_release-notes-<版本>.md` 作为说明，自动创建 tag/Release 并上传 zip；需本机 GitHub 凭据可用）。仓库 <https://github.com/Rtyu3221cc/AutoRun_Codex>。
- 仓库里 `dist/ build/ build-dsh/ .qtcreator/ release/ ref/` 均已 gitignore；源码中 `resources/`（含 8.4MB 字体）需要提交，否则别人构建不出同样界面。

---

## 4. 运行时架构

```
AutoRun_Codex.exe
├─ ui/MainWindow          主窗口：额度条 + 对话列表 + 看守队列 + 日志 + 菜单/托盘
├─ ui/QuotaWidget         悬浮条（参考/经典/极简三种样式）
├─ ui/QuotaCard           悬浮弹卡（点击悬浮条）
├─ ui/*                   环形控件、胶囊进度条、看守呼吸徽标、设置/对话设置对话框
└─ core/
   ├─ CodexPaths          探测 CODEX_HOME / codex.exe / 数据目录
   ├─ JsonRpcProcess      QProcess 启动 codex app-server --stdio，逐行 JSON-RPC
   ├─ CodexClient         初始化、额度读取、thread/list、thread/resume、turn/start、turn/interrupt、goal
   ├─ RolloutScanner      只读扫描 sessions/**/rollout-*.jsonl，提取限额中断/运行中/重置时间
   ├─ ThreadCatalog       合并 thread/list 与 RolloutScanner，输出 ThreadInfo 列表
   ├─ JobEngine           看守队列：状态机、resetsAt 调度、续跑执行、jobs.json 持久化
   └─ SettingsStore       settings.json 读写
```

### 4.1 与 Codex 的交互方式（关键）

程序启动时**自己起一个** `codex app-server --stdio` 子进程（不是去连桌面 app 的实例；桌面 app 没有可连的控制 socket）。

协议要点：
- 逐行 JSON；先 `initialize`（带 `clientInfo` + `capabilities.experimentalApi=true`），再发 `initialized`；
- 请求带自增 id，响应按 id 匹配；服务端通知（notification）没有 id；
- 用到的 method：
  - `account/rateLimits/read`（额度/plan/重置卡；明细在 `rateLimitResetCredits.credits[].expiresAt`）
  - `thread/list`（分页对话列表）
  - `thread/resume`、`turn/start`（续跑）
  - `turn/interrupt`（取消正在运行的续跑）
  - `thread/goal/get|set`（预留，尚未在 UI 中完整使用）
- `turn/start` 带 `approvalPolicy=never`，避免无人值守时卡在审批。

> 独立 app-server 的代价：桌面 app 当前打开的对话可能不会实时刷新，需要重开该对话查看进度；若桌面 app 持有线程写锁，resume 会报 `already has an active writer`——v0.1.9 起 JobEngine 会自动等待锁释放/按设置自动关闭桌面版（见 5.10）。

---

## 5. 核心功能实现

### 5.1 额度显示
- 数据来源：live `account/rateLimits/read` 为主；本地 rollout 里的 `rate_limits` 仅用于检测重置时间兜底。
- 5h 窗口 = `primary.windowDurationMins == 300`，周窗口 = `10080`。
- 颜色规则（唯一取色函数 `theme::quotaColor(usedPercent)`，入参是“已用百分比”，实际按“剩余量”分档）：
  - 剩余 ≥75% → 绿色 `theme::green()`
  - 剩余 50%~75% → 绿黄 `theme::lime()`
  - 剩余 20%~50% → 黄色 `theme::yellow()`
  - 剩余 <20% → 红色 `theme::red()`
  - 这样同一屏上数值接近的两个窗口不会出现一绿一黄；主窗口进度条、悬浮环、弹卡胶囊条共用这一函数。
  - 浅色主题的黄色改为偏橙的琥珀色 `#EA9709`（在上一版 `#D58908` 基础上再提亮约 10%；深色主题仍是 `#F5B942`）；绿/绿黄/红保持原值。
  - 额度耗尽 / 续跑失败的告警仍用红色 `theme::red()`（不是橙色），只把呼吸强度收敛一些：光晕 alpha 0.11~0.31、描边/圆环 0.50~0.80，比最初版本柔和。
- 额度耗尽提醒（剩余 0%）：
  - 对应窗口的进度条/圆环整条（整圈）外侧显示**红色**呼吸灯；主窗口进度条、悬浮环、极简双环、弹卡胶囊条全部一致。
  - 该呼吸与“有进行中对话”的脉冲光晕共用 40ms 定时器/相位，但**不依赖是否有进行中对话**，耗尽时常亮呼吸；额度恢复后自动消失。
  - 主窗口两条额度条已从 `QProgressBar` 换成自绘的 `PillProgress`（与弹卡同款），因此也有圆角、呼吸和耗尽告警。
  - 横向进度条左右两端各留 3px 给光晕（此前 glow 超出控件边界被裁掉，看起来两端“突然中断”）。
- 设置可切换“已用 / 剩余”显示；主窗口、悬浮条、弹卡共用同一模式（有同步守卫）。
- **重置卡到期预警（v0.1.5）**：
  - 数据：`account/rateLimits/read` → `rateLimitResetCredits.credits[]`；取 `status == "available"` 且 `expiresAt > 0` 的**最早到期时间**作为“最近一张可用重置卡”（`nearestResetCreditExpiry()`）。`credits` 为 null/空（只有 `availableCount`）时视为到期时间未知。
  - 分档：`resetCreditUrgency()` 按剩余天数 <2 天 / <7 天 / ≥7 天返回 Critical / Near / Far；`theme::resetCreditColor()` 分别取红 / 黄 / 绿；未知时保持黄色（不误报红/绿）。
  - 应用位置：弹卡“可用 x 张”、主窗口 header“重置卡 N 张”；悬停显示最近到期时间与剩余倒计时。悬浮窗 tooltip 同步显示。
  - 橙色小点：仅 Critical（<2 天）时出现，颜色 `theme::warningDot()`（深色 #F59E0B / 浅色 #EA580C）。**只在悬浮窗**显示，弹卡里不显示（用户反馈去掉）：参考样式贴“周 33%”百分比文字右侧（按最初 3px 的 +70% 口径 ≈ 5px @100%）、经典样式贴周环右上角外沿、极简样式拼在“周 · 剩余 33%”文字后面（4 秒交替显示“周”时出现）。

### 5.2 对话列表与状态
- `thread/list` 拉取标题、cwd、updatedAt、status。
- `RolloutScanner` 只读文件尾部（最多 2MB），识别：
  - `event_msg/task_complete` 且 `error.codex_error_info == "usage_limit_exceeded"` → “限额中断”；
  - 之后又出现 `task_started/turn_started` 或正常 complete → 视为已恢复；
  - 大文件尾部错过 `task_started` 时，用“最后一条不是 turn 结束 + rollout 文件最近 180 秒内仍在写”推断“进行中”；
  - `rate_limits.primary(300min).resets_at` 作为重置时间；没有则用错误文本 `try again at H:MM AM/PM` 兜底。
- 表格支持搜索、状态过滤、点击表头排序（默认最近活动倒序）。

### 5.3 看守队列 / 自动续跑
- 每个对话一条 ResumeJob：prompt、mode（单次/持续）、enabled、state、attempts（累计）、cycleAttempts（本轮）、retryPending、nextTriggerAt。
- “加入看守”时：
  - 若对话是限额中断且已知 resetAt → Waiting 到 `resetAt + 等待秒数`（设置项，默认 150 秒 ≈ 2.5 分钟）；
  - 否则只登记（nextTriggerAt=0），等之后被限额打断时由 `notifyThreadState` 重新排期（同样 + 等待秒数，并且本轮尝试次数清零）。
- **时间到了不会立刻发**：到点后先等 `重置后续跑等待` 秒（默认 150，可设 120~600），避免服务端 reset 时间有延迟导致第一次必失败。
- 若应用在计划时间未运行（错过 nextTriggerAt），重启后下一个 tick 会立即补跑一次，不会重新等待；这是为了不丢已排期的续跑。
- 到点后 JobEngine tick：`thread/resume` → `turn/start(prompt)`，等 `turn/completed`：
  - 正常完成 → Succeeded，本轮尝试次数清零；
  - 恢复线程失败 / 发送消息失败 / turn 仍被限额 → 不立即判死：进入 Waiting，`retryPending=true`，**2 分钟后自动重试**；每轮最多尝试 `retryAttempts` 次（默认 3 = 首次 + 2 次重试）；
  - 3 次都失败 → state=Failed，停止自动重试，悬浮窗“看守中”绿灯变红，主窗口摘要显示“N 个续跑失败（红灯）”；
  - 失败重试的 Waiting 不会被线程列表刷新重排（`retryPending` 守卫）；Failed 也不会再被自动重排，需要手动“立即续跑”或重新加入看守；
  - app-server 未连接 → 30 秒后重试（不消耗本轮尝试次数）。
- 同一时刻默认只跑 1 个续跑任务（串行）。
- 取消看守时若正在续跑，会调用 `turn/interrupt` 中断当前 turn。
- “立即续跑”= Ready，本轮尝试次数清零，下一个 tick 执行。
- 备注：持续看守在“3 次都失败”后也停止自动重排并亮红灯，避免无感地一直等到下一个窗口；用户可手动继续或取消。

### 5.4 悬浮条 UI
- 三种样式（设置 → 悬浮窗样式）：
  1. **参考样式（默认）**：两个环，数字在环下；
  2. **经典样式**：数字在环内；
  3. **极简模式**：外细白弧 = 5h 刷新倒计时，5h 用量环，周用量环，内细白弧 = 周刷新倒计时，中心 OpenAI logo，底部每 3.8s 交替显示 5h/周百分比+刷新时间。
- 拖动/边缘吸附、双击开主窗口、右键菜单、托盘。
- 有进行中对话时，用量环叠加脉冲光晕（40ms 正弦呼吸）。
- 透明背景 + 可调不透明度（设置 30-100%，作用于悬浮条与弹卡背景）。
- “看守中 N”使用 WatchBadge：光晕 + 平滑呼吸 + 只扩散不收缩的涟漪（外圈），文字与圆点整体居中；当存在“重试耗尽仍失败”的看守任务时，呼吸点由绿变红。
- 主题切换时，环形/徽标/极简倒计时弧等自绘控件会主动重绘；极简模式的外/内倒计时弧已从写死白色改为主题色（浅色主题下也能看清）。
- 悬浮条标签颜色不再在构造函数里写死十六进制颜色，统一走 `theme::*`。
- 重置卡临期（最近一张 <2 天）时，“周”百分比旁出现橙色小点：参考样式贴文字右侧（间距约 5px）、经典样式贴周环右上角、极简样式拼进“周 · 剩余 x%”文字。

### 5.5 弹卡
- 点击悬浮条弹出；显示 5h/周进度条、剩余/已用百分比、重置时间、重置卡、看守数、刷新时间。
- “可用 x 张”按最近一张重置卡剩余时间取色：<2 天红、<7 天黄、≥7 天绿；悬停可看最近到期时间。
- 计划胶囊“plus”自绘（`PlanBadge`：圆角胶囊 + 文字按字形墨迹 bbox 垂直居中），解决 QLabel 在 HarmonyOS 字体下文字偏上/偏下的问题。
- 卡片进度条（PillProgress）在“有进行中对话”时同样带呼吸光晕：进度条上下预留 3px 留白，光晕画在留白里（早期实现画在 8px 控件边界外，被裁掉，所以“看不到呼吸”）；强度和节奏与悬浮环一致（40ms tick、约 1.4s 一个周期）。
- 额度耗尽（剩余 0%）时，该条进度条即使填充长度为 0，也会整条外圈显示红色呼吸灯（红色描边 + 外圈柔光，不依赖进行中状态）；左右留 3px 让光晕完整。
- 打开动画：以悬浮条朝向屏幕内部的锚点为中心，窗口几何从 52×28 的小矩形展开到最终尺寸（200ms，OutCubic），内容随窗口裁剪逐步露出。收起/隐藏动画：反向收缩回锚点（180ms，InCubic）。Esc、失焦、鼠标移开、拖动、双击开主窗口等所有隐藏路径都会走收回动画。
- 内容透明度用 `QGraphicsOpacityEffect` 控制，背景 alpha 用 `reveal` 属性控制，两者和几何动画并行。
- **可见期间冻结数据更新**（避免每次额度刷新重建布局造成闪烁），每次打开时同步最新快照；主题切换时显式重建内部控件，文字颜色立刻跟随主题。
- 无“打开主窗口”按钮（双击/右键打开），底部只有一行提示。
- 弹卡与悬浮条共用显示模式、主题、不透明度。

### 5.6 主题
- `ui/Theme.h/.cpp`：全局深/浅色状态 + 调色板/QSS + 语义颜色（含新增 `lime()` 绿黄、`rgba()` QSS 辅助）。
- 设置 → 界面主题，**即时生效**（主窗口、表格、悬浮条、弹卡、日志、设置对话框、线程设置对话框）。
- 浅色主题的黄色档改为偏橙的 `#EA9709`（`theme::yellow()` 分主题返回，深色仍是金黄）；告警保持红色，仅收敛了呼吸强度（没有橙色告警色）。
- 主题切换时会重刷的地方：
  - 主窗口：header/quota 卡片、标题、详情框、日志标题、连接状态色（按是否已连接重新取色）、详情富文本里的强调色；
  - 表格：状态色改为按主题取色（深色亮蓝/浅色深蓝），切换时 viewport 重绘；
  - 悬浮条：标签 stylesheet、环形/徽标等自绘控件 update()；
  - 弹卡：`QuotaCard::applyThemeStyles()` 重建内部控件（此前只调 `setSnapshot` 会因数据未变而提前返回，导致深→浅/浅→深切换后弹卡仍保留旧主题文字颜色）；
  - 设置/线程设置对话框：标题、提示、线程信息、状态行全部改走 `theme::*`（以前写死了深色主题的浅灰，浅色主题下几乎看不见）。
- 所有新增颜色一律走 `arc::theme::*`；新增控件若用 stylesheet 写死颜色，切换主题时不会自动更新，需要挂到 `MainWindow::updateThemeStyles` / `QuotaWidget::applyThemeStyles` / `QuotaCard::applyThemeStyles`。
- 已删除：`main.cpp` 里遗留的 `applyDarkTheme`、`QuotaCard` 里未使用的 `colorForPercent/blendColor` 与旧颜色常量。
- 退出程序：`QApplication::quit()` 在“窗口未激活/非前台”时偶发不退出（自动化截图时能稳定复现），统一改成 `QCoreApplication::exit(0)`；语义等价，退出可靠。

---

### 5.7 账号头像 / 昵称（v0.1.6）

- 弹卡与主窗口标题从“Codex 用量”改为**当前用户头像 + 昵称**（取不到资料时回退原标题）。
- 数据来源（全部只读）：
  1. 本地 `$CODEX_HOME/auth.json` 的 `tokens.id_token`（JWT payload 里 base64 解出 `name`/`email`），**不联网**；
  2. 头像：本地没有缓存（或缓存超过 7 天）时，用本地 `access_token` 对 `https://chatgpt.com/backend-api/me` 发一次 **只读 GET** 拿到 `picture` URL，再 GET 图片缓存到 `%APPDATA%\AutoRun_Codex\account-avatar.png`；之后 7 天内不再联网。
- 网络走系统代理（`QNetworkProxyFactory::setUseSystemConfiguration(true)`，与 Codex 桌面版一致）；请求超时 8 秒，失败自动退化为本地昵称 + 字母头像。
- 安全性：不写 `auth.json`、不刷新 token、不调用 `account/consume`、不消耗额度或重置卡；只读取自己的账号昵称/头像。该 profile 接口属于 ChatGPT 网页端接口（非 app-server 官方 method），属于“读自己的资料”，不会触发限额；若希望**完全零联网**，设 `AR_PROFILE_OFFLINE=1`。
- 已知限制：接口返回的 `name` 与 Codex 桌面 app 里显示的昵称不完全一致（可能是另一个昵称字段，当前接口未返回），如需完全一致可后续加“手动昵称”设置。

---

### 5.8 统一字体（v0.1.7）

- 全软件统一使用 `resources/HarmonyOS_SansSC_TW.ttf`（字体族 **HarmonyOS Sans SC TW**，8.4MB），用 qrc 嵌入 exe，不依赖系统安装；exe 体积从约 1.1MB 增加到约 11.3MB（含字体）。
- 加载：main.cpp 在 QApplication 构造后调用 `theme::installBundledFont()`（QFontDatabase::addApplicationFont(":/resources/HarmonyOS_SansSC_TW.ttf")），族名存到 `theme::uiFontFamily()`；`theme::applyApplicationTheme()` 用 `QFont::setFamilies({HarmonyOS Sans SC TW, Microsoft YaHei UI})` 设全局字体，加载失败自动回退微软雅黑（日志会打 WARN）。
- 同时修掉一个旧 bug：qrc 的 `BASE` 之前没设，资源实际路径变成 `:/resources/resources/xxx`，导致原来的 OpenAI logo 资源一直加载失败且没人发现；本次加 `BASE "resources"` 后 logo 与字体都正常。
- 注意：字体只有 Regular 字重，粗体由 Qt 合成；如果以后要更清晰的粗体，可再补 Bold/Medium 字重文件并按 weight 注册。

---

### 5.9 悬浮条缩放适配：看守徽标 / 呼吸灯（v0.1.8）

- 背景：设置里的“悬浮条缩放”（widgetScale，50%~150%）只缩放了圆环、百分比/时间标签、宽度等；底部 **“● 看守中 N”徽标（ui/WatchBadge）没有跟随缩放**——文字固定 12px、呼吸圆点半径固定 3.6px、布局预留 5px、文字间距 7px、光晕 5+9×breath、涟漪 5+10×ripple、描边 1.6px；同时看守行行高 qMax(30, 34×scale)（极简 28）有固定下限。用户把缩放调到 50% 后，环和标签都变小了，只有“看守中 N”文字和绿色呼吸灯保持原大小，文字偏大并顶到/溢出悬浮条右边界。
- 修复：
  - ui/WatchBadge.h/.cpp：新增 setScalePercent(int)（clamp 50~150）与 m_scalePercent；paintEvent 内上述所有度量乘以缩放系数；sizeHint() 同步。**100% 时数值与旧版完全一致（零视觉变化）。**
  - ui/QuotaWidget.cpp：setScalePercent() 里 m_watchBadge->setScalePercent(m_scale)；看守行行高改为 qMax(17, qRound(34×scale))（极简 qMax(16, qRound(32×scale))）；分隔线间距改为 qMax(2, 5×scale)。
  - 版本升到 v0.1.8（main.cpp）。
- 验证：隔离 %APPDATA% + AR_SCREENSHOT_* 假数据，分别 widgetScale = 50/100/140 截图：
  - 50%：修改前悬浮条 64×190、文字 12px 且右侧被裁；修改后 64×177、文字约 8px、圆点/光晕/涟漪同比缩小、完整居中；
  - 100%：与旧版一致；
  - 140%：徽标随整体放大，比例协调；
  - 参考/经典/极简样式在 50% 下都验证过。
  - 对比图：docs/widget-scale-50.png（左：修改前 50%，右：修改后 50%）。
- 遗留：经典样式在 50% 时圆环内的中文标题（“5h 窗口/每周”）会贴着描边、略显拥挤（RingWidget 内部文字按环径缩放，极小环下没做省略），参考/极简样式不受影响；如需要可后续给 RingWidget 标题加 elide 或小尺寸时隐藏。

---

### 5.10 写锁冲突自愈（v0.1.9）

- **背景（v0.1.8 实际踩到的坑）**：Codex 的 thread store 是单写者模型，桌面版把某个对话加载进会话后会独占 `~/.codex/thread-writer-locks/<threadId>.lock`（会话级持有，对话不在跑 turn 时也持有）。本程序用的是自己拉起的 `codex app-server`，去 `thread/resume` 就会返回 `thread-store conflict: thread ... already has an active writer`。旧版本把它当普通失败：2 分钟间隔重试 3 次都失败 → Failed/红灯，并且会白白消耗重试次数。
- **修复**：
  - 新增 `core/CodexWriterLock.*`：
    - `isThreadWriterLockHeld(threadId)`：用 Win32 `CreateFileW(... share=0)` 探测锁文件是否被独占；可独占打开 = 没有写入者。
    - `isThreadRecentlyActive(threadId, seconds)`：找该线程的 rollout 文件并看 mtime，判断对话是否仍在跑。
    - `systemIdleSeconds()`：GetLastInputInfo，判断用户是否离开电脑。
    - `closeCodexDesktopApp()`：枚举 WindowsApps\OpenAI.Codex 进程树（含桌面版拉起的 codex app-server），先 EnumWindows 发 WM_CLOSE 优雅退出，8 秒内不退则 TerminateProcess 整棵树。
  - `core/JobEngine`：
    - `ResumeJob` 新增 `writerBlocked / writerBlockedSince / writerReleaseAttempted`（持久化到 jobs.json）。
    - 识别写锁：`execute()` 先探测锁（同时排除“本进程 app-server 自己加载过该线程”的情况，见 `m_ownLoadedThreads`）；resume 失败信息里含 `active writer`/`thread-store conflict` 也归入写锁冲突。
    - 进入“等待写锁释放”：state=Waiting、`writerBlocked=true`，本轮重试次数回滚（不消耗 retryAttempts），不亮红灯；每个 tick 轮询锁，锁一释放立刻执行续跑。
    - 自动释放：若开启设置 `autoReleaseWriterLock`，冲突宽限 60 秒后，且目标对话 rollout 已空闲 90 秒、系统闲置 ≥5 分钟时，自动关闭桌面版；关完 5 秒后复查锁是否释放。
    - `notifyThreadState()` 对 writerBlocked 的 job 直接返回，避免被线程刷新重排。
  - `ui/MainWindow`：设置接入；主窗口“立即续跑”现在会重置 `cycleAttempts/retryPending/writerBlocked`（与右键 requestRunNow 一致）；看守队列摘要增加“N 个等待 Codex 桌面版释放写锁”；详情页显示写锁状态说明；`showLog()` 同时写文件日志（`[job] ...`）。
  - `ui/SettingsDialog` + `core/SettingsStore`：新增 `autoReleaseWriterLock`（默认 true，设置里可关）。
- **验证（隔离 APPDATA + 假锁，不碰真实对话/桌面版）**：
  - 测试 1（仅等待）：用 PowerShell 进程在 dummy thread 的锁文件上持锁 20 秒 → 启动 → 8 秒时 jobs.json 为 state=1、`writerBlocked=true`、`attempts=0`、`cycleAttempts=0`；持锁进程释放后 2 秒内自动发起 resume（dummy 线程报 `no rollout found`，属预期）→ 证明“锁释放即恢复”。
  - 测试 2（自动关桌面版）：用 `AR_CODEX_DESKTOP_PID` 指向持锁的测试进程并开启自动释放 → AutoRun 在宽限 3 秒后自动把它结束（真实 ChatGPT 桌面版进程数前后都是 15，未受影响）→ 锁释放 → 立即续跑。
  - 测试开关：`AR_WRITER_POLL_SECONDS` / `AR_WRITER_GRACE_SECONDS` / `AR_WRITER_IGNORE_USER_IDLE` / `AR_CODEX_DESKTOP_PID`（仅指定进程，不改配置）/ `AR_AUTOTEST_EXIT_SECONDS`（自动退出，用于回归脚本）。
- **提示文案（v0.1.10 改进）**：之前“等待写锁释放”太抽象；现在主窗口详情会直接写「在 Codex 桌面版里关闭这个对话或切换到其他对话/退出桌面版」，并说明锁释放后会自动继续；日志每约 5 分钟提醒一次；旧版本 writer-lock 红灯（state=Failed）也会显示“这是旧版本遗留，点立即续跑即可”。
- **已知取舍**：自动关闭桌面版只会在“用户离开 + 目标对话空闲”时触发；如果用户一直坐在电脑前，程序只会等待写锁释放（主窗口/日志显示“等待写锁释放”），不会强行杀掉正在使用的桌面版。若桌面版同时在其它对话里跑任务，无法完全避免影响（设置可关闭该功能）。

---

### 5.11 悬浮条活动指示灯（v0.1.10，v0.1.11 微调）

- **背景**：原来悬浮条底部的“● 看守中 N”呼吸灯只表示“有几个看守任务”，和时间/活动无关；用户要求改成“当前是否有对话正在运行”的全局指示灯。
- **新语义**（`WatchBadge` 重写为活动指示灯，文字+圆点随状态变化，仍随悬浮条缩放等比缩放）：
  | 状态 | 颜色 | 动画 | 判定 |
  |---|---|---|---|
  | 运行中 | 绿黄 `theme::lime()` | 呼吸+涟漪 | 任一对话 `info.running`（rollout 最近仍在写入） |
  | 等待回复 | 黄色 `theme::yellow()` | 呼吸+涟漪 | 非 running，但 app-server thread 状态为 `active`（turn 已开始、rollout 最近无写入=等模型回复） |
  | 异常中断 | 红色 `theme::red()` | 呼吸+涟漪 | 任一对话 `info.interrupted`（限额中断等） |
  | 完成 | 蓝色 `theme::blue()` | 呼吸 | 任一对话最近 5 分钟内 `completedAt`（正常 task_complete） |
  | 最近结束 | 绿色 `theme::green()` | 呼吸 | 最近 5 分钟活动停止、但 `completedAt` 未知（兜底） |
  | 无活动对话 | 灰色 | 不呼吸 | 以上都没有 |
- **v0.1.11 微调**：状态文案“刚完成”→“**完成**”；呼吸规则改为“**除灰色（无活动对话）外全部呼吸**”（完成/最近结束从常亮改为呼吸+涟漪，与其它状态一致）。
- **优先级**：异常中断 > 运行中 > 等待回复 > 完成 > 最近结束 > 空闲；被 `ignoredThreadIds` 忽略的对话不参与统计。窗口默认 5 分钟，可用 `AR_ACTIVITY_RECENT_SECONDS` 覆盖。
- **数据补充**：`RolloutState/ThreadInfo` 新增 `completedAt`（最后一次正常完成的 turn 时间），由 RolloutScanner 解析 `task_complete/turn_complete`（非限额错误）时写入；刻意忽略明显晚于当前时间的坏时间戳（曾导致指示灯永远停在“完成”）。
- **其他**：主窗口摘要末尾追加「当前：运行中/等待回复/…」；活动状态变化时写一条 `[job] 活动指示灯：…` 日志；截图/调试可用 `AR_SCREENSHOT_FAKE_ACTIVITY=running|waiting|interrupted|completed|recent|idle[:N]` 强制某一状态。
- **验证**：截图模式分别渲染 6 种状态（100% 与 50% 缩放），见 `docs/activity-indicator.png`；真实数据下能正确从「无活动对话」切到「完成」（有对话在 5 分钟内正常结束）。语义是推断：等待回复/最近结束为启发式，可能与极端情况有偏差。

---

## 6. 数据与配置

### 6.1 settings.json 字段
```json
{
  "alwaysOnTop": true,
  "autoStart": false,                 // 仅勾选项，尚未写注册表
  "codexCliPath": "",
  "continuousByDefault": false,
  "defaultPrompt": "继续执行未完成的任务",
  "displayRemaining": true,           // 当前按“剩余”显示
  "dockEdge": "right",
  "floatingStyle": "reference",       // reference / classic / minimal
  "ignoredThreadIds": [],
  "jitterSeconds": 150,               // 额度重置后先等待多少秒再续跑（默认 2.5 分钟）
  "retryAttempts": 3,                 // 每轮最多尝试次数（首次 + 重试；失败间隔 2 分钟）
  "autoReleaseWriterLock": true,      // v0.1.9：写锁被桌面版占用且用户离开时，自动关闭桌面版释放写锁
  "minimizeToTray": true,
  "showFloatingWidget": true,
  "uiTheme": "dark",
  "widgetOpacity": 85,
  "widgetScale": 90                  // 悬浮条缩放 50~150（设置里调；当前用户设为 90）
}
```

### 6.2 jobs.json
每个启用的看守任务一条记录；字段：threadId/threadName/prompt/mode/enabled/state/attempts/cycleAttempts/retryPending/nextTriggerAt/lastRunAt/lastError，以及 v0.1.9 新增的 writerBlocked/writerBlockedSince/writerReleaseAttempted（写锁等待状态）。
`state` 枚举：Disabled=0, Waiting=1, Ready=2, Running=3, Succeeded=4, Failed=5, Paused=6。

### 6.3 当前运行状态（交接时）
- **已部署 v0.1.11** 到 `dist\AutoRun_Codex.exe`（2026-11-02 18:18 替换；旧 v0.1.10 exe 备份为 `dist\AutoRun_Codex.exe.old-v010`，可删）。当前运行中的 PID 18676 仍是 v0.1.10（17:17 启动），**退出托盘旧实例重启后才是 v0.1.11**。
- settings.json：`uiTheme=dark`、`widgetScale=90`、`autoReleaseWriterLock=true`、`displayRemaining=true`、`jitterSeconds=120`、`retryAttempts=3`。
- 当前看守队列（交接时）：线程 `01a19b1f…`（生成云间日麻测试 APK），单次模式、状态 Waiting、`nextTriggerAt = 0`（只登记，等该对话下次被限额打断再排期）。
- 之前那条 v0.1.8 遗留的 writer-lock 红灯（线程 `01a18233…`）已被用户移除/替换，不再是当前队列。
- 最近额度（2026-11-02 17:01 JST 主窗口截图）：5h 剩余约 41%（18:08 重置）、每周剩余约 8%（11-04 15:37 重置）、plan=plus、重置卡 3 张。界面为深色主题；启动日志显示 `活动指示灯：无活动对话`。
- 发布状态：GitHub Release v0.1.11 已发布（<https://github.com/Rtyu3221cc/AutoRun_Codex/releases/tag/v0.1.11>；asset 与 SHA256 见 Release 页面或 `release\AutoRun_Codex-v0.1.11-win64.zip.sha256`）。
- 注意：如果某条看守的 nextTriggerAt 已过（>0 且小于当前时间），启动后会“补跑”一次；本机当前那条是 0，所以不会。

---

## 7. 已验证的内容

1. **协议验证**：app-server 拉起 → initialize → rateLimits/read → thread/list → 测试线程 turn/start → thread/resume → turn/completed → thread/delete，全部通过。
2. **实时额度**：拿到 `primary/secondary`、`ordinaryUsageAllowed`、plan=plus、重置卡 availableCount=2。
3. **看守链路集成测试**：临时线程 + 注入 jobs.json（等待 5 秒）→ 启动 app → 自动 resume + turn/start → turn 完成 → jobs.json 变 Succeeded、attempts=1 → 清理线程与 jobs。
4. **UI 渲染**：主窗口、悬浮条（三种样式）、弹卡、深浅主题、透明/不透明度、呼吸灯涟漪、弹卡进度条呼吸光晕均已有截图/实测。
5. **编译**：MSVC2019 + Qt6.6.2 零错误；dist 可双击运行并连接 app-server。
6. **本次 UI 验证（v0.1.1 ~ v0.1.4）**：
   - 四档颜色：用 `AR_SCREENSHOT_FAKE_REMAINING` 渲染 82%/62%（绿、绿黄）和 48%/18%（黄、红），见 `docs/card-color-tiers.png`；
   - 弹卡呼吸光晕：静态截图放大后可见填充外侧柔光，且随 40ms tick 变化；
   - 展开/收回动画：抓取到中间帧（`docs/card-animation.png` 左/右），中间帧尺寸 201×200、307×324，不再是瞬间出现/消失；收回后再次打开仍是完整 320×339；
   - 主题即时切换：弹卡可见时 dark→light 切换，重建后 320×339，文字从浅色变深色，无残留；
   - 深色/浅色主窗口、悬浮条、弹卡四档颜色等截图见 `docs/main-window.png` / `docs/light-main.png` / `docs/light-card.png` / `docs/card-color-tiers.png` / `docs/settings-dialog-light.png` / `docs/minimal-widget-light.png`。
   - 额度耗尽提醒：`AR_SCREENSHOT_FAKE_REMAINING=0,37` 渲染，弹卡/悬浮环/极简双环/主窗口进度条均显示整条**红色**呼吸灯（强度已收敛）、左右光晕不再被裁断，见 `docs/card-exhausted.png`。
   - 浅色黄色：`AR_SCREENSHOT_FAKE_REMAINING=48,18` 浅色渲染，黄色档为 `#EA9709`（比上一版再亮约 10% 的橙琥珀色），见 `docs/card-color-tiers.png` 右图。
   - 看守失败红灯：注入 state=Failed 的 jobs.json → 悬浮条“看守中”呼吸点变红、弹卡显示“1 个续跑失败”、主窗口摘要显示“1 个续跑失败（红灯）”，见 `docs/watch-failed.png`。
7. **重置卡到期预警验证（v0.1.5）**：
   - `AR_SCREENSHOT_FAKE_CREDIT_DAYS=1.5`：弹卡/主窗口“可用/重置卡 N 张”红色，悬浮窗“33%”右侧出现橙色小点（参考样式；经典样式贴在周环右上角、极简样式拼在“周 · 剩余 33%”后）；
   - `=5`：黄色、无小点；`=10`：绿色、无小点；浅色主题同样通过；
   - 截图见 `docs/card-credit-expiry.png`（三档 + 小点）与 `docs/card-credit-expiry-light.png`；真实数据（约 5.4 天 → 黄色）回归一致。
   - 默认悬浮窗样式验证：删除 `settings.json` 启动（`%APPDATA%` 走临时目录）→ 悬浮窗为参考样式（数字在环下）；`AppSettings::floatingStyle` 默认与设置对话框首项都是 reference。
8. **续跑重试验证（v0.1.3+）**：临时 jobs.json 注入不存在的 threadId，设置 `AR_RETRY_DELAY_SECONDS=2` 缩短间隔；启动后确认自动重试 3 次 → jobs.json 变 state=5（Failed）、attempts=3、cycleAttempts=3、retryPending=false、nextTriggerAt=0，日志记录最后一次错误 `no rollout found for thread id`。
10. **统一字体验证（v0.1.7）**：启动日志出现 `[theme] 已加载内置字体: HarmonyOS Sans SC TW`；主窗口/弹卡/悬浮条/对话框文字全部为该字体（对比截图见 `docs/`）；exe 约 11.3MB。
9. **账号资料验证（v0.1.6）**：主窗口 header 与弹卡标题显示头像 + 昵称（本地 id_token 昵称 + ChatGPT 头像）；联网失败或 `AR_PROFILE_OFFLINE=1` 时显示字母头像；不消耗额度、不改写 auth.json。截图见 `docs/main-window.png`、`docs/quota-card.png`。
11. **悬浮条缩放验证（v0.1.8）**：隔离 APPDATA + `AR_SCREENSHOT_FORCE_PULSE=1` + `AR_SCREENSHOT_FAKE_REMAINING=67,30`，分别以 `widgetScale=50/100/140` 启动截图：
   - 50%：修改前 64×190、徽标文字 12px 右侧被裁；修改后 64×177、文字约 8px、圆点/光晕/涟漪同比缩小并居中；
   - 100%：与旧版一致；140%：徽标随整体放大；
   - 参考/经典/极简三种样式在 50% 下均已检查；对比图 `docs/widget-scale-50.png`。
12. **写锁冲突自愈验证（v0.1.9）**：隔离 APPDATA + 伪造 thread-writer-lock，未触碰真实对话与真实桌面版：
   - 仅等待模式：持锁 20 秒期间 jobs.json 保持 `writerBlocked=true / attempts=0 / cycleAttempts=0 / state=Waiting`（不计失败、不亮红灯）；锁释放后 2 秒内自动发起 `thread/resume`（dummy 线程报 `no rollout found`，符合预期）；
   - 自动释放模式：`AR_WRITER_GRACE_SECONDS=3` + `AR_CODEX_DESKTOP_PID=<持锁测试进程>` → AutoRun 在宽限后自动结束该进程、锁释放、立即续跑；真实 ChatGPT 桌面版进程数前后一致（15 → 15）；
   - 日志验证：`[job] ... 被 Codex 桌面版占用 ... / 已自动关闭 Codex 桌面版 ... / 写锁已释放，立即续跑` 均落盘到 auto-run-codex.log。
13. **活动指示灯验证（v0.1.10）**：隔离 APPDATA + `AR_SCREENSHOT_FAKE_ACTIVITY` 分别渲染 6 种状态（100% / 50% 缩放均检查文字不溢出），见 `docs/activity-indicator.png`；真实数据下启动后日志出现 `活动指示灯：无活动对话` → `活动指示灯：完成`（5 分钟内有对话正常结束），并修复过一处“未来时间戳导致永远显示完成”的判定 bug。

---

## 8. 已知问题 / 限制（按优先级）

1. **账号资料接口非官方**：头像读取用了 ChatGPT 网页端 profile 接口（只读、一次性、7 天缓存）。不消耗额度、不改账号数据，封号风险低但并非零；如担心可 `AR_PROFILE_OFFLINE=1` 完全关闭（退化为本地昵称 + 字母头像）。
2. **重置卡到期明细可能缺失**：后端只保证 `availableCount`，`credits` 明细可能为 null 或被截断；此时无法算出最近到期时间，颜色保持黄色、不显示小点（不会误报红/绿）。
3. **“进行中”是推断**：基于 rollout 文件活动；超长工具调用期间可能短暂显示为历史，30 秒内自动纠正。
4. **开机自启未实现**：设置里的开关只是保存字段，没有写 HKCU Run / 计划任务。
5. **重试策略的边界**：每轮最多 `retryAttempts` 次（默认 3），失败间隔固定 2 分钟（暂未做成设置项）；用尽后必须手动“立即续跑”或重新加入看守。持续看守在 3 次失败后也不会自动等下一个窗口（刻意设计为红灯提醒人工介入）。
6. **浅色主题细节**：已修掉写死颜色问题；浅色黄色改为 `#EA9709`（偏橙琥珀色，白底对比度一般，属于观感优先）。
7. **弹窗闪烁**：已做“可见时冻结更新 + 重复数据守卫 + rebuild 关闭刷新”，若仍偶发（自动隐藏/重新打开导致），需继续定位。
8. **死代码**：`core/WindowsAcrylic.*`（毛玻璃方案已废弃）仍未删；`ui/*` 旧取色函数已清理。
9. **QPainter 警告噪声**：`QPainter::end: Painter ended with N saved states` 会在“抓取/打开设置类对话框”时偶发（v0.1.0 起既有，非本轮引入）；截图之外的普通使用也可能出现在日志里，但不影响显示与功能，暂未定位，可忽略。
10. **动画期间的主题切换**：弹卡在展开/收回动画进行中若刚好切主题，会跳过当帧的 adjustSize（避免和动画打架），动画结束后尺寸仍正确。
11. **无单元测试工程**：当前只有 ad-hoc 集成测试；建议补 Qt Test 或 Node 协议回归脚本。
12. **监控/通知**：续跑完成、失败没有 Windows 通知；只有日志和界面状态。
13. **周额度**：周额度耗尽时 JobEngine 没有专门判断 secondary 100%，仍会按 5h resetAt 触发、重试 3 次后转红；后续应显式判断周额度已尽并直接说明。
14. **经典样式小尺寸文字拥挤（v0.1.8 发现，未修）**：悬浮条缩放到 50% 附近时，经典样式圆环里的中文标题（“5h 窗口/每周”）会贴着描边、略显拥挤；RingWidget 内部字号按环径缩放但没有省略/隐藏策略。参考/极简样式不受影响。若用户反馈再处理。
15. **自动关闭桌面版的边界（v0.1.9）**：写锁冲突时默认只会在“系统闲置 ≥5 分钟 + 目标对话空闲 ≥90 秒”时自动关闭 Codex 桌面版；如果桌面版同时在其它对话里执行任务，关闭会打断那些任务（无法逐对话释放写锁）。用户可在设置里关闭该行为（只等待锁释放）。
16. **活动指示灯是推断（v0.1.10，v0.1.11 调整呼吸）**：“运行中”来自 rollout 文件活动，“等待回复”依赖 app-server 的 active 状态 + 最近的 rollout 写入间隔，“完成/最近结束”是 5 分钟窗口启发式；极端情况（超长工具调用、rollout 尾部截断、坏时间戳）会有偏差。被忽略的对话不参与统计；若某条很久以前限额中断的对话一直没处理，红色会持续存在（这是提示去处理它）。

---

## 9. 下一步建议（Backlog）

- [x] 写锁冲突归类与自愈（v0.1.9）：写锁冲突不再消耗重试次数/不亮红灯，自动等待释放；可配置“用户离开时自动关闭桌面版”。（auth/网络类失败仍按普通重试处理）
- [ ] 重试间隔/次数做成设置项（当前间隔固定 2 分钟）；周额度耗尽停止并通知。
- [ ] Windows 通知（托盘气泡）与开机自启（HKCU Run）。
- [ ] 续跑完成后自动刷新主窗口/悬浮条状态。
- [ ] 清理死代码（WindowsAcrylic、未用常量）。
- [ ] 补自动化测试（协议层 + RolloutScanner 解析 + JobEngine 状态机）；v0.1.9 的写锁路径已有两个隔离 APPDATA 的回归脚本思路可复用（见 5.10 验证）。
- [ ] 写锁冲突时用 Windows 通知（托盘气泡）提醒“已自动关闭桌面版/等待中”，而不是只写日志。
- [ ] 浅色主题其它细节打磨（可选“模糊”开关，默认关闭）。
- [ ] 耗尽呼吸灯的颜色/强度/频率如觉得太抢眼，可再调 alpha 与周期。
- [ ] 悬浮条缩放到 50%~70% 时经典样式环内中文标题拥挤（给 RingWidget 标题加 elide 或小尺寸隐藏副标题）。
- [ ] 弹卡动画手感微调（时长/缓动/是否加轻微缩放），呼吸强度如觉得弱可再调 alpha。
- [ ] 考虑“始终呼吸”开关（当前仅在检测到进行中对话时呼吸，与悬浮环一致）。
- [x] Git 初始化与版本管理（v0.1.8 已上传 GitHub：<https://github.com/Rtyu3221cc/AutoRun_Codex>）。
- [ ] 安装包/单文件分发：当前是便携 zip 发布（`release.cmd`）；以后可做 Inno Setup 安装包 / 单文件自解压。

---

## 10. 开发/调试小抄

- **必须先退出运行实例再 deploy**，否则 exe 被占用。
- **截图模式（开发用）**：设置环境变量 `AR_SCREENSHOT_DIR=<目录>` 后启动，程序会在 4 秒后抓取主窗口/悬浮条/弹卡 PNG 并退出；实现见 `MainWindow` 构造函数末尾。常用附加参数：
  - `AR_UI_THEME=dark|light`：强制本次进程主题（不写回 settings.json）；
  - `AR_SCREENSHOT_FORCE_PULSE=1`：强制按“1 个进行中对话”渲染，检查呼吸光晕；
  - `AR_SCREENSHOT_FAKE_REMAINING=82,62`：用假的剩余百分比渲染，检查四档颜色（5h,周）；填 `0,37` 可检查额度耗尽的整条红色呼吸灯；
  - `AR_SCREENSHOT_FAKE_CREDIT_DAYS=1.5`：用假的“最近一张重置卡剩余天数”渲染，验证 <2 天（红 + 橙点）/ <7 天（黄）/ ≥7 天（绿）三档；
  - `AR_PROFILE_OFFLINE=1`：禁用账号资料联网读取（只用本地 auth.json 昵称 + 字母头像）；
  - `AR_SCREENSHOT_DIALOGS=1`：顺带抓取设置 / 线程设置对话框（检查浅色主题可读性）；
  - `AR_SCREENSHOT_SWITCH_THEME=light|dark`：弹卡可见时切换主题，抓 `*-switched.png`；
  - `AR_SCREENSHOT_CLOSE=1`（配合 `AR_SCREENSHOT_REOPEN=1`）：抓收回动画中间帧并在收回后再次打开，验证第二次弹出尺寸正确。
  - **悬浮条缩放验收（v0.1.8）**：在隔离 APPDATA 的 settings.json 里把 `widgetScale` 设为 50 / 100 / 140，配 `AR_SCREENSHOT_DIR` + `AR_SCREENSHOT_FAKE_REMAINING=67,30` 启动，分别抓三种缩放的 `quota-widget.png`。
  - **活动指示灯验收（v0.1.10）**：`AR_SCREENSHOT_FAKE_ACTIVITY=running|waiting|interrupted|completed|recent|idle`（可加 `:N` 指定数量）强制渲染某一状态；也可用 `AR_ACTIVITY_RECENT_SECONDS` 调整“完成/最近结束”的展示窗口（默认 300）。
- 续跑重试/失败告警测试：
  - 用临时 APPDATA + 手写 `jobs.json`（state=Failed, attempts=3）启动，可直接看红灯 UI；
  - 用不存在的 threadId + `AR_RETRY_DELAY_SECONDS=2`（`AR_RESET_WAIT_SECONDS` 可覆盖重置等待）可在 ~30 秒内跑完“3 次失败 → 红色告警”的完整状态机（这里故意用红色，指看守灯的失败告警）。
- 写锁冲突自愈测试（v0.1.9，不碰真实对话/桌面版）：
  - 用 PowerShell 在 `~/.codex/thread-writer-locks/<dummy-thread-id>.lock` 上以独占方式持锁（FileShare.None）模拟桌面版；
  - `AR_WRITER_POLL_SECONDS=2`（轮询间隔）、`AR_WRITER_GRACE_SECONDS=3`（宽限）、`AR_WRITER_IGNORE_USER_IDLE=1`（测试时跳过“用户是否离开”检查）；
  - `AR_CODEX_DESKTOP_PID=<持锁测试进程PID>`：把该进程当作“桌面版”，验证自动关闭路径（不会碰真实 ChatGPT 进程）；
  - `AR_AUTOTEST_EXIT_SECONDS=N`：N 秒后自动退出，方便写回归脚本；
  - 隔离 APPDATA 里放一条 dummy threadId 的 jobs.json（state=2 Ready）即可跑完整状态机。
- 直接抓真实悬浮窗：用 PowerShell 枚举进程的可见窗口（不要按中文标题匹配，脚本编码会乱），取宽 60~200、高 100~450 的窗口，再 `CopyFromScreen`。
- 快速验证额度/线程协议：可参考 DSH 工作区 `.recon` 下的 spike 脚本（`appserver-*.mjs`），它们用 Node 直接起 `codex.exe app-server --stdio` 发 JSON-RPC。
- Qt6 坑：
  - `QPropertyAnimation` 没有 `setDeleteWhenStopped`，用 `start(QAbstractAnimation::DeleteWhenStopped)`；
  - 局部变量不要叫 `x/y`，会遮蔽 `QWidget::x()/y()`；
  - 中文字符串统一 `QStringLiteral`，源文件 UTF-8；PowerShell 控制台显示乱码不代表文件坏。
- 主题相关：新增颜色一律用 `arc::theme::*`；新增控件若用 stylesheet 写死颜色，切换主题时不会自动更新，需要挂到 `MainWindow::updateThemeStyles` / `QuotaWidget::applyThemeStyles` / `QuotaCard::applyThemeStyles`。
- 弹卡调试：`QuotaCard` 内部是“背景（paintEvent，用 reveal 调 alpha）+ 内容子控件（QGraphicsOpacityEffect 调透明度）”两层；展开/收回动画同时驱动 geometry、reveal 和内容效果，改动画时注意别直接 `hide()`，统一走 `dismissAnimated()`。
- 悬浮窗数据更新链路：`CodexClient::rateLimitsUpdated` → `MainWindow::onRateLimitsUpdated` → `QuotaWidget::setSnapshot` → `QuotaCard`（仅在隐藏时同步，防止闪烁）。
- 看守链路：`ThreadCatalog::threadsUpdated` → `MainWindow::onThreadsUpdated` → `JobEngine::notifyThreadState`（只对 interrupted 的线程排期）→ `JobEngine::onTick/execute`。

---

## 11. 对话中的关键决策记录

1. **为什么自起 app-server，而不是连桌面 app**：桌面 app 没有可连接的 control socket（曾尝试 daemon 失败），独立 app-server 是官方 CLI 支持的路径。
2. **为什么不再做毛玻璃**：抓屏+模糊方案造成明显卡顿，Acrylic 在 Win10 的 Qt 分层半透明窗上无效；改为半透明 + 可调不透明度。
3. **为什么弹卡可见时冻结数据**：每次额度刷新重建布局会导致闪烁，冻结+打开时同步能保证稳定。
4. **为什么“进行中”用推断**：独立 app-server 拿不到桌面 app 的 loaded 状态，只能用 rollout 文件活动估计。
5. **为什么续跑用 resume + turn/start 而不是 UI 自动化**：官方协议稳定；UI 注入只作为未来兜底，不做默认。
6. **为什么移除弹卡“打开主窗口”按钮**：双击悬浮条/右键菜单已足够，减少卡片高度与重建时的视觉变化。
7. **为什么颜色按“剩余量”分档**：界面默认按“剩余”显示，用户看到 39% / 42% 两个接近的值却一橙一绿，观感割裂；统一按剩余量分档后同屏一致性更好，且“<20% 红色”与低额度预警直觉一致。
8. **为什么弹卡用“几何 + 透明度”动画**：Qt 顶层窗口没有直接的缩放能力；用窗口 geometry 动画从悬浮条锚点的小矩形展开，叠加背景 alpha 与内容透明度，能达到“从悬浮窗里展开/被吸回去”的效果，且不依赖 QGraphicsEffect 之外的黑科技。
9. **为什么把 `QApplication::quit()` 换成 `QCoreApplication::exit(0)`**：自动化截图场景（进程非前台）下 `quit()` 偶发不退出，`exit(0)` 稳定；两者本意等价（quit 内部也是 exit(0)）。
10. **为什么主窗口进度条也换成 PillProgress**：原来 `QProgressBar` 无法做呼吸光晕与“耗尽整条红色呼吸”的告警；换成与弹卡同款自绘控件后，三处展示（主窗口/悬浮条/弹卡）的配色、呼吸、耗尽提醒完全一致。
11. **为什么耗尽要整条外圈红色呼吸**：剩余 0% 时进度条填充长度为 0，如果只给填充上色，用户看到的是一条空条，反而没有任何“额度已用完”的提醒；把整条外圈做成红色呼吸灯后，空条也能一眼看出该窗口额度耗尽。
12. **为什么红色保留、只收敛呼吸强度**：告警语义靠红色最直观；上一版把红色换成橙色是理解偏了（橙色只用于浅色主题的黄色档）。现在保持红色，但把光晕/描边 alpha 调低，兼顾“醒目”和“不刺眼”；浅色黄色则单独用偏橙的 `#EA9709`。
13. **为什么改成“重置后再等 2~3 分钟”**：Codex 的 resets_at 是服务端给的预计时间，实际可用额度往往有几十秒~几分钟延迟；到点立即 resume 很容易拿到“仍被限额”，白白消耗一次尝试。默认等 150 秒能显著降低第一次就失败的概率。
14. **为什么失败要重试 3 次再亮红灯**：网络抖动 / writer lock / 服务端延迟都可能是一次性的；2 分钟间隔 × 3 次能覆盖大部分瞬时故障。3 次都失败说明不是抖动，改为 Failed + 红灯并停止自动重排，避免用户以为看守正常却实际一直没续上。
15. **为什么红灯后不自动等下一个窗口**：持续看守若静默等待，用户可能很久之后才发现一次都没续上；红灯 + 手动“立即续跑”让失败可见、可决策。
16. **为什么写锁冲突要单独处理（v0.1.9）**：这不是续跑失败，而是 Codex 单写者模型的正常竞争——桌面版打开着对话就必然冲突。把它计入失败次数/亮红灯会误导用户，而且重试多少次都没用。正确做法是等待锁释放，并在用户确实离开时自动释放占用（关闭桌面版）；同时不把我们自己 app-server 已加载线程的写锁误判为冲突。
17. **为什么把“看守中 N”呼吸灯换成活动指示灯（v0.1.10）**：旧灯只表达“有几个看守任务”，既不反映对话是否真在运行，也无法提示异常中断/完成；用户实际关心的是“现在有没有对话在跑/在等我/出问题了”。新指示灯按全局最高优先级显示运行中/等待回复/异常中断/完成/最近结束/空闲，颜色语义与主窗口状态一致；仍然保留“看守队列”数字在弹卡和主窗口里，不丢信息。v0.1.11 起除灰色外所有状态都呼吸，且“刚完成”文案简化为“完成”。

---

## 12. 交接后第一件事（Checklist）

1. 打开 `D:\QtWorkspace\AutoRun_Codex`，确认 Qt Creator kit 能编译。
2. 退出旧实例 → `build.cmd` → `deploy.cmd` → 启动 `dist\AutoRun_Codex.exe`。
3. 打开主窗口确认“已连接”、额度值正常、对话列表有数据。
4. 打开弹卡：确认是“从悬浮条展开”出现，按 Esc / 点别处 / 移开鼠标是收回动画；切换深浅主题后弹卡文字颜色立即变化。
5. 看 5h/周两条进度条颜色是否符合四档规则（剩余 82% 绿、62% 绿黄、48% 橙黄、18% 红）；有进行中对话时进度条应带呼吸光晕；任一窗口剩余 0% 时，该条/该环应整条红色呼吸且两端不被裁断。
6. 选中一个“限额中断”对话 → 加入看守（或立即续跑）验证一次真实续跑（会消耗少量额度）；观察日志里“重置后再等 N 秒”“本轮第 X/Y 次”和失败重试记录。
7. 读日志 `%APPDATA%\AutoRun_Codex\auto-run-codex.log` 确认无异常。