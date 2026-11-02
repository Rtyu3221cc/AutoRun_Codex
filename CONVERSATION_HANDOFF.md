# AutoRun Codex · 对话交接文档（换对话前先读我）

> 用途：换新的 AI 对话时，把本文件路径发给新对话，就能接着干。
> 最后更新：2026-11-02 17:15（JST）
> 项目路径：`D:\QtWorkspace\AutoRun_Codex`
> 代码仓库：<https://github.com/Rtyu3221cc/AutoRun_Codex>（v0.1.8 首次上传）
> 当前版本：**v0.1.10**（已 build + deploy 到 `dist\AutoRun_Codex.exe`；需重启程序生效；将上传 GitHub Release）
> 技术细节看同目录 `HANDOFF.md`；本文件只记录“这个对话里做了什么、现在停在哪、下一段对话该怎么接”。

---

## 0. 一句话背景

Windows 桌面小工具：通过本机 Codex CLI 的 app-server JSON-RPC 显示 5h / 每周额度，并在额度重置后自动续跑指定对话。

---

## 1. 本对话（2026-11-01 晚）做了什么：重置卡到期预警（v0.1.5）

用户需求：检测重置卡的剩余时间——最近一张剩余 <2 天时“可用 x 张”显示红色，并且在“周用量百分比”旁显示一个橙色小点；<7 天黄色不显示小点；≥7 天绿色。

实现：
- **数据层**（`core/Types.h`）：新增 `nearestResetCreditExpiry()`、`ResetCreditUrgency`（Unknown/Far/Near/Critical）、`resetCreditUrgency()`。数据来自 `account/rateLimits/read` 的 `rateLimitResetCredits.credits[]`（`status == "available"` 且 `expiresAt > 0` 取最早）；只剩 `availableCount`、没有明细时视为 Unknown。
- **取色**（`ui/Theme.*`）：`theme::resetCreditColor()`：<2 天红、<7 天黄、≥7 天绿、未知黄；`theme::warningDot()`：橙色小点（深色 #F59E0B / 浅色 #EA580C）。
- **弹卡**（`ui/QuotaCard.cpp`）：“可用 x 张”按上述取色；<2 天时“每周窗口”行最右侧（靠右对齐）加橙色小点；悬停显示“最近一张重置卡到期：MM-dd HH:mm / 剩余 x 天 x 小时”。
- **主窗口**（`ui/MainWindow.*`）：header 里新增独立 `m_creditLabel`（“· 重置卡 N 张”）按同一口径取色；tooltip 显示最近到期时间。
- **悬浮窗**（`ui/QuotaWidget.*`）：<2 天时周百分比旁出现橙色小点（弹卡里也有一个，两处同口径），三种样式分别定位：
  - 参考样式（默认）：贴在“周 33%”百分比文字右侧（用 QFontMetrics 算文字宽度；用户多轮反馈后间距最终 ×3.33：3px→10px @100%）；
  - 经典样式：贴在周环右上角外沿；
  - 极简样式：拼在交替显示的“周 · 剩余 33%”文字后（4 秒一轮，只在显示“周”时出现）。
  悬浮窗 tooltip 也加了“最近一张重置卡 %1 到期，剩 %2”。
- **截图/验收**：`MainWindow` 截图模式新增 `AR_SCREENSHOT_FAKE_CREDIT_DAYS=<天数>`，配合 `AR_SCREENSHOT_FAKE_REMAINING` 可稳定渲染三档；截图骨架里补了一次 `QApplication::processEvents()`，避免抓到“文字已改但布局还没激活”的截断画面。
- **文档/截图**：新增 `docs/card-credit-expiry.png`（三档：1.5 天红+橙点 / 5 天黄 / 10 天绿）与 `docs/card-credit-expiry-light.png`（浅色 1.5 天）；刷新了 `docs/main-window.png`、`docs/light-main.png`；`README.md` / `HANDOFF.md` 已更新到 v0.1.5。
- **版本**：`main.cpp` `setApplicationVersion("0.1.5")`。

