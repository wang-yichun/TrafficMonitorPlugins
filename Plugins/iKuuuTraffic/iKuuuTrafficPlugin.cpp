#include <Windows.h>
#include <TlHelp32.h>
#include <UIAutomation.h>

#include "iKuuuTrafficPlugin.h"

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <deque>
#include <iomanip>
#include <regex>
#include <sstream>
#include <vector>

#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    constexpr wchar_t kProcessName[] = L"iKuuuVPN.exe";
    constexpr wchar_t kTodayLabel[] = L"iKuuu 今日";
    constexpr auto kPollInterval = std::chrono::seconds(15);
    constexpr size_t kMaxUiElements = 4096;
    constexpr size_t kMaxTreeDepth = 48;
    constexpr int kUiaConnectionTimeoutMs = 3000;
    constexpr int kUiaTransactionTimeoutMs = 3000;
    constexpr ULONGLONG kUiaReadDeadlineMs = 8000;

    template<class T>
    class ComPtr
    {
    public:
        ComPtr() = default;
        explicit ComPtr(T* value) : m_value(value) {}
        ~ComPtr() { if (m_value) m_value->Release(); }
        ComPtr(const ComPtr&) = delete;
        ComPtr& operator=(const ComPtr&) = delete;
        ComPtr(ComPtr&& other) noexcept : m_value(other.m_value) { other.m_value = nullptr; }
        ComPtr& operator=(ComPtr&& other) noexcept
        {
            if (this != &other)
            {
                if (m_value) m_value->Release();
                m_value = other.m_value;
                other.m_value = nullptr;
            }
            return *this;
        }
        T* get() const { return m_value; }
        T** put()
        {
            if (m_value) { m_value->Release(); m_value = nullptr; }
            return &m_value;
        }
        T* operator->() const { return m_value; }
        explicit operator bool() const { return m_value != nullptr; }
    private:
        T* m_value{};
    };

    std::vector<DWORD> FindTargetProcesses()
    {
        std::vector<DWORD> processes;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return processes;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, kProcessName) == 0) processes.push_back(entry.th32ProcessID);
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return processes;
    }

    struct WindowSearch
    {
        HWND main_window{};
        HWND visible_window{};
        HWND any_window{};
        const std::vector<DWORD>* process_ids{};
    };

    BOOL CALLBACK FindIkuuuWindow(HWND window, LPARAM parameter)
    {
        auto* search = reinterpret_cast<WindowSearch*>(parameter);
        DWORD process_id{};
        GetWindowThreadProcessId(window, &process_id);
        if (!search->process_ids || std::find(search->process_ids->begin(), search->process_ids->end(), process_id) == search->process_ids->end()) return TRUE;
        if (!search->any_window) search->any_window = window;
        if (IsWindowVisible(window) && !IsIconic(window) && !search->visible_window) search->visible_window = window;
        wchar_t title[256]{};
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        if (_wcsicmp(title, L"iKuuuVPN") == 0) search->main_window = window;
        return TRUE;
    }

    std::wstring ElementName(IUIAutomationElement* element)
    {
        BSTR value{};
        if (!element || FAILED(element->get_CurrentName(&value)) || !value) return {};
        std::wstring result(value, SysStringLen(value));
        SysFreeString(value);
        return result;
    }

    bool ParseTraffic(const std::wstring& text, double& used, double& total, double& today);

    bool LooksLikeTrafficText(const std::wstring& name)
    {
        static const std::wregex unit_amount(LR"([0-9]+(?:[.,][0-9]+)?\s*(?:B|KB|MB|GB|TB|KiB|MiB|GiB|TiB))", std::regex_constants::icase);
        static const std::wregex traffic_label(LR"(流量统计|流量用量|今日已用|今日用量|今天已用|today|traffic|used\s*today)", std::regex_constants::icase);
        return std::regex_search(name, unit_amount) || std::regex_search(name, traffic_label);
    }

    bool ReadWindowText(HWND window, std::wstring& traffic_text, const std::atomic<bool>& stop)
    {
        ComPtr<IUIAutomation2> automation;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation.put())))) return false;
        if (FAILED(automation->put_ConnectionTimeout(kUiaConnectionTimeoutMs)) ||
            FAILED(automation->put_TransactionTimeout(kUiaTransactionTimeoutMs))) return false;
        ComPtr<IUIAutomationElement> root;
        if (FAILED(automation->ElementFromHandle(window, root.put())) || !root) return false;
        ComPtr<IUIAutomationTreeWalker> walker;
        if (FAILED(automation->get_ControlViewWalker(walker.put())) || !walker) return false;

        struct Entry { ComPtr<IUIAutomationElement> element; size_t depth; };
        std::deque<Entry> pending;
        pending.push_back({ std::move(root), 0 });
        size_t visited = 0;
        const ULONGLONG deadline = GetTickCount64() + kUiaReadDeadlineMs;
        while (!pending.empty() && visited++ < kMaxUiElements && GetTickCount64() < deadline && !stop.load())
        {
            Entry current = std::move(pending.front());
            pending.pop_front();
            const std::wstring name = ElementName(current.element.get());
            if (LooksLikeTrafficText(name))
            {
                if (!traffic_text.empty()) traffic_text.push_back(L' ');
                traffic_text.append(name, 0, (std::min)(name.size(), size_t{ 512 }));
                if (traffic_text.size() > 4096) traffic_text.erase(0, traffic_text.size() - 2048);
                double used{}, total{}, today{};
                if (ParseTraffic(traffic_text, used, total, today)) return true;
            }
            if (current.depth >= kMaxTreeDepth) continue;

            ComPtr<IUIAutomationElement> child;
            if (SUCCEEDED(walker->GetFirstChildElement(current.element.get(), child.put())) && child)
            {
                while (child && pending.size() + visited < kMaxUiElements && GetTickCount64() < deadline && !stop.load())
                {
                    ComPtr<IUIAutomationElement> next;
                    if (FAILED(walker->GetNextSiblingElement(child.get(), next.put()))) break;
                    pending.push_back({ std::move(child), current.depth + 1 });
                    child = std::move(next);
                }
            }
        }
        return false;
    }

    bool ParseTraffic(const std::wstring& text, double& used, double& total, double& today)
    {
        static const std::wregex pattern(
            LR"(([0-9]+(?:[.,][0-9]+)?)\s*(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)\s*/\s*([0-9]+(?:[.,][0-9]+)?)\s*(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)[\s\S]{0,160}?(?:今日已用|今日用量|今天已用|今天使用|今日使用量|used\s*today|today(?:'s)?\s*(?:used|usage|traffic))\s*[:：]?\s*([0-9]+(?:[.,][0-9]+)?)\s*(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB))",
            std::regex_constants::icase);
        std::wsmatch match;
        if (!std::regex_search(text, match, pattern)) return false;
        auto number = [](const std::wstring& value, double& output) {
            std::wstring normalized = value;
            std::replace(normalized.begin(), normalized.end(), L',', L'.');
            wchar_t* end{};
            output = wcstod(normalized.c_str(), &end);
            return end != normalized.c_str() && *end == L'\0' && output >= 0.0;
        };
        auto amount_gb = [&](const std::wstring& value, const std::wstring& unit, double& output) {
            if (!number(value, output)) return false;
            const std::wstring lower = [&] { std::wstring result = unit; std::transform(result.begin(), result.end(), result.begin(), towlower); return result; }();
            const bool binary = lower.find(L"i") != std::wstring::npos;
            const double base = binary ? 1024.0 : 1000.0;
            if (lower == L"b") output /= base * base * base;
            else if (lower == L"kb" || lower == L"kib") output /= base * base;
            else if (lower == L"mb" || lower == L"mib") output /= base;
            else if (lower == L"tb" || lower == L"tib") output *= base;
            return true;
        };
        return amount_gb(match[1].str(), match[2].str(), used) && amount_gb(match[3].str(), match[4].str(), total) &&
            amount_gb(match[5].str(), match[6].str(), today) && total > 0.0 && used <= total * 1.05;
    }

    std::wstring FormatGb(double value, int decimals)
    {
        std::wostringstream output;
        output << std::fixed << std::setprecision(decimals) << value;
        return output.str();
    }

    std::wstring FormatItemValue(const IKuuuSnapshot& snapshot, bool today)
    {
        std::wstring value;
        if (!snapshot.has_values) value = L"不可用";
        else if (today) value = FormatGb(snapshot.today_gb, 2) + L" GB";
        else value = FormatGb(snapshot.used_gb, 2) + L"/" + FormatGb(snapshot.total_gb, 0) + L" GB";
        if (snapshot.has_values && snapshot.state != IKuuuSnapshot::State::live) value += L" (旧)";
        return value;
    }

    int MeasureTextWidth(HDC dc, const std::wstring& text)
    {
        if (!dc || text.empty()) return 0;
        SIZE size{};
        if (!GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size)) return 0;
        return size.cx;
    }

    struct ItemLayoutMetrics
    {
        int padding{ 2 };
        int label_value_gap{ 2 };
    };

    ItemLayoutMetrics GetItemLayoutMetrics(HDC dc)
    {
        TEXTMETRICW metrics{};
        if (!dc || !GetTextMetricsW(dc, &metrics)) return {};
        const int font_height = (std::max)(1, static_cast<int>(metrics.tmHeight));
        return {
            (std::max)(2, MulDiv(font_height, 4, 16)),
            (std::max)(2, MulDiv(font_height, 3, 16))
        };
    }

    std::wstring StateText(IKuuuSnapshot::State state)
    {
        switch (state)
        {
        case IKuuuSnapshot::State::app_not_running: return L"iKuuuVPN 未运行";
        case IKuuuSnapshot::State::window_not_available: return L"iKuuuVPN 窗口不可见";
        case IKuuuSnapshot::State::stats_not_exposed: return L"当前页面未提供流量统计";
        case IKuuuSnapshot::State::live: return L"已读取应用统计";
        }
        return L"不可用";
    }
}

