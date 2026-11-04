# Qoder Usage Plugin

TrafficMonitor 的 Qoder / WorkBuddy 额度监控插件。自动读取当前 Windows 用户已登录的桌面应用凭据，无需配置 token。

## 显示项目与命名（0.8.0）

显示设置、任务栏文字、详情标题、应用按钮及插件命令统一使用以下短名称；无 `CN` 后缀为国际版。

| 名称 | 版本 | 显示项 ID | 独立历史文件 |
| --- | --- | --- | --- |
| Qoder | 国际版 | `QoderUsageQuota9` | `QoderUsage.history` |
| QoderCN | 中国版 | `QoderCNUsageQuota` | `QoderCNUsage.history` |
| WorkBuddy | 国际版 | `WorkBuddyInternationalCredits` | `WorkBuddyInternationalUsage.history` |
| WorkBuddyCN | 中国版 | `WorkBuddyCredits` | `WorkBuddyUsage.history` |

原有三个 ID 和历史文件名保持兼容；新增 QoderCN 不会改变已有显示勾选。右键 TrafficMonitor 任务栏 → **显示设置** → 勾选需要显示的项目。四项分别查询、分别发布状态，失败不会覆盖其他项；旧 `Region` 选项保留配置兼容，界面显示为按项目固定区域，不能再把国际版项目切换到中国版。


## WorkBuddy / WorkBuddyCN 积分

`WorkBuddy` 读取国际版 `WorkBuddyAI.exe` 的登录状态，`WorkBuddyCN` 读取中国版 `WorkBuddy.exe` 的登录状态，原中国版显示项仅更名，ID 不变。插件命令分别提供两项详情。

- 每个应用占一行，连续进度条内显示明确版本和剩余/合计，例如 `WorkBuddyCN 2200/2200`、`WorkBuddy 350/350`。与 Qoder / QoderCN 采用相同布局和配色，刷新频率共用。
- 左键打开对应版本详情，标题、区域（中国 cn / 国际 global）、登录来源均区分版本；“打开 WorkBuddyCN”或“打开 WorkBuddy”按钮启动对应客户端。
- 中国版使用 `WorkBuddyUsage.history`，国际版使用 `WorkBuddyInternationalUsage.history`，与 `QoderUsage.history` 分开；版本、账号或总量变化时重建相应基线。一版失败不会覆盖另一版余额。请求失败时显示占位符及错误状态，不把旧值冒充为最新余额。

### 已核实的接口与开发要点

2026-11-02 对照本机客户端及开源实现，验证两版均使用以下只读链路（中国版示例，国际版域名为 `www.workbuddy.ai`）：

```text
POST https://www.workbuddy.cn/billing/meter/get-user-resource-summary
Authorization: Bearer <桌面 accessToken>
X-User-Id: <当前账号 uid>
Content-Type: application/json

{}
```

成功要求 HTTP 2xx 且 JSON `code: 0`。从 `data.Packages[]` 汇总 `CapacityUnit` 为 `"credits"` 或 `"credit"` 的 `CycleTotalCapacity` 和 `CycleRemainCapacity`；数字和十进制字符串都支持。国际版新用户赠送包实测使用单数 `credit`，不能只识别复数，否则会漏算 250 积分。`CreditGroups` 是同一余额的另一种分组，不能再次累计。`CycleFrozenCapacity` 单独展示，不重复扣减剩余量。余额超过当期发放量时保留实际余额，进度条最多填满；空包列表显示已确认的 `0/0`，结构异常不会转换为零余额。

登录文件均位于 `%LOCALAPPDATA%\CodeBuddyExtension\Data\Public\auth`，按版本严格匹配，不跨版回退：

| 版本 | 登录文件 | 默认安装目录下的程序 | 允许的请求域名 |
| --- | --- | --- | --- |
| 中国版 | `workbuddy-desktop.info` | `WorkBuddy\WorkBuddy.exe` | `www.workbuddy.cn` |
| 国际版 | `workbuddy-desktop-ai.info` | `WorkBuddyAI\WorkBuddyAI.exe` | `www.workbuddy.ai` |

尊重各自的 `.logged-out` 标记。新版 `$wbEncrypted` 字段使用 AES-256-GCM。插件以隐藏的 Node 模式短暂启动对应版本的客户端，通过它自身的 `electron_browser_workbuddy_storage` 读取构建密钥并在该子进程完成解密及查询。向宿主回传的仅为选定积分字段和不可逆账号摘要；令牌、密钥不写盘、不记日志、不进入命令行，也不刷新或修改登录态。无额外 Node/Python 运行依赖；单次请求限时 15 秒，子进程总限时 25 秒，拒绝重定向和与当前版本不匹配的登录域名。

