#pragma once

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>
#include "WorkdayCalendar.h"

namespace CodexTimeBar
{
    inline bool IsWorkingTime(long long timestamp, bool weekly, const CodexCalendar::Calendar& calendar)
    {
        const long long beijing = timestamp + 8 * 3600;
        const long long day = beijing / 86400;
        if (!calendar.IsWorkday(day)) return false;
        if (weekly) return true;
        const long long seconds = beijing % 86400;
        return (seconds >= (9 * 60 + 30) * 60 && seconds < 12 * 3600) ||
            (seconds >= (13 * 60 + 30) * 60 && seconds < (18 * 60 + 30) * 60);
    }

    inline std::vector<int> MarkerPixels(long long now, long long reset, bool weekly, int width,
        const CodexCalendar::Calendar& calendar = {})
    {
        std::vector<int> pixels;
        if (width <= 0 || reset <= now) return pixels;
        constexpr long long day_seconds = 86400;
        constexpr long long beijing_offset = 8 * 3600;
        const long long duration = weekly ? 7 * day_seconds : 5 * 3600;
        const long long visible_start = (std::max)(now, reset - duration);
        const long long first_day = (visible_start + beijing_offset) / day_seconds;
        const long long last_day = (reset + beijing_offset) / day_seconds;
        const auto add = [&](long long boundary) {
            if (boundary <= visible_start || boundary >= reset) return;
            // The remaining bar shrinks from the right; reset is its left endpoint.
            const int pixel = static_cast<int>(std::lround(width * static_cast<double>(reset - boundary) / duration));
            const int visible_width = static_cast<int>(std::lround(width * (std::min)(1.0, static_cast<double>(reset - now) / duration)));
            if (pixel > 0 && pixel < visible_width - 1) pixels.push_back(pixel);
        };
        for (long long day = first_day; day <= last_day; ++day)
        {
            const long long midnight = day * day_seconds - beijing_offset;
            if (weekly)
            {
                if (calendar.IsWorkday(day) != calendar.IsWorkday(day - 1)) add(midnight);
            }
            else if (calendar.IsWorkday(day))
            {
                for (int minutes : { 9 * 60 + 30, 12 * 60, 13 * 60 + 30, 18 * 60 + 30 })
                    add(midnight + minutes * 60);
            }
        }
        std::sort(pixels.begin(), pixels.end());
        pixels.erase(std::unique(pixels.begin(), pixels.end()), pixels.end());
        return pixels;
    }
}