CIKuuuTrafficPlugin& CIKuuuTrafficPlugin::Instance()
{
    static CIKuuuTrafficPlugin instance;
    return instance;
}

IPluginItem* CIKuuuTrafficPlugin::GetItem(int index)
{
    if (index == 0) return &m_quota_item;
    if (index == 1) return &m_today_item;
    return nullptr;
}

void CIKuuuTrafficPlugin::DataRequired()
{
    // Polling is deliberately performed by the worker; the host's render/update thread only reads a snapshot.
}

const wchar_t* CIKuuuTrafficPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME: return L"iKuuu 流量统计";
    case TMI_DESCRIPTION: return L"读取 iKuuuVPN 窗口中当前可见的流量用量";
    case TMI_AUTHOR: return L"Codex";
    case TMI_COPYRIGHT: return L"Copyright (C) 2026";
    case TMI_VERSION: return L"1.00";
    case TMI_URL: return L"";
    default: return L"";
    }
}

void CIKuuuTrafficPlugin::OnInitialize(ITrafficMonitor*)
{
    std::lock_guard<std::mutex> lock(m_worker_mutex);
    m_stop.store(false);
    if (!m_worker.joinable()) m_worker = std::thread(&CIKuuuTrafficPlugin::PollLoop, this);
}

void CIKuuuTrafficPlugin::OnShutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_worker_mutex);
        m_stop.store(true);
    }
    m_worker_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

