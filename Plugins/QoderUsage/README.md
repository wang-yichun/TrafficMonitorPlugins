# Qoder Usage Plugin

为 TrafficMonitor 提供的 Qoder 桌面应用额度监控插件。数据模型参考 [qoder-usage-monitor](https://github.com/wang-yichun/qoder-usage-monitor)。

## 功能

- 自动解密 Qoder 桌面应用的 `auth.v1.dat`(DPAPI + AES-256-GCM),无需手动配置 token
- 拉取 `GET {base}/sash/api/v2/me/usage` 接口,展示用户额度 / 附加额度 / 组织额度 / 专属资源包
- 自绘进度条(分段/连续),剩余额度 / 总量 + 到期倒计时
- 周期到期时间与倒计时
- 详情弹窗、左键点击打开
- 选项对话框:刷新频率(1/5/15/60 分)、主题、进度条样式、API 区域、低额度预警阈值、显示项开关

## 安装

将 `QoderUsage.dll`(x64/x86/ARM64) 复制到 TrafficMonitor 安装目录的 `plugins/` 子目录。重启 TrafficMonitor 后在「选项 → 插件管理」中可见。

## 凭据来源

仅支持 Qoder 桌面应用 OAuth 令牌(实测 Personal Access Token 对用量 API 返回 TOKEN_EXPIRE)。插件会按以下顺序自动探测:

1. `%APPDATA%\com.qoder.app.stable\Local State` + `auth.v1.dat`
2. `%APPDATA%\com.qoder.app\Local State` + `auth.v1.dat`

桌面应用启动后会周期性重写 auth.v1.dat,因此令牌过期后只要打开 Qoder 应用刷新,插件下次轮询即可生效。

## API 区域

默认自动探测:首次访问成功后将缓存该区域。也可在选项中固定为 `global`(openapi.qoder.sh) 或 `cn`(openapi.qoder.com.cn)。

## 已知限制

- 仅支持 Windows 10+
- 需要用户已登录 Qoder 桌面应用
- 不打印、不落盘、不上传 token;`ShowNotifyMessage` 仅在低额度阈值满足时显示提示
- 企业账户(displayMode=enterprise)只显示查看链接,不渲染额度条