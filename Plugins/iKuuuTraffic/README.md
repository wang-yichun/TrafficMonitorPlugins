# iKuuu Traffic plugin

The plugin reads the text currently exposed by the `iKuuuVPN.exe` window through Windows UI Automation every 15 seconds. It displays total quota and today's usage as two independently selectable TrafficMonitor items. The quota item shows used/total GB followed by a horizontal remaining-quota bar; the today item shows `iKuuu 今日` followed closely by today's used GB. Both items use compact, left-aligned custom drawing, with no separate `iKuuu` label line. The numbers reflect the values currently displayed by iKuuuVPN; their freshness depends on iKuuuVPN's own update behavior.

When the host's `show_status_bar` option is enabled, the quota item shows a horizontal bar behind its text. The bar fills from left to right to show the remaining quota. This requires the host's updated mode-2 graph support; for quota, the host ignores the history graph setting. Its color uses TrafficMonitor's native status-bar settings: `graph_color_following_system` follows the Windows theme, while `status_bar_color` selects a manual color. The plugin supplies the remaining-quota ratio to the host renderer and does not draw a second bar. The tooltip also reports remaining GB and percentage.

The host draws a full-width gray track behind the remaining-quota fill, using the same dark and light empty-cell colors as Codex Usage. When that quota bar is active, the plugin overlays today's consumed share immediately to the right of the host's remaining-quota fill. The overlay uses a darker gray in dark mode and a lighter gray in light mode, and is hidden when the host status bar is disabled.

The plugin only reads UI Automation names from the app window. It does not navigate pages, click controls, send keystrokes, access credentials, or make network requests. Values update when the current UI Automation tree exposes the traffic statistics. If the app is closed, minimized with no exposed text, or showing another page, the last successful values remain visible with an `(旧)` marker, and the tooltip reports the reason and last read date/time. Before a successful read, the item shows `不可用`.

UI Automation can only read text the app exposes through its accessibility tree. A future iKuuu app update that changes those accessible labels may require a parser adjustment.

## Build

Build `iKuuuTraffic` for the same architecture as TrafficMonitor, for example `Release|x64`. The resulting DLL is `bin\x64\Release\iKuuuTraffic.dll`.

After reviewing the target host directory and settings, install with:

```powershell
powershell -ExecutionPolicy Bypass -File .\Plugins\iKuuuTraffic\Install-iKuuuTraffic.ps1
```

The installer backs up `config.ini` and any existing plugin DLL, enables the two display item IDs, copies the DLL, and restarts TrafficMonitor. Preview its resolved paths and changes first with `-ValidateOnly`.

## Read-only verification

Build and run `iKuuuTrafficProbe` with the DLL path as its first argument and an optional watch duration in seconds. The default run lasts five seconds. It loads the plugin, starts its polling worker, prints both item values once per second, then calls `OnShutdown` and unloads the DLL. It does not interact with the iKuuu window.