void CIKuuuTrafficPlugin::PollLoop()
{
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(m_worker_mutex);
            if (m_stop.load()) break;
        }
        RefreshSnapshot();
        std::unique_lock<std::mutex> lock(m_worker_mutex);
        if (m_worker_cv.wait_for(lock, kPollInterval, [this] { return m_stop.load(); })) break;
    }
    if (SUCCEEDED(com_result)) CoUninitialize();
}

void CIKuuuTrafficPlugin::RefreshSnapshot()
{
    IKuuuSnapshot next;
    const std::vector<DWORD> process_ids = FindTargetProcesses();
    WindowSearch search;
    search.process_ids = &process_ids;
    EnumWindows(FindIkuuuWindow, reinterpret_cast<LPARAM>(&search));
    HWND window = search.main_window ? search.main_window : (search.visible_window ? search.visible_window : search.any_window);
    if (process_ids.empty())
    {
        next.state = IKuuuSnapshot::State::app_not_running;
    }
    else if (!window)
    {
        next.state = IKuuuSnapshot::State::window_not_available;
    }
    else
    {
        next.state = IsIconic(window) || !IsWindowVisible(window)
            ? IKuuuSnapshot::State::window_not_available : IKuuuSnapshot::State::stats_not_exposed;
        std::wstring text;
        double used{}, total{}, today{};
        if (ReadWindowText(window, text, m_stop) && ParseTraffic(text, used, total, today))
        {
            next.state = IKuuuSnapshot::State::live;
            next.has_values = true;
            next.used_gb = used;
            next.total_gb = total;
            next.today_gb = today;
            SYSTEMTIME now{};
            GetLocalTime(&now);
            std::wostringstream time;
            time << now.wYear << L"-" << std::setfill(L'0') << std::setw(2) << now.wMonth << L"-"
                << std::setw(2) << now.wDay << L" " << std::setw(2) << now.wHour << L":" << std::setw(2) << now.wMinute;
            next.last_read_time = time.str();
        }
    }

    std::lock_guard<std::mutex> lock(m_snapshot_mutex);
    if (next.has_values)
    {
        m_snapshot = std::move(next);
    }
    else
    {
        // Retain the last successful values and expose their stale state in the item text and tooltip.
        m_snapshot.state = next.state;
    }
}

