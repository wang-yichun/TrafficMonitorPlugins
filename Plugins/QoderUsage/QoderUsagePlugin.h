#pragma once

#include "PluginInterface.h"
#include "../CodexUsage/WorkdayCalendar.h"
#include "../CodexUsage/RecentUsage.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace QoderDesktopAuth { struct Credential; }

enum class QuotaProvider { Qoder, QoderChina, WorkBuddyInternational, WorkBuddyChina, Count };

struct QoderQuota
{
    double total{};
    double used{};
    double remaining{};
    double reserved{};         // reported separately by WorkBuddy; already outside spendable balance
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
    CodexCalendar::Calendar calendar;
    QoderUsage usage;
    long long fetched_at{};
    std::wstring credential_label;   // human label only (no token)
    std::wstring region;              // "global", "cn", "demo"
    std::wstring history_identity;    // provider/account boundary for local usage history
    std::wstring status;              // human-readable status line
    bool has_data{};
    bool checkin_known{}, checkin_active{}, checkin_done{};
    double checkin_daily{}, checkin_today{}, checkin_streak{};
    std::wstring checkin_activity, checkin_start, checkin_end;
    double recent_percent{};
    std::vector<double> recent_buckets;
};

struct QoderOptions
{
    int poll_seconds{ 300 };          // 60 / 300 / 900 / 3600
    int theme{};                      // 0 host, 1 light, 2 dark
    int bar_style{};                  // 0 segmented, 1 continuous
    int region{};                     // legacy setting retained; each item now has a fixed region
    int alert_threshold{};            // 0 off, 1 50, 2 25, 3 10
    bool show_total{ true };
    bool show_cycle{ true };
    bool language_cn{};              // false = follow host, true = force chinese
    int rest_schedule{};              // 0: Sat/Sun off, 1: Sun off, 2/3: alternating ISO weeks
    int morning_start{ 9 * 60 + 30 };
    int morning_end{ 12 * 60 };
    int afternoon_start{ 13 * 60 + 30 };
    int afternoon_end{ 18 * 60 + 30 };
    std::wstring qoder_executable_path;       // empty = use the default Qoder install path
    std::wstring qoder_cn_executable_path;    // empty = use the default Qoder CN install path
    std::wstring workbuddy_executable_path;   // empty = use the default WorkBuddy install path
    std::wstring workbuddy_cn_executable_path;// empty = use the default WorkBuddy CN install path
};

class CQoderUsageItem : public IPluginItem
{
public:
    explicit CQoderUsageItem(QuotaProvider provider = QuotaProvider::Qoder) : m_provider(provider) {}
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
    void* OnItemInfo(ItemInfoType type, void*, void*) override
    {
        return type == IIT_TASKBAR_SHARED_BORDER ? this : nullptr;
    }
    int IsDoubleLineExclusive() const override { return 0; }
    int IsDrawResourceUsageGraph() const override { return 0; }
    float GetResourceUsageGraphValue() const override;
private:
    QuotaProvider m_provider{};
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
    int GetCommandCount() override { return 6; }
    const wchar_t* GetCommandName(int command_index) override;
    void* GetCommandIcon(int /*command_index*/) override { return nullptr; }
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    OptionReturn ShowOptionsDialog(void* hParent) override;

    QoderOptions Options() const;
    bool SaveOptions(const QoderOptions& options);
    QoderSnapshot Snapshot(QuotaProvider provider = QuotaProvider::Qoder) const;
    bool IsChinese() const { return m_is_chinese; }
    void RequestRefresh();
    void PublishSnapshot(QoderSnapshot snapshot, QuotaProvider provider = QuotaProvider::Qoder);
    ITrafficMonitor* App() const { return m_app; }

private:
    CQoderUsagePlugin() = default;
    void PollLoop();
    void LoadConfig();

    mutable std::mutex m_options_mutex;
    QoderOptions m_options;
    std::wstring m_config_path;
    CQoderUsageItem m_item;
    CQoderUsageItem m_qoder_cn_item{ QuotaProvider::QoderChina };
    CQoderUsageItem m_workbuddy_item{ QuotaProvider::WorkBuddyChina };
    CQoderUsageItem m_workbuddy_intl_item{ QuotaProvider::WorkBuddyInternational };
    ITrafficMonitor* m_app{};
    bool m_is_chinese{ true };

    mutable std::mutex m_snapshot_mutex;
    struct ProviderState
    {
        QoderSnapshot snapshot;
        CodexRecent::Window history;
        double history_total{};
        std::wstring history_identity;
        bool history_loaded{};
    };
    std::array<ProviderState, static_cast<size_t>(QuotaProvider::Count)> m_provider_states;

    std::mutex m_worker_mutex;
    std::condition_variable m_worker_cv;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_refresh_requested{ false };
    std::thread m_worker;
};

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
