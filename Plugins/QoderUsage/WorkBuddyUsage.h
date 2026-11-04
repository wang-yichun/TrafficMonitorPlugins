#pragma once
#include <string>

struct QoderSnapshot;

namespace WorkBuddyUsage
{
    enum class Edition { China, International };
    std::wstring ExecutablePath(Edition edition = Edition::China, const std::wstring& configured_path = {});
    void Refresh(QoderSnapshot& snapshot, bool chinese, Edition edition = Edition::China,
        const std::wstring& configured_path = {});
    // Kept separate from transport so malformed billing responses can be checked offline.
    bool ParseSummary(const std::wstring& json, QoderSnapshot& snapshot, Edition edition = Edition::China);
}