IKuuuSnapshot CIKuuuTrafficPlugin::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_snapshot_mutex);
    return m_snapshot;
}

const wchar_t* CIKuuuTrafficPlugin::GetTooltipInfo()
{
    thread_local std::wstring tooltip;
    const IKuuuSnapshot snapshot = Snapshot();
    tooltip = StateText(snapshot.state);
    if (snapshot.has_values)
    {
        tooltip += L"\r\n已用: " + FormatGb(snapshot.used_gb, 2) + L" / " + FormatGb(snapshot.total_gb, 2) + L" GB";
        const double remaining_gb = (std::max)(0.0, snapshot.total_gb - snapshot.used_gb);
        const double remaining_ratio = snapshot.total_gb > 0.0 ? remaining_gb / snapshot.total_gb : 0.0;
        tooltip += L"\r\n剩余: " + FormatGb(remaining_gb, 2) + L" GB (" + FormatGb(remaining_ratio * 100.0, 1) + L"%)";
        tooltip += L"\r\n今日已用: " + FormatGb(snapshot.today_gb, 2) + L" GB";
        tooltip += L"\r\n最后读取: " + snapshot.last_read_time;
        if (snapshot.state != IKuuuSnapshot::State::live) tooltip += L"（数据可能已过期）";
    }
    else
    {
        tooltip += L"\r\n等待流量统计页面";
    }
    return tooltip.c_str();
}

void CIKuuuTrafficPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_DRAW_RESOURCE_USAGE_GRAPH)
    {
        m_quota_graph_context.store(data && data[0] == L'1');
    }
    else if (index == EI_DRAW_TASKBAR_WND && (!data || data[0] != L'1'))
    {
        m_quota_graph_context.store(false);
    }
}

const wchar_t* CIKuuuTrafficItem::GetItemName() const
{
    return m_today ? L"iKuuu 今日流量" : L"iKuuu 总流量";
}

const wchar_t* CIKuuuTrafficItem::GetItemId() const
{
    return m_today ? L"iKuuuTodayGB1" : L"iKuuuQuotaGB1";
}

const wchar_t* CIKuuuTrafficItem::GetItemLableText() const
{
    return m_today ? L"iKuuu 今日" : L"";
}

const wchar_t* CIKuuuTrafficItem::GetItemValueText() const
{
    thread_local std::wstring values[2];
    std::wstring& value = values[m_today ? 1 : 0];
    const IKuuuSnapshot snapshot = CIKuuuTrafficPlugin::Instance().Snapshot();
    value = FormatItemValue(snapshot, m_today);
    return value.c_str();
}

