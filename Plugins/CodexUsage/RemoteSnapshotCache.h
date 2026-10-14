#pragma once
#include "CodexUsagePlugin.h"

// Cache only remote fields: local token totals and calendar remain freshly read.
inline bool RestoreCachedRemote(CodexSnapshot& next, const CodexSnapshot& cached, bool usage_ok, bool credits_ok)
{
    bool restored = false;
    if (!next.has_session && cached.has_session)
    {
        next.has_session = true;
        next.session_used = cached.session_used;
        next.session_reset = cached.session_reset;
        restored = true;
    }
    if (!next.has_weekly && cached.has_weekly)
    {
        next.has_weekly = true;
        next.weekly_used = cached.weekly_used;
        next.weekly_reset = cached.weekly_reset;
        restored = true;
    }
    if (!usage_ok)
    {
        next.account_rows = cached.account_rows;
        next.account_details = cached.account_details;
    }
    if (!credits_ok)
    {
        next.reset_credits = cached.reset_credits;
        next.reset_credit_expiries = cached.reset_credit_expiries;
        restored = restored || cached.reset_credits >= 0 || !cached.reset_credit_expiries.empty();
    }
    return restored;
}