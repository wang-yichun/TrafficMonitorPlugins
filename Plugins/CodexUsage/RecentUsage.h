#pragma once
#include <algorithm>
#include <cmath>
#include <deque>
#include <istream>
#include <ostream>

namespace CodexRecent
{
constexpr long long Retention = 7 * 86400;
struct Minute { long long time; double amount; long long cycle; };
class Window
{
public:
    long long reset{}, last_time{}, cycle_start{};
    double last_used{};
    std::deque<Minute> minutes;

    void Record(long long now, double used, long long new_reset, long long cycle_duration)
    {
        if (now <= 0 || !std::isfinite(used) || new_reset <= now || cycle_duration <= 0) return;
        used = (std::clamp)(used, 0.0, 100.0);
        if (last_time == 0 || now < last_time)
        {
            // First observation has no previous meter reading to subtract.
            if (now < last_time) minutes.clear();
            reset = new_reset; last_time = cycle_start = now; last_used = used;
            return;
        }
        const bool expired = reset <= now && new_reset > reset + 60;
        const bool dropped = used < last_used - 0.5;
        if (expired || dropped)
        {
            // Prefer the reported start of the new cycle if it lies between readings.
            // Otherwise a known expiry is usable; an early manual reset is estimated at now.
            const long long reported_start = new_reset - cycle_duration;
            long long start = now;
            if (reported_start >= last_time && reported_start <= now) start = reported_start;
            else if (expired) start = (std::max)(last_time, reset);
            cycle_start = start;
            Distribute(start, now, used);
            last_used = used;
        }
        else
        {
            Distribute(last_time, now, (std::max)(0.0, used - last_used));
            // Ignore small corrections rather than counting their recovery twice.
            last_used = (std::max)(last_used, used);
        }
        last_time = now;
        reset = new_reset; // A moving reset timestamp alone never creates consumption.
    }

    double Recent(long long now, long long duration) const
    {
        if (last_time == 0 || now < last_time || reset <= now || duration <= 0) return 0;
        const long long cutoff = (std::max)(now - duration, cycle_start);
        double total = 0;
        for (const auto& minute : minutes)
        {
            if (minute.cycle != cycle_start) continue;
            const long long start = (std::max)(minute.time, cycle_start);
            const long long end = (std::min)(minute.time + 60, last_time);
            if (end > start)
            {
                const long long overlap = end - (std::max)(start, cutoff);
                if (overlap > 0) total += minute.amount * static_cast<double>(overlap) / (end - start);
            }
            else if (start >= cutoff && start <= now) total += minute.amount;
        }
        return (std::clamp)(total, 0.0, 100.0);
    }

    // Retention is independent from recording, window queries, and cycle changes.
    void Prune(long long now)
    {
        while (!minutes.empty() && minutes.front().time + 60 <= now - Retention) minutes.pop_front();
    }
    void Write(std::ostream& out) const
    {
        out << reset << ' ' << last_time << ' ' << last_used << ' ' << cycle_start << ' ' << minutes.size() << '\n';
        for (const auto& m : minutes) out << m.time << ' ' << m.amount << ' ' << m.cycle << '\n';
    }
    bool Read(std::istream& in)
    {
        Window loaded;
        size_t count{};
        if (!(in >> loaded.reset >> loaded.last_time >> loaded.last_used >> loaded.cycle_start >> count) ||
            count > 22000 || !std::isfinite(loaded.last_used) || loaded.last_used < 0 || loaded.last_used > 100 ||
            loaded.last_time < 0 || loaded.cycle_start < 0 || loaded.cycle_start > loaded.last_time) return false;
        for (size_t i = 0; i < count; ++i)
        {
            Minute m{};
            if (!(in >> m.time >> m.amount >> m.cycle) || !std::isfinite(m.amount) || m.amount < 0 || m.amount > 100 ||
                m.time < 0 || m.time % 60 != 0 || m.time > loaded.last_time || m.cycle < 0 ||
                m.cycle > loaded.last_time || (!loaded.minutes.empty() && m.time < loaded.minutes.back().time)) return false;
            loaded.minutes.push_back(m);
        }
        *this = loaded;
        return true;
    }
    bool ReadLegacy(std::istream& in, long long cycle_duration)
    {
        long long legacy_reset{};
        size_t count{};
        if (!(in >> legacy_reset >> count) || count > 2000) return false;
        std::deque<std::pair<long long, double>> readings;
        for (size_t i = 0; i < count; ++i)
        {
            long long t{}; double used{};
            if (!(in >> t >> used) || t <= 0 || !std::isfinite(used) || used < 0 || used > 100 ||
                (!readings.empty() && (t < readings.back().first || used < readings.back().second))) return false;
            readings.emplace_back(t, used);
        }
        // Repair the old false-reset zero without discarding the remaining observations.
        if (readings.size() >= 2 && readings[0].second == 0 && readings[0].first == readings[1].first &&
            readings[0].first > legacy_reset - cycle_duration + 60) readings.pop_front();
        Window loaded;
        for (const auto& r : readings) loaded.Record(r.first, r.second, legacy_reset, cycle_duration);
        *this = loaded;
        return true;
    }
private:
    void Add(long long minute, double amount)
    {
        if (!minutes.empty() && minutes.back().time == minute && minutes.back().cycle == cycle_start)
            minutes.back().amount += amount;
        else minutes.push_back({minute, amount, cycle_start});
    }
    void Distribute(long long start, long long end, double amount)
    {
        if (end <= start)
        {
            if (amount > 0) Add((end == cycle_start ? end : end - 1) / 60 * 60, amount);
            return;
        }
        const double rate = amount / static_cast<double>(end - start);
        // Very long outages cannot allocate unbounded memory before the retention pass.
        const long long first = (std::max)(start / 60 * 60, (end - Retention) / 60 * 60);
        for (long long t = first; t < end; t += 60)
        {
            const long long overlap = (std::min)(end, t + 60) - (std::max)(start, t);
            if (overlap > 0) Add(t, rate * overlap);
        }
    }
};
inline double Coverage(double percent, int segments, int index)
{
    return (std::clamp)(percent * segments / 100.0 - index, 0.0, 1.0);
}
}