验证结果（全部实拍）：
- 1.5 天：弹卡/主窗口“可用/重置卡 N 张”=红色，弹卡“每周窗口”行最右侧（靠右对齐）+ 悬浮窗周百分比右侧=橙色小点（三种悬浮样式都验证过）；
- 5 天：黄色、无小点；10 天：绿色、无小点；
- 浅色主题同样通过；
- 真实数据（2026-11-01 21:47 JST）：重置卡 2 张，最近一张 11-07 00:34 到期（约 5.4 天）→ 黄色、无小点，与预期一致。

> 说明：用户只说了“周用量百分比旁边”，没有明确是弹卡还是悬浮窗；本次**两处都加了**（弹卡“每周窗口”行最右端 + 悬浮窗“周”百分比旁），两处同口径、只在 <2 天时出现。若用户只想要一处，删掉 `QuotaCard` 的 dot 或 `QuotaWidget::updateCreditDot()` 即可。

### 1.1 统一字体（v0.1.7）

- 用户要求全软件统一使用 `HarmonyOS_SansSC_TW.ttf`（字体族 HarmonyOS Sans SC TW）。实现：字体文件放到 `resources/`，CMake 用 qrc 嵌入（BASE "resources"），main.cpp 调 `theme::installBundledFont()`，`theme::applyApplicationTheme()` 设全局字体，失败回退微软雅黑。
- 顺带修掉旧 bug：qrc 没设 BASE，资源路径实际是 `: /resources/resources/...`，原来的 OpenAI logo 也一直没加载成功；现在正常。
- exe 从 ~1.1MB 变为 ~11.3MB（字体 8.4MB）。字体只有 Regular 字重，粗体是 Qt 合成的。
- 计划胶囊“plus”：QLabel 在 HarmonyOS 字体下文字不居中 → 改成自绘 `PlanBadge`（圆角胶囊 + 按字形墨迹 bbox 垂直居中）。
- 全量截图已用新字体重拍（`docs/main-window.png`、`docs/quota-card.png`、`docs/quota-widget.png`、`docs/light-main.png`、`docs/light-card.png`、`docs/light-widget.png`、`docs/card-credit-expiry*.png`）。

### 1.2 后续反馈（2026-11-01 22:05 ~ 23:40，已修）

- 橙点间距：80% → 200% 太大 → 85% → “太大” → **最终按最初 3px 的 +70% ≈ 5px @100%**（用户口径每次都是相对最初值，不是相对上一版累乘）。
- 去掉**弹卡**里的橙色小点（用户要求）；橙点只在悬浮窗周百分比旁显示。
- 账号头像/昵称（v0.1.6）：
  - 弹卡与主窗口标题原“Codex 用量”改为**当前用户头像 + 昵称**（取不到时回退原标题）；
  - 昵称/邮箱：只读本地 `$CODEX_HOME/auth.json` 的 id_token（不联网）；
  - 头像：本地无缓存/超过 7 天时，用本地 access_token 向 `https://chatgpt.com/backend-api/me` 发一次**只读 GET** 取 `picture` URL，再下载图片缓存到 `%APPDATA%\AutoRun_Codex\account-avatar.png`（7 天内不再联网）；网络走系统代理；
  - 不写 auth.json、不刷新 token、不调用 consume、不消耗额度/重置卡；`AR_PROFILE_OFFLINE=1` 可完全禁用联网（退化为本地昵称 + 字母头像）；
  - 已知限制：profile 接口返回的昵称与 Codex 桌面 app 显示的不完全一致（可能是另一个昵称字段，当前接口没返回），要完全一致可后续加“手动昵称”设置。
- 新增文件：`core/AccountProfile.*`、`ui/AvatarUtils.*`；CMake 增加 Qt6::Network；main.cpp 开启系统代理。
- 版本升到 v0.1.6。