参考资料：

- [WorkBuddy 官方积分说明](https://www.codebuddy.cn/docs/workbuddy/Credits)：积分扣减与有效期规则。
- [WorkBuddy Open API](https://open.workbuddy.cn/docs/openapi)：公开的积分能力是兑换；本插件采用客户端 billing 接口。
- [CodexBar WorkBuddy 接入说明](https://github.com/steipete/CodexBar/blob/main/docs/workbuddy.md)：summary 字段、分组重复计数、冻结量、Cookie 与 User-Agent 绑定、可选套餐接口。
- [zcode-workbuddy-connect](https://github.com/hawchou1995/zcode-workbuddy-connect#workbuddy-56-encrypted-credentials)：利用已安装应用运行时读取加密凭据，密钥仅留内存。

当前支持默认安装位置的 **中国版和国际版个人账号**。在插件选项中，Qoder、Qoder CN、WorkBuddy 和 WorkBuddy CN 的程序路径可以分别留空使用默认位置，也可以浏览选择自定义安装位置的 `.exe` 文件。企业配额尚未接入。summary 不提供积分包到期/重置时间，插件不推测。billing 和桌面加密方式属于客户端内部接口，升级后若变化需适配。

### 验证记录

0.8.0 文字下移（2026-11-03）：文字在原字形居中位置上下移 1 个物理像素，靠近下沿积分条；行高、字号、列宽及方块尺寸/间距不变。仅构建插件，Release x64 与四档模拟 DPI 的偏移后字形适配、方块和 GDI 回归通过，已检查 144 DPI 预览。已备份部署并正常重启，DLL 构建/安装 SHA256 一致、单进程、加载路径及显示选择保留确认。

0.8.0（2026-11-03）：撤销最小行高扩展，宿主恢复总高 32 DIP / 每行 16 DIP。Qoder 下沿改为 2 DIP 细条、文字/细条之间留 1 物理像素；字体按实际可见字形拟合和居中，保证上下行字号一致。宿主和插件 Release x64 构建通过；宿主布局函数剔除 Qoder 共边块后与扩高前版本逐字一致，四档模拟 DPI 的原 16 DIP 行高、文字可见墨迹居中/裁切、严格等宽方块/1px 间距、消耗方向及 GDI 状态检查通过，已检查 144 DPI 预览。已正常退出、备份、部署并重启，EXE/DLL 哈希一致、单进程、加载路径和显示选择确认。读取当前 144 DPI 运行任务栏子窗口，实际客户区为 791×48 物理像素，高度精确恢复为 32 DIP；此为尺寸核验，真实任务栏视觉与跨屏行为仍待人工确认。

0.7.0 灰底补齐（2026-11-03）：下沿灰色底轨铺满到最右侧内部边界，格宽/间距和末端余量的几何位置不变，余量仅改为灰底。插件 Release x64 与四档模拟 DPI 回归通过，已检查 144 DPI 预览；已备份部署并重启，单进程、加载路径、构建/安装 SHA256 一致及显示选择保留均确认。

0.7.0（2026-11-03）：实现已选方案 A，统一浅底文字区与下沿 4 DIP 分格条分离，文字在上方区域垂直居中。新增可选最小行高查询，宿主在任务栏可用高度内为每行预留 22 DIP。每行格宽严格一致，间隔 1 个物理像素，整数除法余量保留末端留白。宿主及插件 Release x64 构建通过；96/120/144/192 模拟 DPI 的文字区无彩色/灰色条纹、文字适配、27/22 格计数、逐格等宽/1px 间距/末端留白、消耗方向、共享边框和 GDI 状态/对象检查通过；已检查 96/144 DPI 的 GDI 双行预览。已备份并部署 EXE 与 DLL，确认两项构建/安装 SHA256 一致、单进程、加载 DLL 路径及原显示选择保留。真实任务栏观感与跨屏 DPI 切换待人工确认。

0.6.1（2026-11-03）：三档颜色进一步加深，部分格修正为从上向下消失（剩余颜色贴底）。仅修改并构建 Qoder 插件；Release x64、四档模拟 DPI 的新配色与灰/彩交界方向检查、27/22 格、中线及 1px 间隙回归均通过，已检查 144 DPI 双行预览。已备份并部署，确认单个运行宿主、加载 DLL 路径、构建/安装 SHA256 一致及显示选择保留；真实任务栏观感与跨屏切换仍待人工确认。

0.6.0（2026-11-03）：改为黑色 1px 外框及每 100 积分分格，灰底/外框之间保留 1px 宿主背景，三档填色略微加深。配合宿主的可选共用边框查询，两行共享一条 1px 中线。宿主及插件 Release x64 构建通过；四档模拟 DPI 的 27/22 格计数、1px 灰色格间隔、部分格从上向下填充、共享中线及两侧背景留白、字体适配、零/未知/超额余额与 GDI 状态/对象检查均通过。已目视检查 144 DPI 的原生 GDI 双行预览。已备份并部署宿主 EXE 与插件 DLL，确认单个运行进程、正确加载路径、两项构建/安装 SHA256 一致、原有显示勾选和排列设置保留。真实任务栏视觉及跨显示器 DPI 切换仍待人工验收。

0.5.0 宽度调整（2026-11-03）：取消 `88888.8/88888.8` 样例的最小宽度，按当前文字和必要边距测量，同列由宿主取较长行宽度。Release x64 与四档 DPI 绘制回归通过，已检查收窄后的 GDI 预览；已备份并部署，单个宿主进程、加载路径、构建/安装 SHA256 一致及显示选择保留均确认。

0.5.0（2026-11-03）：Release x64 构建通过。`QoderQuotaDrawingTests.cpp` 的 96/120/144/192 DPI 绘制检查全部通过，包含连续填充比例、20%/50% 边界、四项原 ID 与单行标志、零/无数据/超额余额、文字适配、1px 边框和背景间隙、绘图状态与 GDI 对象检查；已目视检查生成的 144 DPI 原生 GDI 预览。已备份旧 DLL 并部署到本机，正常重启后确认单个宿主进程、实际加载插件路径、构建/安装 SHA256 一致，宿主显示勾选及排列设置保留，TrafficMonitor 保持运行。真实任务栏外观及跨显示器 DPI 切换尚待人工验收。

0.4.0（2026-11-02）：Release x64 构建通过。新增 `QoderDesktopAuthTests.cpp`，使用合成 DPAPI/AES-GCM 登录文件验证两版分别读取、QoderCN 缺失时不得回退国际版，以及真实本机两版凭据读取；原 WorkBuddy 解析测试继续通过。动态加载构建 DLL 验证四个准确短名称、原 ID 保留、真实轮询和浅/深色 16/24 像素字号绘制，实测 `Qoder 2180/2700`、`QoderCN 100/100`、`WorkBuddy 350/350`、`WorkBuddyCN 2200/2200`，关闭正常。已部署到本机并正常重启，单个宿主进程和插件加载路径确认，构建/安装 SHA256 一致，保留已有配置与显示选择；宿主已生成独立 `QoderCNUsage.history`。

0.3.0（2026-11-02）：Release x64 构建通过，真实接口确认中国版 `2200/2200`、国际版 `350/350`。新增国际版单数 `credit` 赠送包、两版同 UID 的历史隔离及官网地址隔离回归检查。动态加载 DLL，验证三个显示项、原中国版 ID 保持不变、两版中文名称、后台分别查询、浅/深色及 16/24 像素字号绘制和正常关闭。已部署并正常重启本机 TrafficMonitor，确认单个宿主进程、加载插件路径、构建/安装 SHA256 一致，两个版本均生成各自历史文件；安装后 DLL 的真实查询和绘制再次通过。保留宿主退出时保存的已有显示选择。应用按钮实际点击与长期运行仍待人工验收。

0.2.0（2026-11-02）：Release x64 构建通过；`WorkBuddyUsageTests.cpp` 覆盖数字字符串、冻结量、重复分组、单位过滤、异常结构、业务失败、零余额及超额余额，真实 API 查询返回 `2200/2200`。动态加载构建 DLL，确认当时两个显示项、原 Qoder ID、后台真实轮询、浅/深色与 16/24 像素字号绘制及正常关闭。已部署到本机 `D:\Projects\TrafficMonitor\plugins\QoderUsage.dll`，正常重启后确认单个宿主进程、加载路径和构建/部署 SHA256 一致，插件启用与显示选择保持原状，实际宿主生成了独立 WorkBuddy 历史文件；安装后的 DLL 再次真实查询与绘制检查通过。应用按钮点击与长期运行仍待人工验收。

在 VS x64 开发者命令行中，从仓库根目录可单独运行解析和真实接口检查：

```bat
cl /nologo /EHsc /std:c++17 /utf-8 /wd4100 /I include Plugins\QoderUsage\WorkBuddyUsageTests.cpp Plugins\QoderUsage\WorkBuddyUsage.cpp /Fe:WorkBuddyUsageTests.exe /link user32.lib
WorkBuddyUsageTests.exe
WorkBuddyUsageTests.exe --live
cl /nologo /EHsc /std:c++17 /utf-8 /wd4100 Plugins\QoderUsage\QoderDesktopAuthTests.cpp Plugins\QoderUsage\QoderDesktopAuth.cpp /Fe:QoderDesktopAuthTests.exe /link crypt32.lib bcrypt.lib
QoderDesktopAuthTests.exe
QoderDesktopAuthTests.exe --live
```

## 两行任务栏进度条（0.8.0，方案 A）

每个应用改为独立的单行项目。在任务栏右键 → **显示设置** 中，从 `Qoder`、`QoderCN`、`WorkBuddy`、`WorkBuddyCN` 勾选需要的两个应用；关闭宿主的“水平排列”后，两个项目共用一列，上下两行等宽显示。匹配的新宿主会将两个已声明共用边框的同插件项目重叠 1 个物理像素，中间只保留一条黑线；该列内独立的垂直间距由共用边框替代，其他列仍按原宿主设置布局。沿用四个原 ID，升级不改插件启用状态、显示勾选或其他宿主偏好。宿主仍允许选择更多项目；按原有顺序每两项成列，其他单行监控项也参与宿主配对。

- 采用已确认的方案 A：上方文字区域使用统一浅底 `RGB(236,240,242)`，黑字按可见字形居中后下移 1 个物理像素，更靠近进度条；下沿保留 2 DIP 高的分格积分条，文字区域相对原居中位置下移 1 个物理像素，裁切止于进度条上缘，字号、列宽与方块位置不变。文字按实际可见字形拟合，忽略字体上下未着墨的空白；空间不足时缩小字形，避免文字盖到格子上。
- 黑色外框和共用中线各为 1 个物理像素；外框内侧保留 1 个物理像素宿主原背景。下沿轨道使用原浅灰 `RGB(194,202,211)`，每 100 积分一格（末格可不足 100）。2700 分 27 格、2200 分 22 格；每行格宽严格相同，行与行可使用不同格宽，间距严格为 1 个物理像素。格宽取整数，灰色底轨一直铺到最右侧内部边界；除不尽的末端余量显示灰底，不摊入方块。额度消耗时部分格从上向下露出灰底，剩余颜色贴底。极大额度在像素宽度不足时合并格子。
- 下沿状态色按方案 A 使用较沉稳的绿 `RGB(116,170,132)`（剩余 >50%）、黄 `RGB(213,174,75)`（>20% 且 ≤50%）、红 `RGB(207,128,123)`（≤20%）。文字区域颜色固定，不随额度阶段改变。低分辨率下有余额的部分格至少保留 1 个彩色像素，细条比例按像素取整。
- 字体保留宿主字体并缩小至约 85%，测量和绘制共用字体逻辑；列宽仍按当前较长文字加必要边距计算。已移除 0.7.0 的最小行高请求：宿主恢复原总高 32 DIP、每行 16 DIP，Qoder 在原区域内适配，不再扩大其他显示项。上下行共边带来的 1 像素高度差不改变两行字号。DPI 缩放字体、区域高度和内边距，边框、透明留白与方块间距始终保持 1 个物理像素。
- 无数据时显示 `--/--` 和灰条，不显示旧余额；零余额显示 `0/总额`。WorkBuddy 超额余额保留实际文字，条宽最多填满；已确认 `0/0` 与无数据区分。

Qoder 合计包含接口返回且可用的用户额度、附加额度、组织资源包和专属资源包，以合计余额计算比例，不使用接口可能有舍入差异的百分比。各应用的本地历史文件仍保留记录，账号、区域或总量变化时重建基线。

### 绘制回归检查

从 VS x64 开发者命令行运行（不要定义 `NDEBUG`）：

```bat
cl /nologo /EHsc /std:c++17 /utf-8 /wd4100 /I include Plugins\QoderUsage\QoderQuotaDrawingTests.cpp Plugins\QoderUsage\QoderDesktopAuth.cpp Plugins\QoderUsage\WorkBuddyUsage.cpp /Fe:QoderQuotaDrawingTests.exe /link user32.lib gdi32.lib crypt32.lib bcrypt.lib advapi32.lib
QoderQuotaDrawingTests.exe <预览输出目录>
```

测试通过替换 GDI 报告的 DPI，在不更改 Windows 设置的情况下执行真实绘制代码，覆盖 96/120/144/192 DPI、四项 ID/单行标志、文字/底部细条分区、文字适配、阶段边界、100 积分分格、每行严格等宽/1px 间距及末端灰底铺满、从上向下消耗并保留底部颜色、27/22 格计数、两行共用 1px 黑边框、零/未知/超额余额、文字适配、1px 边框与留白、绘图状态恢复和 1000 次绘制的 GDI 对象数量。生成各 DPI 的 BMP 供目视检查；此检查不等同于在所有真实显示器缩放设置下验收宿主。

## 详情气泡与应用按钮

左键点击打开详情，再次点击、Escape、关闭按钮或鼠标离开气泡及原点击位置后隐藏。打开后的 600ms 提供移入宽限期，鼠标停留在原点击位置时保持显示。

详情包含连接状态、区域、凭据来源、刷新时间、套餐、接口百分比、额度周期和各类额度。右上角应用按钮使用插件选项里对应版本的程序路径；路径留空时自动查找各客户端的默认安装位置。WorkBuddy 的自定义路径同时用于执行额度查询程序。

Qoder 或 Codex 详情可见时，对应宿主窗口的原生提示气泡暂停显示，包括宿主再次请求显示提示的情况；详情关闭后恢复。多个插件同时打开详情时，全部关闭才恢复共享的宿主提示。

## 构建与安装

使用 Visual Studio 2022 的 v143 C++ 工具链和 Windows SDK。项目支持 Win32/x64，Release 和 Release (lite)；当前验证版本为 Release x64。

```powershell
# 在 Visual Studio 开发者 PowerShell 中运行
MSBuild.exe Plugins\QoderUsage\QoderUsage.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$PWD\" /m
```

产物为 `bin/x64/Release/QoderUsage.dll`。退出 TrafficMonitor 后复制到实际运行目录的 `plugins/`，与 Codex 共用 `plugins/calendar/`。当前本机部署位置是 `D:\Projects\TrafficMonitor\plugins`。重启后在任务栏显示设置中选择所需短名称项目。升级保留已有日历和配置。

## 配置与凭据

选项提供刷新频率（1/5/15/60 分）、原低额度预警阈值和显示选择说明，保存在宿主插件配置目录的 `QoderUsage.ini`。旧主题、进度条样式、合计/周期开关与工作时间设置保留配置兼容，已从选项界面移除，不改变新的单行连续条布局。

仅支持桌面应用 OAuth token；Personal Access Token 不适用于此用量接口。凭据分别探测当前用户 `%APPDATA%` 下对应区域的目录，每个目录都需要 `Local State` 和 `auth.v1.dat`：

| 名称 | 首选目录 | 兼容目录 | 唯一 API 域名 |
| --- | --- | --- | --- |
| Qoder | `com.qoder.app.stable` | `com.qoder.app` | `openapi.qoder.sh` |
| QoderCN | `com.qodercn.app.stable` | `com.qodercn.app` | `openapi.qoder.com.cn` |

使用当前用户 DPAPI + AES-256-GCM 解密。只读取凭据，不替桌面应用刷新 token。过期时打开对应客户端登录，后续轮询重新读取凭据。QoderCN 缺少凭据时明确报错，不回退到 Qoder；国际版同样不读取中国版凭据。中国区官方 OpenAPI 域名可参考 [Qoder CN 文档](https://docs.qoder.cn/en/cloud-agents/quickstart)。

两项使用相同用量路径 `GET /sash/api/v2/me/usage`，各自固定上述区域，不再按全局 Region 设置跨区重试。WinHTTP 连接前解析 HTTPS URL，只向连接函数传入主机名及端口。

## 限制与验证

- Windows 10+，需要支持 API 9 关闭回调的 TrafficMonitor 宿主。
- 企业账户无额度明细时不能绘制合计额度。
- 凭据仅用于 HTTPS Authorization，不写入日志或配置。
- 活动额度与试用额度各有到期规则；10:00 时间条不是自动补发套餐额度的承诺。
- 已通过 Release x64 构建、真实 API 读取、额度比例与时间边界检查、提示窗口再次显示拦截/恢复检查；本机已确认进程路径、加载 DLL 和 SHA256。应用按钮点击及最终 UI 外观仍需人工验收。
