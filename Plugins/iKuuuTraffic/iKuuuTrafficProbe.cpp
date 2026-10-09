#include <Windows.h>

#include "PluginInterface.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <sstream>

using GetPluginInstance = ITMPlugin* (*)();

namespace
{
    void PrintUtf8(const std::wstring& text);

    bool ParseLeadingNumber(const std::wstring& text, double& value)
    {
        wchar_t* end{};
        value = std::wcstod(text.c_str(), &end);
        return end != text.c_str() && std::isfinite(value) && value >= 0.0;
    }

    bool GetQuotaValues(IPluginItem* quota, IPluginItem* today, double& total_gb, double& today_gb)
    {
        if (!quota || !today) return false;
        const std::wstring quota_text = quota->GetItemValueText();
        const size_t separator = quota_text.find(L'/');
        double used_gb{};
        if (separator == std::wstring::npos || !ParseLeadingNumber(quota_text.substr(0, separator), used_gb)) return false;
        (void)used_gb;
        if (!ParseLeadingNumber(quota_text.substr(separator + 1), total_gb) || total_gb <= 0.0) return false;
        return ParseLeadingNumber(today->GetItemValueText(), today_gb);
    }

    bool CheckQuotaOverlay(ITMPlugin* plugin, IPluginItem* quota, double total_gb, double today_gb, HFONT font)
    {
        const float remaining_ratio = quota->GetResourceUsageGraphValue();
        const double consumed_fraction = (std::clamp)(1.0 - static_cast<double>(remaining_ratio), 0.0, 1.0);
        const double today_fraction = (std::clamp)(today_gb / total_gb, 0.0, consumed_fraction);
        const double requested_width = today_fraction > 0.0 ? std::ceil(3.0 / today_fraction) : 0.0;
        if (requested_width > 32768.0 || today_fraction <= 0.0)
        {
            std::wcerr << L"Quota overlay DIB check skipped: today's fraction is too small or unavailable.\n";
            return false;
        }
        const int width = (std::max)(512, static_cast<int>(requested_width));
        const int height = 64;

        const int remaining_percent = (std::clamp)(static_cast<int>(remaining_ratio * 100.0f), 0, 100);
        const int remaining_width = width * remaining_percent / 100;
        const int today_width = static_cast<int>(width * today_fraction);
        const int overlay_right = (std::min)(width, remaining_width + today_width);
        if (today_width < 3 || remaining_width < 0 || overlay_right > width || overlay_right < remaining_width)
        {
            std::wcerr << L"Quota overlay geometry check failed.\n";
            return false;
        }

        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO bitmap_info{};
        bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
        bitmap_info.bmiHeader.biWidth = width;
        bitmap_info.bmiHeader.biHeight = -height;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        void* bits{};
        HBITMAP bitmap = CreateDIBSection(dc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits)
        {
            if (bitmap) DeleteObject(bitmap);
            DeleteDC(dc);
            return false;
        }
        HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
        HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
        SetBkMode(dc, TRANSPARENT);

        const COLORREF track_color = RGB(194, 202, 211);
        const COLORREF host_fill_color = RGB(90, 110, 130);
        const COLORREF expected_overlay_color = RGB(174, 181, 190);
        auto render = [&](bool taskbar_context, bool graph_context) {
            plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, taskbar_context ? L"1" : L"0");
            plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_RESOURCE_USAGE_GRAPH, graph_context ? L"1" : L"0");
            HBRUSH track_brush = CreateSolidBrush(track_color);
            HBRUSH fill_brush = CreateSolidBrush(host_fill_color);
            RECT track{ 0, 0, width, height };
            FillRect(dc, &track, track_brush);
            RECT fill{ 0, 0, remaining_width, height };
            FillRect(dc, &fill, fill_brush);
            DeleteObject(track_brush);
            DeleteObject(fill_brush);
            quota->DrawItem(dc, 0, 0, width, height, false);
            return GetPixel(dc, remaining_width + today_width / 2, 1);
        };

        const COLORREF without_graph = render(true, false);
        const COLORREF with_graph = render(true, true);
        plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, L"1");
        plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_RESOURCE_USAGE_GRAPH, L"1");
        plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, L"0");
        const COLORREF after_taskbar = [&] {
            HBRUSH track_brush = CreateSolidBrush(track_color);
            HBRUSH fill_brush = CreateSolidBrush(host_fill_color);
            RECT track{ 0, 0, width, height };
            FillRect(dc, &track, track_brush);
            RECT fill{ 0, 0, remaining_width, height };
            FillRect(dc, &fill, fill_brush);
            DeleteObject(track_brush);
            DeleteObject(fill_brush);
            quota->DrawItem(dc, 0, 0, width, height, false);
            return GetPixel(dc, remaining_width + today_width / 2, 1);
        }();
        const bool pixels_ok = without_graph == track_color && with_graph == expected_overlay_color && after_taskbar == track_color;

        if (old_font && old_font != HGDI_ERROR) SelectObject(dc, old_font);
        if (old_bitmap && old_bitmap != HGDI_ERROR) SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);

        std::wostringstream result;
        result << L"Quota overlay DIB: remaining=" << remaining_ratio << L", today=" << today_fraction
            << L", remainingPixels=" << remaining_width << L", todayPixels=" << today_width
            << L", right=" << overlay_right << L"/" << width
            << L", graphOff=" << without_graph << L", graphOn=" << with_graph
            << L", mainSkin=" << after_taskbar << L"\n";
        PrintUtf8(result.str());
        return pixels_ok;
    }

    void PrintUtf8(const std::wstring& text)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return;
        std::string output(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size, nullptr, nullptr);
        std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::wcerr << L"Usage: iKuuuTrafficProbe.exe <path-to-iKuuuTraffic.dll> [duration-seconds]\n";
        return 2;
    }
    const int duration_seconds = argc == 3 ? (std::max)(1, _wtoi(argv[2])) : 5;

    HMODULE module = LoadLibraryW(argv[1]);
    if (!module)
    {
        std::wcerr << L"LoadLibrary failed: " << GetLastError() << L"\n";
        return 3;
    }
    auto get_instance = reinterpret_cast<GetPluginInstance>(GetProcAddress(module, "TMPluginGetInstance"));
    if (!get_instance)
    {
        std::wcerr << L"TMPluginGetInstance export was not found.\n";
        FreeLibrary(module);
        return 4;
    }

    ITMPlugin* plugin = get_instance();
    if (!plugin)
    {
        std::wcerr << L"Plugin instance was not returned.\n";
        FreeLibrary(module);
        return 5;
    }
    plugin->OnInitialize(nullptr);
    HDC measure_dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ previous_font = measure_dc && font ? SelectObject(measure_dc, font) : nullptr;

    bool contracts_ok = true;
    IPluginItem* quota = plugin->GetItem(0);
    IPluginItem* today = plugin->GetItem(1);
    if (!quota || !today)
    {
        std::wcerr << L"Expected both quota and today items.\n";
        contracts_ok = false;
    }
    else
    {
        contracts_ok = quota->IsCustomDraw() && today->IsCustomDraw() &&
            std::wstring(quota->GetItemLableText()).empty() &&
            today->IsDrawResourceUsageGraph() == 0 && quota->IsDrawResourceUsageGraph() == 2;
        if (!contracts_ok) std::wcerr << L"Custom draw, labels, or graph mode contract failed.\n";
    }

    PrintUtf8(L"Polling iKuuu traffic text for " + std::to_wstring(duration_seconds) + L" seconds.\n");
    bool ratio_ok = true;
    for (int second = 0; second < duration_seconds; ++second)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::wstring line = L"[" + std::to_wstring(second + 1) + L"s] ";
        for (int index = 0; index < 2; ++index)
        {
            IPluginItem* item = plugin->GetItem(index);
            if (index) line += L" | ";
            line += item ? item->GetItemName() : L"<missing item>";
            line += L": ";
            line += item ? item->GetItemValueText() : L"";
            if (item && measure_dc)
            {
                std::wostringstream metrics;
                const float fraction = item->GetResourceUsageGraphValue();
                const std::wstring value_text = item->GetItemValueText();
                const bool unavailable = value_text == L"不可用";
                if (fraction < 0.0f || fraction > 1.0f || (unavailable && fraction != 0.0f)) ratio_ok = false;
                metrics << L" [label='" << item->GetItemLableText() << L"', custom=" << item->IsCustomDraw()
                    << L", width=" << item->GetItemWidthEx(measure_dc) << L"px, graph=" << item->IsDrawResourceUsageGraph()
                    << L", fraction=" << std::fixed << std::setprecision(4) << fraction << L"]";
                line += metrics.str();
            }
        }
        line += L"\n";
        PrintUtf8(line);
    }
    bool graph_draw_ok = true;
    double total_gb{}, today_gb{};
    if (quota && today && GetQuotaValues(quota, today, total_gb, today_gb))
    {
        graph_draw_ok = CheckQuotaOverlay(plugin, quota, total_gb, today_gb, font);
    }
    else
    {
        PrintUtf8(L"Quota overlay DIB check skipped: live quota/today values are unavailable.\n");
    }
    PrintUtf8(std::wstring(plugin->GetTooltipInfo()) + L"\n");
    plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_RESOURCE_USAGE_GRAPH, L"0");
    plugin->OnExtenedInfo(ITMPlugin::EI_DRAW_TASKBAR_WND, L"0");
    plugin->OnShutdown();
    if (measure_dc && previous_font) SelectObject(measure_dc, previous_font);
    if (font) DeleteObject(font);
    if (measure_dc) DeleteDC(measure_dc);
    FreeLibrary(module);
    PrintUtf8(L"Plugin shutdown and DLL unload completed.\n");
    return contracts_ok && ratio_ok && graph_draw_ok ? 0 : 6;
}
