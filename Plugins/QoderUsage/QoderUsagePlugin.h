#pragma once

#include "PluginInterface.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace QoderDesktopAuth { struct Credential; }

struct QoderQuota
{
    double total{};
    double used{};
    double remaining{};
    double used_percentage{};   // 0..100
    std::wstring unit{ L"credits" };
    bool available{ true };
    long long expires_at{};     // unix epoch seconds, 0 = unknown
};

enum class QoderQuotaKind { User, AddOn, Org, Package };

struct QoderQuotaRow
{
    QoderQuotaKind kind{};
    std::wstring key;          // stable identity for visibility settings
    std::wstring label;        // human label (for dedicated packages)
    QoderQuota quota;
};

struct QoderUsage
{
    bool enterprise{};
    std::wstring detail_url;
    std::wstring user_type;
    bool quota_exceeded{};
    double total_usage_percentage{ -1.0 };   // 0..100, -1 = unknown
    long long cycle_expires_at{};
    std::vector<QoderQuotaRow> rows;
};

struct QoderSnapshot
{
    QoderUsage usage;
    long long fetched_at{};
    std::wstring credential_label;   // human label only (no token)
    std::wstring region;              // "global", "cn", "demo"
    std::wstring status;              // human-readable status line
    bool has_data{};
};

struct QoderOptions
{
    int poll_seconds{ 300 };          // 60 / 300 / 900 / 3600
    int theme{};                      // 0 host, 1 light, 2 dark
    int bar_style{};                  // 0 segmented, 1 continuous
    int region{};                     // 0 auto, 1 global, 2 cn
    int alert_threshold{};            // 0 off, 1 50, 2 25, 3 10
    bool show_total{ true };
    bool show_cycle{ true };
    bool language_cn{};              // false = follow host, true = force chinese
};

class CQoderUsageItem : public IPluginItem
{
public:
    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
    bool IsCustomDraw() const override { return true; }
    int GetItemWidth() const override { return 320; }
    int GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;
    int IsDoubleLineExclusive() const override { return 1; }
    int IsDrawResourceUsageGraph() const override { return 0; }
    float GetResourceUsageGraphValue() const override;
};

class CQoderUsagePlugin : public ITMPlugin
{
public:
    static CQoderUsagePlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    void OnInitialize(ITrafficMonitor* app) override;
    void OnShutdown() override;
    const wchar_t* GetTooltipInfo() override;
    int GetCommandCount() override { return 3; }
    const wchar_t* GetCommandName(int command_index) override;
    void* GetCommandIcon(int /*command_index*/) override { return nullptr; }
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    OptionReturn ShowOptionsDialog(void* hParent) override;

    QoderOptions Options() const;
    bool SaveOptions(const QoderOptions& options);
    QoderSnapshot Snapshot() const;
    bool IsChinese() const { return m_is_chinese; }
    void RequestRefresh();
    void PublishSnapshot(QoderSnapshot snapshot);
    ITrafficMonitor* App() const { return m_app; }

private:
    CQoderUsagePlugin() = default;
    void PollLoop();
    void LoadConfig();

    mutable std::mutex m_options_mutex;
    QoderOptions m_options;
    std::wstring m_config_path;
    CQoderUsageItem m_item;
    ITrafficMonitor* m_app{};
    bool m_is_chinese{ true };

    mutable std::mutex m_snapshot_mutex;
    QoderSnapshot m_snapshot;

    std::mutex m_worker_mutex;
    std::condition_variable m_worker_cv;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_refresh_requested{ false };
    std::thread m_worker;
};

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance();