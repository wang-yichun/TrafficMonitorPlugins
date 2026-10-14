#include "RecentUsage.h"
#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>
#include <ctime>
void Near(double actual, double expected) { if (std::abs(actual - expected) >= 1e-8) std::cerr << "actual=" << actual << " expected=" << expected << "\n"; assert(std::abs(actual - expected) < 1e-8); }
int main(int argc, char** argv)
{
    using CodexRecent::Window;
    Window hour;
    hour.Record(600, 20, 30000, 18000);
    Near(hour.Recent(600, 3600), 0);
    hour.Record(900, 30, 30000, 18000); // Five-minute gap, 2% per minute.
    assert(hour.minutes.size() == 5);
    for (const auto& m : hour.minutes) Near(m.amount, 2);
    Near(hour.Recent(900, 3600), 10);
    Near(hour.Recent(900, 120), 4);
    Near(hour.Recent(900, 90), 3); // Partial minute at the rolling boundary.
    hour.Record(960, 31, 30001, 18000); // Reset timestamp jitter preserves statistics.
    Near(hour.Recent(960, 3600), 11);
    hour.Record(960, 32, 30001, 18000); // Immediate manual refresh is not counted twice.
    hour.Record(960, 32, 30001, 18000);
    Near(hour.Recent(960, 3600), 12);
    const auto before = hour.minutes.size();
    hour.Record(1020, 32, 1020, 18000); // Invalid/expired response adds no reading.
    assert(hour.last_time == 960 && hour.minutes.size() == before);
    hour.Record(1260, 37, 30001, 18000);
    Near(hour.Recent(1260, 300), 5);
    Near(hour.Recent(5000, 3600), 0); // Consumption ages out without new successful requests.
    Window edge;
    edge.Record(600, 70, 900, 18000);
    edge.Record(900, 71, 901, 18000); // One-second shift at expiry is still not a reset.
    Near(edge.Recent(900, 3600), 1);

    Window cycle;
    cycle.Record(600, 80, 900, 18000);
    cycle.Record(1200, 6, 18900, 18000); // Prior expiry at 900, distribute 6% only after it.
    assert(cycle.cycle_start == 900 && cycle.minutes.size() == 5);
    for (const auto& m : cycle.minutes) { assert(m.time >= 900); Near(m.amount, 1.2); }
    Near(cycle.Recent(1200, 3600), 6);
    Near(cycle.Recent(1200, 120), 2.4);
    cycle.Record(1260, 7, 18900, 18000);
    const auto old_count = cycle.minutes.size();
    cycle.Record(1320, 2, 18900, 18000); // Unobserved manual reset: estimate at this reading.
    assert(cycle.cycle_start == 1320 && cycle.minutes.size() > old_count);
    Near(cycle.Recent(1320, 3600), 2); // Earlier-cycle buckets retained but excluded.
    cycle.Record(1380, 3, 18900, 18000);
    Near(cycle.Recent(1380, 3600), 3);
    Window weekly;
    weekly.Record(600, 40, 800000, 604800);
    weekly.Record(1380, 45, 800000, 604800);
    Near(weekly.Recent(1380, 86400), 5); // Session resets do not affect weekly.

    Window cutoff;
    cutoff.Record(600, 20, 30000, 18000);
    cutoff.Record(7800, 40, 30000, 18000); // Two-hour outage: only half lies in the last hour.
    Near(cutoff.Recent(7800, 3600), 10);
    Near(cutoff.Recent(9600, 3600), 5);
    Window day;
    day.Record(600, 0, 800000, 604800);
    day.Record(600 + 2 * 86400, 48, 800000, 604800);
    Near(day.Recent(600 + 2 * 86400, 86400), 24);
    const auto retained = day.minutes.size();
    day.Prune(600 + 7 * 86400 + 3600);
    assert(day.minutes.size() == retained - 60); // Cleanup is independent from the display window.

    std::stringstream saved; saved.precision(17); cycle.Write(saved);
    Window restored; assert(restored.Read(saved));
    Near(restored.Recent(1380, 3600), 3);
    restored.Record(1440, 4, 18900, 18000);
    Near(restored.Recent(1440, 3600), 4);
    std::stringstream legacy("30000 2\n600 20\n900 30\n");
    Window migrated; assert(migrated.ReadLegacy(legacy, 18000));
    Near(migrated.Recent(900, 3600), 10);
    std::stringstream bad_legacy("20001 2\n4800 0\n4800 75\n");
    Window repaired; assert(repaired.ReadLegacy(bad_legacy, 18000));
    Near(repaired.Recent(4800, 3600), 0);
    std::stringstream corrupt("20000 600 20 600 22001"); assert(!restored.Read(corrupt));
    assert(CodexRecent::Coverage(75, 10, 7) == 0.5);
    assert(CodexRecent::Coverage(80, 10, 7) == 1);
    if (argc == 2)
    {
        std::ifstream real(argv[1]);
        std::string version, account;
        Window live_session, live_weekly;
        assert(real >> version >> account);
        const bool loaded = version == "v2" ? live_session.Read(real) && live_weekly.Read(real)
            : version == "v1" && live_session.ReadLegacy(real, 18000) && live_weekly.ReadLegacy(real, 604800);
        assert(loaded);
        const long long now = static_cast<long long>(std::time(nullptr));
        std::cout << "Live history: " << version << ", 5h recent=" << live_session.Recent(now, 3600)
            << "% (" << live_session.minutes.size() << " minute buckets), 7d recent="
            << live_weekly.Recent(now, 86400) << "% (" << live_weekly.minutes.size() << " minute buckets)\n";
    }
    std::cout << "PASS: minute interpolation, gaps, rolling cutoff, reset crossing, independent cycles, retention, v1 migration, persistence\n";
}