### 1.2 第二轮反馈（2026-11-01 22:05，已修）
- 悬浮窗百分比文字与橙点距离太近 → 先 ×1.8（3px → 5px @100% 缩放），仍看不出差别；
- 第三轮反馈（22:28）再加大 200% → 16px，用户觉得太大；
- 第四轮反馈（22:39）改为相对上一版 +85% → 最终 ×3.33（3px → 10px @100%），并给小点加了“不超出悬浮窗右边界”的收缩保护。
- 弹卡橙点改为**靠右对齐**：放在“每周窗口”行最右端（重置时间之后）。
- 用户要求默认悬浮窗样式为“数字在环下”：代码默认值本来就是 `reference`（参考样式/数字在环下），已用删除配置的 fresh 启动再验证一次，并在文档中固化。

### 1.3 本轮修复（2026-11-02 00:10 ~ 00:45）：看守徽标/呼吸灯不随悬浮条缩放（v0.1.8）

用户反馈：把设置里的“悬浮条缩放”调小（当前 `settings.json` 里 `widgetScale = 50`）后，悬浮条底部“看守中 N”文字和它的绿色呼吸灯没有跟着一起缩小。

原因：
- `ui/WatchBadge`（悬浮条底部“● 看守中 N”徽标）内部所有尺寸都是写死像素：文字 12px、圆点半径 3.6px、布局预留 5px、文字间距 7px、呼吸光晕 `5+9×breath`、涟漪 `5+10×ripple`、描边 1.6px；
- `ui/QuotaWidget::setScalePercent()` 里看守行行高是 `qMax(30, 34×scale)`（极简 28），有固定下限，50% 时行高仍是 30px；
- 结果：环、百分比/时间文字都缩小了，只有“看守中 N”文字和呼吸灯保持原大小，50% 时文字显得很大、还会顶到/溢出悬浮条右边界。

修复（v0.1.8）：
- `ui/WatchBadge.h/.cpp`：新增 `setScalePercent(int)`（内部 clamp 50~150）与 `m_scalePercent`；`paintEvent` 内文字像素、圆点半径、布局预留、文字间距、光晕半径、涟漪半径、涟漪描边宽度全部乘缩放系数；`sizeHint()` 同步缩放。**100% 时所有数值与旧版完全一致，视觉零变化。**
- `ui/QuotaWidget.cpp`：`setScalePercent()` 里调用 `m_watchBadge->setScalePercent(m_scale)`；三种样式的看守行行高改为随缩放（`qMax(17, 34×scale)`，极简 `qMax(16, 32×scale)`），50% 时 17/16px；看守计数上方分隔线的间距也按 `5px×scale`（最小 2px）。
- 版本：`main.cpp` `setApplicationVersion("0.1.8")`。

验证（隔离 `%APPDATA%` + `AR_SCREENSHOT_*` 假数据，同一份数据分别 `widgetScale = 50 / 100 / 140` 启动）：
- 50%：修改前悬浮条 64×190、“看守中 1”按 12px 绘制且右侧被裁；修改后 64×177、文字约 8px、圆点和光晕/涟漪同比缩小、完整居中不再溢出；
- 100%：与旧版逐像素一致（只是代码路径变了）；
- 140%：徽标随放大，比例协调；
- 参考/经典/极简三种样式在 50% 下都检查过（经典样式环内中文标题在极小环里偏挤，是 RingWidget 既有问题，见 HANDOFF 已知问题）；
- 对比图：`docs/widget-scale-50.png`（左：修改前 50%，右：修改后 50%）。

### 1.4 发布上架（2026-11-02 00:50 ~ 01:10）：GitHub 仓库 + v0.1.8 发布包

用户要求：以当前代码出一个可以发给别人用的版本，并把代码上传到 <https://github.com/Rtyu3221cc/AutoRun_Codex>（仓库此前已由用户建好，空仓库、public、默认分支 main）。

