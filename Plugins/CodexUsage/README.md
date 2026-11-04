# Codex Usage TrafficMonitor Plugin

This plugin ports the Codex portion of Codex Usage Monitor into TrafficMonitor. It draws segmented 5-hour / 7-day remaining-quota rows, reads today's cumulative token totals from local Codex session logs, and shows account/reset-credit details in a scrollable popup.

![Codex Usage taskbar quota bars](images/quota-time-bars.png)

## Build and try in PluginTester

This directory is the sole source for the Codex plugin. The TrafficMonitor host repository loads the DLL and does not contain or build a source copy of this plugin.

For installing both repositories on another Windows PC, follow the host repository's [Codex installation guide](https://github.com/wang-yichun/TrafficMonitor/blob/codex/codex-usage-plugin/INSTALL-CODEX.md). Its `InstallFromRepos.ps1` builds and installs this plugin automatically.

Build `CodexUsage` and `PluginTester` as the same platform and configuration from `TrafficMonitorPlugins.sln`. Both outputs go to `bin/<Platform>/<Configuration>/`, so `PluginTester.exe` discovers `CodexUsage.dll` from its current directory. Start the tester, choose **Codex quota**, then inspect the preview, dark background, two-row layout, tooltip, and click popup.

The tester checks drawing and basic plugin events. Also build the matching TrafficMonitor host from the adjacent `TrafficMonitor` source checkout and copy the DLL into its `plugins` directory for taskbar validation. This plugin uses API 9's `OnShutdown` callback to stop its polling thread; an older host/tester that does not call this callback is not a safe runtime target.

For the adjacent host checkout, install `bin/x64/Release/CodexUsage.dll` to `../TrafficMonitor/Bin/x64/Release/plugins/CodexUsage.dll`. The host's `RestartTrafficMonitor.cmd` also installs this independent build after gracefully closing the running host. Build the plugin again before restarting to apply source changes.

Click the item to open the detail popup, or right-click it for the details and manual-refresh commands. The host tooltip contains a short quota summary; account and token details stay in the plugin popup. This first port covers Codex; Claude Code, Antigravity, and quota notifications are not included.

Quota cells use the same remaining-quota thresholds and light/dark palette as codex-usage-monitor: above 50% is green, above 20% through 50% is amber, and 20% or less is red.

## Data and privacy

The plugin reads the access token and optional account ID from `%CODEX_HOME%\\auth.json`, or `%USERPROFILE%\\.codex\\auth.json` when `CODEX_HOME` is not set. It sends the token only in HTTPS authorization headers to the Codex usage and reset-credit endpoints. It does not write credentials or API responses to disk or logs.

For today's local token totals, it scans active and archived Codex session JSONL files updated since local midnight, including chats created on earlier dates. It counts cumulative token-count increments whose event timestamps fall within today, using the previous day's final event as the baseline. Duplicate active/archive copies are counted once. The summary contains input, cached input, output, reasoning, total, session count, and unreadable-log count.

The total also shows a conversion to units of 100 million tokens (亿 in Chinese), alongside the full token count.

## Configurable workdays and holidays

The time bar uses Beijing civil dates. Five-hour markers for 09:30, 12:00, 13:30 and 18:30 appear only on working days. Weekly markers separate continuous working-day and rest-day periods, including official holidays and make-up workdays.

The time bar keeps its original gray for working periods and uses a muted gray for rest periods, with enough contrast against the host background. By default, all hours of a workday count as working periods on the weekly bar. An independent option can instead color the continuous configured start-to-end span as working time, including lunch; hours before or after work and holidays use the rest color. On the five-hour bar, only the configured morning and afternoon periods count as working periods; lunch, evenings and holidays use the rest color.

The plugin options let you choose the weekly rest pattern: two days off (Saturday and Sunday), one day off (Sunday), or alternating A/B weeks. A means Saturdays are off on odd ISO weeks; B is the opposite phase. Official holidays and make-up workdays still take precedence. The morning and afternoon start/end times are editable in `HH:mm` format (defaults: 09:30, 12:00, 13:30 and 18:30); they control the five-hour bar and, when enabled, the weekly bar's daily work-period coloring. The compact time-bar section is disabled when the time bar itself is hidden.

