#pragma once

#include "PluginInterface.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

struct IKuuuSnapshot
{
    enum class State { app_not_running, window_not_available, stats_not_exposed, live };

    State state{ State::app_not_running };
    bool has_values{};
    double used_gb{};
    double total_gb{};
    double today_gb{};
    std::wstring last_read_time;
};

class CIKuuuTrafficItem : public IPluginItem
{
public:
    explicit CIKuuuTrafficItem(bool today) : m_today(today) {}

    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;
    bool IsCustomDraw() const override { return true; }
    int GetItemWidthEx(void* hDC) const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    int IsDrawResourceUsageGraph() const override { return m_today ? 0 : 2; }
    float GetResourceUsageGraphValue() const override;

private:
    bool m_today{};
};

class CIKuuuTrafficPlugin : public ITMPlugin
{
public:
    static CIKuuuTrafficPlugin& Instance();

    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    void OnInitialize(ITrafficMonitor* app) override;
    void OnShutdown() override;
    const wchar_t* GetTooltipInfo() override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    IKuuuSnapshot Snapshot() const;
    bool IsQuotaGraphContextActive() const { return m_quota_graph_context.load(); }

private:
    CIKuuuTrafficPlugin() = default;
    void PollLoop();
    void RefreshSnapshot();

    CIKuuuTrafficItem m_quota_item{ false };
    CIKuuuTrafficItem m_today_item{ true };
    mutable std::mutex m_snapshot_mutex;
    IKuuuSnapshot m_snapshot;
    std::mutex m_worker_mutex;
    std::condition_variable m_worker_cv;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_quota_graph_context{ false };
    std::thread m_worker;
};

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance();