做的事：
- **发布打包脚本**（新增，便于以后每个版本复用）：`release.cmd` → `packaging\make-release.ps1`；
  - 版本号默认从 `main.cpp` 的 `setApplicationVersion("x.y.z")` 正则读取，也可 `release.cmd 0.1.9` 显式指定；
  - 把 `dist\`（windeployqt 依赖）复制到 `release\AutoRun_Codex-vX.Y.Z-win64\`，排除 `*.old-*`，附上 `packaging\README.txt`（面向最终用户的中文使用说明）→ 包内 `README.txt`；
  - `Compress-Archive` 生成 `release\AutoRun_Codex-vX.Y.Z-win64.zip` + 同名 `.sha256`。
- **发布包自检**：用隔离 APPDATA 跑 `AR_SCREENSHOT_DIR` 截图模式，确认发布目录里的 exe 能独立启动（不依赖本机 Qt 环境）。
- **Git 上传**：项目原本没有 `.git`；`.gitignore` 增加 `build/`、`dist/`、`release/`、`.qtcreator/`、`ref/`、`*.old-*` 等（构建产物/设计参考图不上传），提交源码 + docs + resources（含 8.4MB 字体，构建必需）并 push 到 main。
- **git 上传结果**：main 分支首个 commit `a578734`（75 files，约 9.6MB；`dist/ build/ release/ ref/ .qtcreator/` 均 ignore），tag `v0.1.8` 指向同一 commit。
- **发布上传脚本**（新增，可复用）：`packaging\publish-release.ps1` —— 从 git credential manager 取 github.com 凭据（不落盘、不打印），GET Release，不存在则创建 tag/Release，再上传 zip；asset 已存在则跳过。
- **GitHub Release**：<https://github.com/Rtyu3221cc/AutoRun_Codex/releases/tag/v0.1.8>
  - asset：`AutoRun_Codex-v0.1.8-win64.zip`，45,893,237 bytes（约 43.8MB）；
  - SHA256：`6C0FEEA1ADE643BDD7CD5AE576A2D884A8F9F93E82B35DD2890462F9CFD6A7CF`；
  - 已实测 asset 可下载（HTTP 200）、README 中文在 GitHub 上渲染正常、仓库根目录无构建产物。
- 文档：README 增加“下载 / 发布版”“发布流程”两节；HANDOFF 增加 3.5 发布打包；同步清理了文档里的个人路径/昵称（如实名昵称、`C:\Users\...` 等改成通用写法）。

后续发版流程（每版）：改版本号 → `build.cmd` → `deploy.cmd` → `release.cmd` → `powershell -File packaging\publish-release.ps1`（发布说明写到 `release\_release-notes-<版本>.md`）→ `git commit/push`。

### 1.5 写锁冲突自愈（2026-11-02 12:20 ~ 13:00，v0.1.9）

用户反馈：看守任务续跑失败（红灯），问原因。排查结论：**Codex 桌面版打开着该对话，独占 thread 写锁**，本程序独立 app-server 的 `thread/resume` 被拒绝：`thread 01a18233... already has an active writer`。证据：
- `~/.codex/thread-writer-locks/01a18233-....lock` 从 2026-11-01 21:05 桌面版启动后一直存在，且当时（11-02 12:16）仍无法被独占打开；
- rollout 时间线显示该对话 11-01 23:46 之后没有 turn 在跑，02:08 首次续跑时锁仍被占用 → 锁是**会话级**的（桌面版打开着对话就持有），不是 turn 级；
- 02:08/02:10/02:12 三次自动重试、07:31 一次手动重试都撞锁 → Failed；07:32 用户手动在桌面版继续了该对话。

修复（v0.1.9）：
- `core/CodexWriterLock.*`：锁探测（CreateFileW 独占打开）、rollout 最近活动判断、系统空闲时间、关闭 Codex 桌面版（WM_CLOSE → 8 秒后 TerminateProcess 进程树）。
- `JobEngine`：写锁冲突不再算失败（回滚本轮尝试次数、不亮红灯），进入 `writerBlocked` 等待状态并轮询，锁释放即自动续跑；新增设置 `autoReleaseWriterLock`（默认开）：冲突宽限 60 秒 + 目标对话空闲 90 秒 + 系统闲置 ≥5 分钟时，自动关闭桌面版释放写锁。
- 排除“自己的 app-server 已加载线程”的误判（`m_ownLoadedThreads`）。
- 顺手修：主窗口“立即续跑”没重置 `cycleAttempts`，一次失败就直接红灯；`showLog` 现在同时写 `auto-run-codex.log`（以前 job 日志只在 UI 里）。
- 验证：两个隔离 APPDATA + 伪造锁文件的回归（只等待 / 自动关闭），真实 ChatGPT 进程数前后一致（15 → 15）；测试开关写在 HANDOFF 5.10。
- 发版：v0.1.9 已 build/deploy、打 zip 并上传 GitHub Release（v0.1.9）。

### 1.6 活动指示灯 + 写锁提示改进（2026-11-02 16:50 ~ 17:15，v0.1.10）

用户两条反馈：
1. “等待写锁释放”提示太抽象，应该直接告诉用户去桌面版把当前对话关掉或切换一下；
2. 呼吸灯（原“看守中 N”）意义不大——改成“当前是否有对话正在运行”的全局指示灯：
   - 有对话在运行 → 绿黄（呼吸）
   - 有对话在等待回复 → 黄色（呼吸）
   - 有对话异常中断 → 红色（呼吸）
   - 对话正常结束 → 蓝色（常亮）
   - 若无法确认是否完成，但最近 5 分钟内有对话刚结束 → 绿色（常亮）
   - 没有在活动的对话 → 灰色、不呼吸

实现（v0.1.10）：
- `ui/WatchBadge.*` 重写：不再显示“看守中 N”，改为“● 状态文字”（运行中/等待回复/异常中断/刚完成/最近结束/无活动对话）；颜色见上；只有“活动类”三种状态呼吸+涟漪，其余常亮/不呼吸；保留随悬浮条缩放等比缩放。
- 数据：`RolloutState/ThreadInfo` 新增 `completedAt`（最后一次正常 task_complete 时间，由 RolloutScanner 解析）；`ui/MainWindow::computeActivityState()` 按 异常中断 > 运行中 > 等待回复 > 刚完成 > 最近结束 > 空闲 的优先级统计（`ignoredThreadIds` 不参与）；主窗口摘要追加「当前：…」，状态变化写日志。
- “等待回复”= app-server thread 状态 active 但 rollout 最近没有写入（等模型回复/长思考）；“运行中”= rollout 最近仍在写。
- 写锁提示（v0.1.10）：主窗口详情改成“在 Codex 桌面版里关闭这个对话或切换到其他对话/退出桌面版，写锁释放后 AutoRun 会自动继续”；日志每约 5 分钟提醒一次；旧版遗留的 writer-lock 红灯也会显示处理方法。
- 顺手修：坏时间戳（晚于当前时间）会让活动指示灯永远停在“刚完成” → 在 scanner 和判定层都加了防御。
- 验证：`AR_SCREENSHOT_FAKE_ACTIVITY=running|waiting|interrupted|completed|recent|idle` 截图 6 种状态（100% 与 50% 缩放），保存为 `docs/activity-indicator.png`；真实数据下日志正确出现“无活动对话/刚完成”。
- 版本 0.1.10；将打 zip 发 GitHub Release。

---

## 2. 当前配置与实机状态（交接时）

`%APPDATA%\AutoRun_Codex\settings.json` 关键项：
- `uiTheme = dark`，`displayRemaining = true`，`floatingStyle = reference`，`widgetOpacity = 85`，`dockEdge = right`，`widgetScale = 90`（范围 50~150；验收缩放时用户调过 50/90）
- `jitterSeconds = 120`（重置后等待；最小 120），`retryAttempts = 3`
- v0.1.9 新增 `autoReleaseWriterLock`（默认 true）：写锁被桌面版占用且用户离开时自动关闭桌面版释放写锁；旧 settings.json 没有该字段时按 true 处理

真实额度（11-01 23:30 左右抓取）：5h 剩余约 **67%**（11-02 02:06 重置）、每周剩余约 **30%**（11-04 15:37 重置）、plan=plus、重置卡 2 张（最近到期 11-07 00:34 ≈ 5.4 天，黄色档）。

> **部署状态（2026-11-02 17:20 更新）**：v0.1.10 构建产物已放进 `dist\AutoRun_Codex.exe`（旧 v0.1.9 exe 改名 `dist\AutoRun_Codex.exe.old-v019`；当前运行中的 PID 18764 是 v0.1.9）。**退出托盘旧实例 → 重新启动 `dist\AutoRun_Codex.exe` 即为 v0.1.10**；之后可删 `.old-v019`。
> 用户已在 16:47 用 v0.1.9 重启过（所以设置里已有 `autoReleaseWriterLock = true`）。
> v0.1.9 Release：<https://github.com/Rtyu3221cc/AutoRun_Codex/releases/tag/v0.1.9>；v0.1.10 发布记录见 Release 页面。

---

## 3. 看守队列当前状态（启动前必看！）

`jobs.json` 里现在是 **1 条启用的单次看守**（用户已把之前那条 writer-lock 红灯任务移除/替换）：
- 线程：`01a19b1f-e721-7d83-ab34-19a2882ee8aa`（生成云间日麻测试 APK）
- state=Waiting、enabled=true、`nextTriggerAt = 0`（只登记，等该对话下次被额度限额打断时再按 resetAt+120s 排期；重启不会提前补跑）。
- 之前那条 `01a18233…`（writer-lock 红灯）已不在队列；对应问题详见 1.5/1.6。
- 交接时程序仍在运行 v0.1.9（PID 18764）；**需要重启一次才是 v0.1.10**（活动指示灯 + 写锁提示改进）。

---

## 4. 下一步建议

1. **等用户验收 v0.1.10 活动指示灯**：重启后看悬浮条底部：无对话活动时应为灰色“无活动对话”不呼吸；让 Codex 跑一个对话时变绿黄“运行中”；对话刚结束 5 分钟内变蓝“刚完成/最近结束”；有历史限额中断对话时为红“异常中断”。对照截图 `docs/activity-indicator.png`。
2. **等用户验收 v0.1.9/v0.1.10 写锁提示**：对一条看守点“立即续跑”——若桌面版正打开该对话，主窗口详情应明确写“在桌面版里关闭这个对话或切换到其他对话/退出桌面版”，且不亮红灯；关闭对话/离开电脑 5 分钟后自动续跑。
2. **等用户验收（上一轮遗留）**：悬浮条缩放（50%~150%，对比图 `docs/widget-scale-50.png`）、橙点间距（5px）、弹卡无橙点、账号头像/昵称展示；若想显示桌面 app 里的 “Changc” 昵称，需要再找字段或加“手动昵称”设置。
3. 可选打磨：
   - 写锁冲突时加 Windows 托盘通知（“已自动关闭桌面版/正在等待写锁”），而不是只写日志；
   - “可用 x 张”后面直接显示最近到期时间（现在只在 tooltip 里）；
   - 给到期预警加设置开关/阈值可配置（当前阈值写死 2 天 / 7 天）；
   - **悬浮条缩放全量验收**：经典/极简样式在 50% 下的文字拥挤（经典环内中文标题）可再打磨；
   - Windows 通知（托盘气泡）——续跑成功/失败/红灯时提醒；
   - 失败重试间隔（当前固定 2 分钟）做成设置项；
   - 周额度耗尽时显式判断 secondary 100% 并提示；
   - 清理死代码 `core/WindowsAcrylic.*`；
   - 补自动化测试（协议、RolloutScanner、JobEngine 状态机），项目还没有 .git。
4. 注意：`accessibility`/托盘/自启等旧待办见 `HANDOFF.md` 第 9 节。

---

## 5. 常用命令 / 调试入口

```powershell
# 构建（MSVC2019 + Qt 6.6.2，NMake）
cmd /c D:\QtWorkspace\AutoRun_Codex\build.cmd
# 打包到 dist（先退出 AutoRun_Codex，否则 exe 被占用）
Get-Process AutoRun_Codex | Stop-Process -Force
cmd /c D:\QtWorkspace\AutoRun_Codex\deploy.cmd
# 直接运行
D:\QtWorkspace\AutoRun_Codex\dist\AutoRun_Codex.exe
```

截图/验收（开发用，env 变量）：
- `AR_SCREENSHOT_DIR=<目录>`：启动 4 秒后抓主窗口/悬浮条/弹卡并退出；
- `AR_UI_THEME=dark|light`：强制本次主题（不写回 settings.json）；
- `AR_SCREENSHOT_FORCE_PULSE=1`：强制“有进行中对话”呼吸；
- `AR_SCREENSHOT_FAKE_REMAINING=0,37`：假剩余百分比（0,37 = 5h 耗尽 + 周 37%）；
- **`AR_SCREENSHOT_FAKE_CREDIT_DAYS=1.5|5|10`（v0.1.5 新增）**：假“最近一张重置卡剩余天数”，验证 <2 天（红+橙点）/ <7 天（黄）/ ≥7 天（绿）；
- `AR_SCREENSHOT_DIALOGS=1`、`AR_SCREENSHOT_SWITCH_THEME=light|dark`、`AR_SCREENSHOT_CLOSE=1`（+ `AR_SCREENSHOT_REOPEN=1`）等见 HANDOFF 第 10 节。
- 截图测试建议用隔离的 `APPDATA`（复制一份 settings.json、`jobs.json` 写 `[]`），避免真的触发看守任务。
- **悬浮条缩放验收**：在隔离 `APPDATA` 的 settings.json 里把 `widgetScale` 改成 50 / 100 / 140，配 `AR_SCREENSHOT_DIR` + `AR_SCREENSHOT_FORCE_PULSE=1` + `AR_SCREENSHOT_FAKE_REMAINING=67,30` 启动，会分别生成对应缩放的 `quota-widget.png`（`AR_SCREENSHOT_FORCE_PULSE` 会让“看守中 1”出现，便于检查徽标与呼吸灯）。

---

## 6. 关键文件地图（本对话相关）

- `core/Types.h`：`ResetCredit`/`RateLimitSnapshot` + 本次新增的 `nearestResetCreditExpiry()`/`ResetCreditUrgency`/`resetCreditUrgency()`
- `ui/Theme.*`：`resetCreditColor()`、`warningDot()`（全项目唯一取色口径）
- `ui/QuotaCard.*`：弹卡“可用 x 张”取色 + tooltip
- `ui/QuotaWidget.*`：悬浮窗橙色小点（`m_creditDot`、`updateCreditDot()`）、tooltip、极简样式文字小点；`setScalePercent()` 里把缩放传给看守徽标并计算看守行行高（v0.1.8）
- `ui/WatchBadge.*`：悬浮条“● 看守中 N”徽标；`setScalePercent()` + `m_scalePercent`（v0.1.8 新增）让文字/圆点/呼吸光晕/涟漪随缩放等比变化
- `ui/MainWindow.*`：header `m_creditLabel` 取色、截图 fake credit 数据、processEvents
- `core/CodexClient.cpp`：`parseRateLimits()` 早已解析 `rateLimitResetCredits`（本次未改）
- `ui/QuotaWidget.*` / `ui/QuotaCard.*` / `ui/PillProgress.*` / `ui/RingWidget.*` / `ui/DualRingWidget.*` / `ui/WatchBadge.*`：上一轮的 UI 相关文件
- `core/JobEngine.*`：调度/等待/重试/红灯状态机；v0.1.9 新增写锁等待（writerBlocked）、`processWriterBlocked()`、`m_ownLoadedThreads`
- `core/CodexWriterLock.*`（v0.1.9 新增）：thread 写锁探测、rollout 最近活动、系统空闲时间、关闭 Codex 桌面版
- `ui/SettingsDialog.*` / `core/SettingsStore.*`：v0.1.9 新增 `autoReleaseWriterLock` 设置项
- `ui/WatchBadge.*`（v0.1.10 重写）：悬浮条底部活动指示灯，6 种状态颜色/呼吸策略，`ActivityState` 由 `core/Types.h` 定义
- `ui/MainWindow::computeActivityState()`（v0.1.10）：按优先级汇总线程活动状态；`AR_SCREENSHOT_FAKE_ACTIVITY` 可强制状态截图
- `core/RolloutScanner.*` / `core/ThreadCatalog.*`：v0.1.10 新增 `completedAt`（最后一次正常 task_complete 时间）供“刚完成”判定
- `ui/Theme.*`：v0.1.10 新增 `blue()`（正常结束状态色）

---

## 7. 已知坑 / 注意事项

1. **deploy 前必须退出运行中的实例**，否则 dist\exe 被锁。
2. 程序自己的 app-server 与 Codex 桌面 app 并发会撞 `thread-writer-locks`（桌面版打开着对话就持有写锁，且是会话级而不是 turn 级）；v0.1.9 已自动处理（等待/按设置关闭桌面版），但桌面 app 里已打开的对话仍不一定实时刷新。
11. v0.1.9 的“自动关闭桌面版”默认开启，只在“系统闲置 ≥5 分钟 + 目标对话空闲 ≥90 秒”时触发；如果桌面版同时在其它对话跑任务会被一起关掉（Codex 没有逐对话释放写锁的接口），介意可在设置里关闭。
3. 后端不保证返回重置卡明细（`credits` 可能为 null/截断）→ 颜色退化黄色、无小点，这是预期。
4. 日志里偶发 `QPainter::end: Painter ended with N saved states`（v0.1.0 起既有，可忽略）。
5. 用户很在意视觉细节，会直接截图指出问题；改颜色/动效后最好给截图 + 说明“改的是哪个元素”。
6. 连续失败后需要人工「立即续跑」；持续看守不会自动等下一窗口（刻意设计，红灯提醒）。
7. 程序数据目录 `%APPDATA%\AutoRun_Codex`（settings.json / jobs.json / auto-run-codex.log）。
8. 深色主题下 `theme::yellow()` 是金黄 #F5B942；橙色小点用单独的 `theme::warningDot()`（#F59E0B），不要混用。
9. 账号资料联网：只在本地头像缓存缺失/超过 7 天时发一次 profile GET + 图片 GET；`AR_PROFILE_OFFLINE=1` 可完全关闭。该接口是 ChatGPT 网页端接口（非 app-server 官方方法），只读、不影响额度，封号风险低但非零。
10. 弹卡/主窗口标题的头像与昵称来自 `AccountProfileLoader`；主题切换时字母头像颜色由 `MainWindow::refreshAccountHeader()` 重新渲染。
12. 活动指示灯（v0.1.10）是**推断**：“运行中”来自 rollout 文件活动、“等待回复”依赖 app-server active 状态 + rollout 写入间隔、“刚完成/最近结束”是 5 分钟窗口启发式；rollout 尾部截断/超长工具调用/坏时间戳会有偏差。被 `ignoredThreadIds` 忽略的对话不参与统计；很久以前限额中断未处理的对话会让灯一直红（提示去处理）。