const wchar_t* CIKuuuTrafficItem::GetItemValueSampleText() const
{
    return m_today ? L"999.99 GB (旧)" : L"999.99/999.99 GB (旧)";
}

int CIKuuuTrafficItem::GetItemWidthEx(void* hDC) const
{
    HDC dc = static_cast<HDC>(hDC);
    bool release_dc = false;
    if (!dc)
    {
        dc = GetDC(nullptr);
        release_dc = dc != nullptr;
    }
    if (!dc) return 0;

    const IKuuuSnapshot snapshot = CIKuuuTrafficPlugin::Instance().Snapshot();
    const std::wstring value = FormatItemValue(snapshot, m_today);
    const int label_width = m_today ? MeasureTextWidth(dc, kTodayLabel) : 0;
    const ItemLayoutMetrics layout = GetItemLayoutMetrics(dc);
    const int value_width = MeasureTextWidth(dc, value);
    if (release_dc) ReleaseDC(nullptr, dc);

    return layout.padding + label_width + (m_today ? layout.label_value_gap : 0) + value_width + layout.padding;
}

void CIKuuuTrafficItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    HDC dc = static_cast<HDC>(hDC);
    if (!dc) return;
    const int saved = SaveDC(dc);
    if (saved == 0) return;
    SetBkMode(dc, TRANSPARENT);
    const ItemLayoutMetrics layout = GetItemLayoutMetrics(dc);

    const IKuuuSnapshot snapshot = CIKuuuTrafficPlugin::Instance().Snapshot();
    const std::wstring value = FormatItemValue(snapshot, m_today);

    if (!m_today && CIKuuuTrafficPlugin::Instance().IsQuotaGraphContextActive() &&
        snapshot.has_values && snapshot.total_gb > 0.0 && w > 0 && h > 0)
    {
        // Match the host's mode-2 boundary: it truncates the graph ratio to an integer percent,
        // then computes the filled pixel width as rect.Width() * percent / 100.
        const int remaining_percent = (std::clamp)(
            static_cast<int>(GetResourceUsageGraphValue() * 100.0f), 0, 100);
        const int remaining_width = w * remaining_percent / 100;
        const double remaining_fraction = (std::max)(0.0, (snapshot.total_gb - snapshot.used_gb) / snapshot.total_gb);
        const double consumed_fraction = (std::clamp)(1.0 - remaining_fraction, 0.0, 1.0);
        const double today_fraction = (std::clamp)(snapshot.today_gb / snapshot.total_gb, 0.0, consumed_fraction);
        const int today_width = static_cast<int>(w * today_fraction);
        const int overlay_left = x + remaining_width;
        const int overlay_right = (std::min)(x + w, overlay_left + today_width);
        if (overlay_right > overlay_left)
        {
            const COLORREF overlay_color = dark_mode ? RGB(66, 73, 81) : RGB(174, 181, 190);
            HBRUSH brush = CreateSolidBrush(overlay_color);
            if (brush)
            {
                RECT overlay{ overlay_left, y, overlay_right, y + h };
                FillRect(dc, &overlay, brush);
                DeleteObject(brush);
            }
        }
    }

    int text_x = x + layout.padding;
    RECT rect{ text_x, y, x + w - layout.padding, y + h };
    if (m_today)
    {
        DrawTextW(dc, kTodayLabel, static_cast<int>(std::size(kTodayLabel) - 1), &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        text_x += MeasureTextWidth(dc, kTodayLabel) + layout.label_value_gap;
    }
    rect.left = text_x;
    rect.right = x + w - layout.padding;
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (saved != 0) RestoreDC(dc, saved);
}

float CIKuuuTrafficItem::GetResourceUsageGraphValue() const
{
    if (m_today) return 0.0f;
    const IKuuuSnapshot snapshot = CIKuuuTrafficPlugin::Instance().Snapshot();
    if (!snapshot.has_values || snapshot.total_gb <= 0.0) return 0.0f;
    const double remaining = (std::max)(0.0, snapshot.total_gb - snapshot.used_gb);
    return static_cast<float>((std::min)(1.0, (std::max)(0.0, remaining / snapshot.total_gb)));
}

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CIKuuuTrafficPlugin::Instance();
}