Edit `calendar/YYYY.txt` beside the installed `CodexUsage.dll` (usually `plugins/calendar/2026.txt`). Each UTF-8 file belongs to the year in its filename. Lines use these formats; range endpoints are inclusive:

```text
# Comments begin with #. A UTF-8 BOM is accepted.
holiday 2026-10-01 2026-10-07
workday 2026-10-10
```

Make-up workdays override holidays, then unlisted dates fall back to Monday-Friday. Missing years also use Monday-Friday; they do not inherit another year's holiday schedule. Invalid lines, dates or ranges spanning different years are ignored. Split a cross-year range across the two annual files.

The bundled 2026 table follows the [State Council's official 2026 notice](https://www.beijing.gov.cn/zhengce/zhengcefagui/202511/t20251104_4258873.html). No predicted 2027 table is bundled. When the official schedule is published, add `2027.txt` in the same format; no recompilation is required. Calendar edits reload with the next data refresh (normally after a 60-second wait); use the plugin's manual refresh command to reload sooner.

Building the plugin copies the source `calendar/*.txt` files alongside its output DLL. Install both the DLL and calendar folder. The host installation/restart scripts add missing annual files and preserve existing calendar files, so edit the installed file or deliberately replace it when updating a year's schedule.

To run the calendar regression checks, open an x64 Visual Studio developer terminal in `Plugins/CodexUsage`, compile `tests/WorkdayCalendarTests.cpp` with `cl /std:c++17 /EHsc /UNDEBUG`, and run the generated `WorkdayCalendarTests.exe` from that same directory. It checks all bundled holiday/make-up dates, workday precedence, malformed input, missing-year fallback and time-bar marker placement.

The plugin currently displays Codex data only. It includes the custom detail panel, but its labels currently support Chinese and English rather than all languages offered by the standalone monitor.

## Display and refresh

In segmented mode, the 5-hour row can use five or ten cells, and the 7-day row keeps its existing 10-, 7-, and 14-cell choices. Each row has its own fill-direction setting: vertical or horizontal (left to right); both default to vertical. A thin gray line below each row shows its remaining time ratio. Reset cards show expiry days, switching to hours or minutes near expiry.

Taskbar text, bars, and reset-card geometry follow the host drawing context's DPI. Reset-card outlines stay one physical pixel wide to remain crisp at high scaling levels; their inset fill uses Qoder's light-theme empty-cell gray, separated from the outline by a one-pixel gap.

Click the item to open or close the grouped detail panel. The panel also closes with its close button, Escape, or when the pointer leaves the popup and its opening position. Account, quota and local token data refresh after a 60-second wait following each refresh; visible countdowns update every second. Manual refresh is available in the plugin commands.

Compact quota text shows the percentage directly, without the Chinese `余` prefix. The popup header includes an **Open Codex App** button (Chinese: `打开 Codex App`). It opens the installed Windows app through `shell:AppsFolder\OpenAI.Codex_2p2nqsd0c76g0!App`; the desktop app must be installed. This launches the desktop app rather than the Codex CLI.

While either the Codex or Qoder detail panel is visible, the owning TrafficMonitor window's native tooltip is hidden and prevented from showing again. Closing or hiding all panels restores normal host tooltips. Shared suppression counts support overlapping plugin panels. The guard also intercepts repeated host show requests, so a stationary pointer cannot bring the host tooltip back over an open detail panel.

The current local runtime is `D:\Projects\TrafficMonitor\TrafficMonitor.exe`, with DLLs in `D:\Projects\TrafficMonitor\plugins`. Release x64 builds, tooltip suppression/restoration checks, loaded-module paths and installed DLL checksums have been verified. Final visual inspection and desktop-app button activation remain manual acceptance checks.

## Plugin options

Open TrafficMonitor's plugin manager, select Codex Usage, and choose plugin options.
The dialog follows the host's Chinese or English language and supports:

- Quota sampling is fixed at one minute; the refresh control shows this fixed interval.
- Follow the host theme, or use light/dark quota text and bar colors.
- Segmented or continuous quota bars; compact or detailed quota text.
- Choose five or ten cells for the 5-hour bar, keep the existing 7-day cell-count choices, and set the fill direction independently for the 5-hour and 7-day bars.
- Show the 5-hour and/or 7-day quota (at least one must remain visible).
- Show reset cards and the time bar independently.
- Choose two-day, one-day, or alternating A/B weekly rest schedules, and set the five-hour bar's four work-period boundaries.

Defaults preserve the existing display. Settings apply on OK and persist in
`CodexUsage.ini` in TrafficMonitor's plugin configuration directory. Cancel leaves
settings unchanged. A save failure keeps the dialog open and reports the error.
Position/background remain host settings. These display switches affect the host
item; the detail popup retains complete account, quota and token information.
Changing settings requests an immediate refresh. Scheduled samples start one minute
apart (network delays can extend this); manual refreshes in the same minute update
the existing sample. Failed requests never create samples from cached values.

## Recent quota consumption

The 5h row shows the last rolling hour of consumption, and the 7d row shows the
last rolling 24 hours. A subtle gray fill extends the remaining-quota fill into
the empty cells, using the selected fill direction; continuous bars use horizontal
fill. Light mode uses a slightly darker gray than empty cells;
dark mode uses a slightly lighter gray for visibility.

Each successful quota response is a meter reading (time, cumulative percentage,
and reset boundary). The difference from the previous reading is spread uniformly
across the intervening minute buckets, including gaps caused by failed requests.
The 5h display sums the last rolling 60 minutes and the 7d display sums the last
rolling 24 hours. Failed requests never fabricate meter readings. Timestamp shifts
alone never reset statistics.

The taskbar quota summary text is drawn over a compact per-minute histogram. Its
width follows the measured summary text width. The 5h row uses one physical pixel
per minute, showing as many recent minutes as fit (up to the current 5h cycle).
The 7d row uses one pixel per 15 minutes, summing consumption within each slice
and showing as many recent slices as fit (up to seven days). Both use the fixed
RGB color (194, 202, 211). The chart is right-aligned: the rightmost column is the
most recent minute or 15-minute slice, so new activity advances the bars to the
left over time. When both rows are visible, their charts share the right edge of
the longer summary label; the labels themselves remain left-aligned.

If an interval crosses the previous expiry, or the used percentage drops by more
than half a percentage point, the window starts a new cycle. The current reading
is allocated only after that cycle's start. The reported reset minus the window
length supplies the start when it lies between readings; otherwise a known expiry
is used, or an early manual reset is estimated at the current reading. Queries
exclude older cycles, while their buckets remain until the independent retention
pass removes records older than seven days. Each window is processed independently.
First installation establishes a baseline without guessing earlier consumption.
The approximation cannot recover consumption in cycles missed entirely while offline.

`CodexUsage.history` beside `CodexUsage.ini` preserves minute consumption, the latest
meter reading and cycle information across restarts. The v2 reader migrates existing
v1 snapshots by interpolating their differences, including repairing the old false
zero baseline. It also stores a hashed account discriminator, never login tokens.
Switching accounts starts separate fresh tracking. Writes replace the file through
a temporary file; if saving is unavailable, in-memory tracking continues.

Statistics regression test: compile `tests/RecentUsageTests.cpp` with
`/std:c++17 /EHsc /I Plugins/CodexUsage` and run the resulting executable.

UI/config regression test: compile `tests/OptionsDialogTests.cpp` in an x64 Visual
Studio developer prompt with `/std:c++17 /EHsc /UNDEBUG` and link `user32.lib gdi32.lib`.
Run with the built `CodexUsage.dll` path. It uses a temporary config directory and
checks cancel, apply, unchanged OK, reload, and single-row continuous drawing.

### Remote data fallback

Failed or incomplete remote refreshes retain the last available quota percentages, original reset timestamps, account fields and reset credits for the current plugin session. Local token totals continue refreshing. A transparent offline icon replaces the 5h/7d labels while information is unavailable; labels return after recovery. The icon uses the original measured label width, ignores transparent margins and keeps its aspect ratio. Tooltip status indicates cached values. Before the first successful request there is no cached quota; cache is held in memory and does not survive restarting the host.
