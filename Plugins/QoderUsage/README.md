# Qoder Usage Plugin

TrafficMonitor 的 Qoder 额度监控插件。自动读取当前 Windows 用户已登录的 Qoder 桌面应用凭据，无需配置 token。

## 任务栏显示

固定两行，字体与尺寸跟随 TrafficMonitor：

- 第一行：十段额度方块 + 剩余百分比，例如 `32%`。每段代表 10%，不足一段时从底部填充。剩余 >50% 为绿色，>20% 且 ≤50% 为橙色，≤20% 为红色；颜色、间距和方块下方时间条的布局与 Codex 插件一致。
- 第二行：`剩余额度 / 合计额度 · 到期倒计时`，例如 `288 / 900 · 4d 20h`。不显示 credits 单位。不足一天显示小时分钟，不足一小时显示分钟秒。

合计计算包含接口返回且可用的用户额度、附加额度、组织资源包和专属资源包，以 `合计 remaining / 合计 total` 计算百分比，不使用接口可能存在舍入差异的百分比。无数据时显示占位符。

方块下方的细时间条表示**北京时间上一个 10:00 到下一个 10:00 的每日领取窗口**，每天 10:00 恢复满格，随后从右侧缩短。它不表示套餐到期或已领取状态，也不会自动领取活动奖励。第二行倒计时仍表示接口返回的额度周期到期时间。

时间条复用 Codex 5h 条的工作/休息配色和 1 像素断点：工作日的 09:30、12:00、13:30、18:30。读取 DLL 旁的 `calendar/YYYY.txt`，支持节假日和调休；文件格式见 [Codex 日历说明](../CodexUsage/README.md#configurable-workdays-and-holidays)。日历随用量刷新重新加载，无文件时按周一至周五判断。

## 详情气泡与应用按钮

左键点击打开详情，再次点击、Escape、关闭按钮或鼠标离开气泡及原点击位置后隐藏。打开后的 600ms 提供移入宽限期，鼠标停留在原点击位置时保持显示。

详情包含连接状态、区域、凭据来源、刷新时间、套餐、接口百分比、额度周期和各类额度。右上角的“打开 Qoder App”启动 `%LOCALAPPDATA%\Programs\Qoder\Qoder.exe`。应用需安装在此用户安装位置。

Qoder 或 Codex 详情可见时，对应宿主窗口的原生提示气泡暂停显示，包括宿主再次请求显示提示的情况；详情关闭后恢复。多个插件同时打开详情时，全部关闭才恢复共享的宿主提示。

## 构建与安装

使用 Visual Studio 2022 的 v143 C++ 工具链和 Windows SDK。项目支持 Win32/x64，Release 和 Release (lite)；当前验证版本为 Release x64。

```powershell
# 在 Visual Studio 开发者 PowerShell 中运行
MSBuild.exe Plugins\QoderUsage\QoderUsage.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$PWD\" /m
```

产物为 `bin/x64/Release/QoderUsage.dll`。退出 TrafficMonitor 后复制到实际运行目录的 `plugins/`，与 Codex 共用 `plugins/calendar/`。当前本机部署位置是 `D:\Projects\TrafficMonitor\plugins`。重启后在插件管理中选择 Qoder 额度。升级保留已有日历和配置。

## 配置与凭据

选项提供刷新频率（1/5/15/60 分）、主题、API 区域、低额度预警阈值，保存在宿主插件配置目录的 `QoderUsage.ini`。旧选项中的进度条样式、合计/周期开关仍保留配置兼容，但不改变当前固定十段、两行任务栏布局。

仅支持桌面应用 OAuth token；Personal Access Token 不适用于此用量接口。凭据探测顺序：

1. `%APPDATA%\com.qoder.app.stable\Local State` + `auth.v1.dat`
2. `%APPDATA%\com.qoder.app\Local State` + `auth.v1.dat`

使用当前用户 DPAPI + AES-256-GCM 解密。只读取凭据，不替桌面应用刷新 token。过期时打开 Qoder 并登录，后续轮询重新读取凭据。

用量接口为 `GET /sash/api/v2/me/usage`，自动区域按 global (`openapi.qoder.sh`)、cn (`openapi.qoder.com.cn`) 顺序尝试，也可在选项固定区域。WinHTTP 连接前解析 HTTPS URL，只向连接函数传入主机名及端口。

## 限制与验证

- Windows 10+，需要支持 API 9 关闭回调的 TrafficMonitor 宿主。
- 企业账户无额度明细时不能绘制合计额度。
- 凭据仅用于 HTTPS Authorization，不写入日志或配置。
- 活动额度与试用额度各有到期规则；10:00 时间条不是自动补发套餐额度的承诺。
- 已通过 Release x64 构建、真实 API 读取、额度比例与时间边界检查、提示窗口再次显示拦截/恢复检查；本机已确认进程路径、加载 DLL 和 SHA256。应用按钮点击及最终 UI 外观仍需人工验收。
