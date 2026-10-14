#include "RemoteSnapshotCache.h"
#include <cassert>
#include <iostream>
int main()
{
    CodexSnapshot cached;
    cached.has_session = cached.has_weekly = true;
    cached.session_used = 21; cached.weekly_used = 42;
    cached.session_reset = 123; cached.weekly_reset = 456;
    cached.reset_credits = 2; cached.reset_credit_expiries = {789, 999};
    cached.account_rows = {{L"account", L"cached"}};
    cached.account_details = L"remote details";
    CodexSnapshot failed;
    failed.total_tokens = 17;
    assert(RestoreCachedRemote(failed, cached, false, false));
    assert(failed.has_session && failed.has_weekly && failed.session_used == 21 && failed.weekly_used == 42);
    assert(failed.session_reset == 123 && failed.weekly_reset == 456);
    assert(failed.reset_credits == 2 && failed.reset_credit_expiries == cached.reset_credit_expiries);
    assert(failed.total_tokens == 17 && failed.account_details == L"remote details");
    CodexSnapshot again;
    RestoreCachedRemote(again, failed, false, false);
    assert(again.account_details == cached.account_details && again.weekly_used == 42);
    CodexSnapshot partial;
    partial.has_session = true; partial.session_used = 50; partial.account_details = L"fresh account";
    RestoreCachedRemote(partial, cached, true, false);
    assert(partial.session_used == 50 && partial.weekly_used == 42 && partial.reset_credits == 2);
    assert(partial.account_details == L"fresh account");
    CodexSnapshot recovered;
    recovered.has_session = recovered.has_weekly = true;
    recovered.session_used = 5; recovered.weekly_used = 8; recovered.reset_credits = 0;
    assert(!RestoreCachedRemote(recovered, cached, true, true));
    assert(recovered.session_used == 5 && recovered.weekly_used == 8 && recovered.reset_credits == 0);
    CodexSnapshot empty, no_cache;
    assert(!RestoreCachedRemote(empty, no_cache, false, false));
    assert(!empty.has_session && !empty.has_weekly && empty.reset_credits == -1);
    std::cout << "PASS: failure, repeated failure, partial response, recovery, no cache, local totals\n";
}
