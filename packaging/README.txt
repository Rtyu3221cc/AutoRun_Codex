AutoRun Codex v__VERSION__（Windows x64 便携版）
================================================

是什么
------
Windows 桌面小工具，围绕本机 Codex（ChatGPT 账号）做两件事：
1) 显示 5 小时 / 每周额度百分比、重置时间、重置卡数量与最近到期时间；
2) 选择任意 Codex 对话加入“看守队列”，在额度重置后自动续跑，直到任务完成。

运行要求
--------
- Windows 10 / 11 x64。
- 本机已安装 Codex CLI（codex.exe）。程序启动时会自动探测
  %LOCALAPPDATA%\OpenAI\Codex\bin\*\codex.exe；探测不到可在“设置”里手动指定。
- 若启动时提示缺少 VC++ 运行库（VCRUNTIME140.dll 等），先运行本目录的
  vc_redist.x64.exe 安装后再启动。

如何启动
--------
双击 AutoRun_Codex.exe。程序无安装过程，可放在任意目录（U 盘也可）。
首次启动会在 %APPDATA%\AutoRun_Codex\ 生成：settings.json（设置）、
jobs.json（看守队列）、auto-run-codex.log（日志）。

快速上手
--------
1. 主窗口会显示当前额度；右上角“已连接”表示已连上本地 Codex app-server。
2. 在对话列表里选中一个对话 → “加入看守”（额度重置后自动续跑）
   或“立即续跑”（马上发送续跑 prompt，会真实消耗额度）。
3. 悬浮条可拖到屏幕边缘吸附；点击弹出详情卡片；双击打开主窗口；
   右键菜单可打开主窗口 / 设置 / 退出。
4. 设置里可调：悬浮条样式/缩放/不透明度、显示“已用/剩余”、主题（深/浅）、
   重置后续跑等待秒数、失败重试次数等。

安全与隐私
----------
- 不写入 Codex 的 auth.json / config.toml；续跑通过本机 codex app-server 的
  官方 JSON-RPC 完成，和桌面版共用同一个账号与额度。
- 不会自动消耗“重置卡”（只显示数量与到期时间）。
- 账号头像/昵称：昵称只从本地 auth.json 的 id_token 读取（不联网）；头像仅在
  本地无缓存或缓存超过 7 天时，用本地 token 向 ChatGPT 网页端资料接口发一次
  只读 GET 并缓存 7 天。设置环境变量 AR_PROFILE_OFFLINE=1 可完全禁止联网。
- 自动续跑会真实消耗额度，且 Codex 的 sandbox 配置可能是全权限执行；
  请只对可信对话开启长期看守。

卸载
----
删除程序目录 + %APPDATA%\AutoRun_Codex\ 即可。

版本 / 仓库
-----------
版本：v__VERSION__
源码与更新：https://github.com/Rtyu3221cc/AutoRun_Codex
问题反馈：https://github.com/Rtyu3221cc/AutoRun_Codex/issues
