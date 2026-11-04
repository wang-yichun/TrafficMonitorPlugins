#include <windows.h>
#include "WorkBuddyUsage.h"
#include "QoderUsagePlugin.h"
#include "JsonValue.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <vector>

namespace
{
    // Run only the installed app's own runtime. Credentials, build keys and API headers
    // stay in this short-lived child; stdout contains a whitelist of billing fields.
    // The sym-v1 field framing matches WorkBuddy's bundled at-rest implementation.
    constexpr wchar_t kQueryScript[] = LR"JS(
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const emit=value=>process.stdout.write('TM_WORKBUDDY:'+JSON.stringify(value));
async function query(){
 const filename=international?'workbuddy-desktop-ai.info':'workbuddy-desktop.info';
 const expectedDomain=international?'www.workbuddy.ai':'www.workbuddy.cn';
 const file=path.join(process.env.LOCALAPPDATA,'CodeBuddyExtension/Data/Public/auth',filename);
 if(!fs.existsSync(file)||fs.existsSync(file+'.logged-out'))return emit({error:'login'});
 if(fs.statSync(file).size>1048576)return emit({error:'credentials'});
 const session=JSON.parse(fs.readFileSync(file,'utf8'));
 const domain=session.auth?.domain;
 if(domain!==expectedDomain)return emit({error:'domain'});
 if(session.account?.enterpriseId)return emit({error:'enterprise'});
 if(session.auth.expiresAt&&Number(session.auth.expiresAt)<Date.now())return emit({error:'expired'});
 let token=session.auth.accessToken;
 if(typeof token!=='string'){
  if(token?.$wbEncrypted!==1||token.scheme)return emit({error:'credentials'});
  const build=JSON.parse(process._linkedBinding('electron_browser_workbuddy_storage').loggerGet());
  const key=crypto.createHash('sha256').update(build.atRestSecretKey).digest();
  try{
   const env=JSON.parse(Buffer.from(token.envelope,'base64'));
   const id=crypto.createHash('sha256').update(key).digest('hex').slice(0,16);
   if(env.suite!==1||env.keyId!==id)return emit({error:'credentials'});
   const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32BE(n);return b;};
   const lp=s=>Buffer.concat([u32(Buffer.byteLength(s)),Buffer.from(s)]);
   const aad=Buffer.concat([Buffer.from('WB-AAD\0'),Buffer.from([1]),lp('WBEV1'),lp('sym-v1'),u32(1),lp(id),Buffer.from([2,0,0])]);
   const nonce=Buffer.from(env.nonce,'base64'),tag=Buffer.from(env.authTag,'base64');
   if(nonce.length!==12||tag.length!==16)return emit({error:'credentials'});
   const decipher=crypto.createDecipheriv('aes-256-gcm',key,nonce);
   decipher.setAAD(aad);decipher.setAuthTag(tag);
   const plain=Buffer.concat([decipher.update(Buffer.from(env.ciphertext,'base64')),decipher.final()]);
   try{token=plain.toString('utf8');}finally{plain.fill(0);}
  }finally{key.fill(0);}
 }
 if(!token||/[\r\n]/.test(token)||typeof session.account?.uid!=='string')return emit({error:'credentials'});
 const response=await fetch('https://'+expectedDomain+'/billing/meter/get-user-resource-summary',{
  method:'POST',body:'{}',redirect:'error',signal:AbortSignal.timeout(15000),
  headers:{Authorization:'Bearer '+token,'X-User-Id':session.account.uid,'Content-Type':'application/json',
   'Accept-Language':'zh','X-Product':'workbuddy',
   'User-Agent':'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36'}
 });
 if(!response.ok)return emit({error:response.status===401?'expired':'http',http:response.status});
 const json=await response.json();
 if(json.code!==0||!json.data)return emit({error:'api'});
 const data=json.data;
 let checkin=null;
 if(!international) try {
  const status=await fetch('https://'+expectedDomain+'/v2/billing/meter/checkin-activity-status',{
   method:'POST',body:'{}',redirect:'error',signal:AbortSignal.timeout(8000),
   headers:{Authorization:'Bearer '+token,'X-User-Id':session.account.uid,'Content-Type':'application/json',
    'Accept-Language':'zh','X-Product':'workbuddy','User-Agent':'Mozilla/5.0'} });
  const result=await status.json();
  if(status.ok && result.code===0 && result.data && typeof result.data.active==='boolean' &&
     typeof result.data.today_checked_in==='boolean') {
   const d=result.data; checkin={active:d.active,today_checked_in:d.today_checked_in,
    daily_credit:d.daily_credit,today_credit:d.today_credit,streak_days:d.streak_days,
    activity_name:d.activity_name,start_time:d.start_time,end_time:d.end_time};
  }
 } catch {}
 emit({code:0,checkin,account:crypto.createHash('sha256').update(session.account.uid).digest('hex').slice(0,16),data:{
  SubscriptionPackageName:data.SubscriptionPackageName,
  Packages:Array.isArray(data.Packages)?data.Packages.map(p=>({PackageCode:p.PackageCode,
   CycleTotalCapacity:p.CycleTotalCapacity,CycleRemainCapacity:p.CycleRemainCapacity,
   CycleUsedCapacity:p.CycleUsedCapacity,CycleFrozenCapacity:p.CycleFrozenCapacity,CapacityUnit:p.CapacityUnit})):null
 }});
}
query().catch(()=>emit({error:'unavailable'}));
)JS";

    struct Handle
    {
        HANDLE value{};
        ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    };

    std::wstring Environment(const wchar_t* name)
    {
        const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
        if (!size || size > 32768) return {};
        std::wstring value(size, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), size);
        if (!written || written >= size) return {};
        value.resize(written);
        return value;
    }

    std::wstring QuoteArgument(const std::wstring& value)
    {
        std::wstring out = L"\"";
        size_t slashes = 0;
        for (wchar_t ch : value)
        {
            if (ch == L'\\') { ++slashes; continue; }
            out.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
            slashes = 0;
            if (ch == L'\"') out.push_back(L'\\');
            out.push_back(ch);
        }
        out.append(slashes * 2, L'\\');
        return out + L'\"';
    }

    bool RunQuery(const std::wstring& executable, std::wstring& result, WorkBuddyUsage::Edition edition)
    {
        SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
        Handle read, write;
        if (!CreatePipe(&read.value, &write.value, &security, 0) ||
            !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) return false;
        Handle null{ CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr) };
        if (null.value == INVALID_HANDLE_VALUE) return false;

        // Override the child environment only; the host must retain its Electron settings.
        LPWCH environment = GetEnvironmentStringsW();
        if (!environment) return false;
        std::vector<std::wstring> variables;
        for (const wchar_t* entry = environment; *entry; entry += wcslen(entry) + 1)
            if (_wcsnicmp(entry, L"ELECTRON_RUN_AS_NODE=", 21) != 0 &&
                _wcsnicmp(entry, L"NODE_OPTIONS=", 13) != 0)
                variables.emplace_back(entry);
        FreeEnvironmentStringsW(environment);
        variables.emplace_back(L"ELECTRON_RUN_AS_NODE=1");
        std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });
        std::vector<wchar_t> block;
        for (const auto& entry : variables) { block.insert(block.end(), entry.begin(), entry.end()); block.push_back(0); }
        block.push_back(0);
        const auto script = std::wstring(edition == WorkBuddyUsage::Edition::International
            ? L"const international=true;\n" : L"const international=false;\n") + kQueryScript;
        auto command = QuoteArgument(executable) + L" --eval " + QuoteArgument(script);
        SIZE_T attribute_size{};
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
        std::vector<unsigned char> attribute_storage(attribute_size);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) return false;
        HANDLE inherited[]{ write.value, null.value };
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr))
        {
            DeleteProcThreadAttributeList(attributes);
            return false;
        }
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdOutput = write.value;
        startup.StartupInfo.hStdError = startup.StartupInfo.hStdInput = null.value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION process{};
        const auto folder = std::filesystem::path(executable).parent_path().wstring();
        const BOOL started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            block.data(), folder.c_str(), &startup.StartupInfo, &process);
        DeleteProcThreadAttributeList(attributes);
        if (!started) return false;
        Handle process_handle{ process.hProcess }, thread_handle{ process.hThread };
        CloseHandle(write.value); write.value = nullptr;
        std::string bytes;
        const ULONGLONG deadline = GetTickCount64() + 25000;
        bool done = false;
        while (GetTickCount64() < deadline)
        {
            DWORD available{};
            if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr)) break;
            if (available)
            {
                if (bytes.size() + available > 1024 * 1024) break;
                const size_t offset = bytes.size();
                bytes.resize(offset + available);
                DWORD received{};
                if (!ReadFile(read.value, bytes.data() + offset, available, &received, nullptr)) break;
                bytes.resize(offset + received);
                continue;
            }
            if (WaitForSingleObject(process_handle.value, 25) == WAIT_OBJECT_0)
            {
                done = true;
                if (PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) && available) continue;
                break;
            }
        }
        // A closed pipe may arrive before the process-exit signal.
        if (!done) done = WaitForSingleObject(process_handle.value, 100) == WAIT_OBJECT_0;
        if (!done) { TerminateProcess(process_handle.value, 1); WaitForSingleObject(process_handle.value, 1000); return false; }
        DWORD exit_code{};
        if (!GetExitCodeProcess(process_handle.value, &exit_code) || exit_code != 0) return false;
        const auto prefix = bytes.find("TM_WORKBUDDY:");
        if (prefix == std::string::npos) return false;
        bytes.erase(0, prefix + 13);
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (size <= 0) return false;
        result.resize(size);
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), result.data(), size) == size;
    }

    bool ReadCount(const JsonValue* object, const wchar_t* key, double& out)
    {
        const auto value = object ? object->Get(key) : nullptr;
        if (!value) return false;
        if (value->type == JsonValue::Type::Number) out = value->number;
        else if (value->type == JsonValue::Type::String && !value->string.empty())
        {
            wchar_t* end{};
            out = wcstod(value->string.c_str(), &end);
            if (end != value->string.c_str() + value->string.size()) return false;
        }
        else return false;
        return std::isfinite(out) && out >= 0;
    }
}

std::wstring WorkBuddyUsage::ExecutablePath(Edition edition, const std::wstring& configured_path)
{
    if (!configured_path.empty())
    {
        std::error_code configured_error;
        return std::filesystem::is_regular_file(configured_path, configured_error) ? configured_path : std::wstring{};
    }

    for (const auto& folder : { Environment(L"ProgramW6432"), Environment(L"ProgramFiles"),
        Environment(L"LOCALAPPDATA") + L"\\Programs" })
    {
        if (folder.empty()) continue;
        const bool international = edition == Edition::International;
        const auto path = std::filesystem::path(folder) / (international ? L"WorkBuddyAI" : L"WorkBuddy")
            / (international ? L"WorkBuddyAI.exe" : L"WorkBuddy.exe");
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return path.wstring();
    }
    return {};
}

bool WorkBuddyUsage::ParseSummary(const std::wstring& json, QoderSnapshot& snapshot, Edition edition)
{
    JsonValue root;
    if (!ParseJson(json, root)) return false;
    const auto code = root.Get(L"code"), data = root.Get(L"data"), account = root.Get(L"account");
    if (!code || code->type != JsonValue::Type::Number || code->number != 0 || !data) return false;
    const auto packages = data->Get(L"Packages");
    if (!packages || packages->type != JsonValue::Type::Array) return false;
    QoderUsage usage;
    usage.detail_url = edition == Edition::International
        ? L"https://www.workbuddy.ai/profile/plans-usage" : L"https://www.workbuddy.cn/profile/plans-usage";
    if (const auto plan = data->Get(L"SubscriptionPackageName")) usage.user_type = plan->String();
    double total = 0, remaining = 0;
    int index = 0;
    for (const auto& package : packages->array)
    {
        const auto unit = package.Get(L"CapacityUnit");
        // The international signup grant currently reports singular "credit".
        if (!unit || (unit->String() != L"credits" && unit->String() != L"credit")) continue;
        QoderQuota quota;
        if (!ReadCount(&package, L"CycleTotalCapacity", quota.total) ||
            !ReadCount(&package, L"CycleRemainCapacity", quota.remaining)) return false;
        if (package.Get(L"CycleFrozenCapacity") && !ReadCount(&package, L"CycleFrozenCapacity", quota.reserved)) return false;
        quota.used = (std::max)(0.0, quota.total - quota.remaining);
        quota.used_percentage = quota.total > 0 ? quota.used / quota.total * 100 : 0;
        total += quota.total; remaining += quota.remaining;
        if (!std::isfinite(total) || !std::isfinite(remaining)) return false;
        const auto key = package.Get(L"PackageCode");
        usage.rows.push_back({ QoderQuotaKind::Package, key ? key->String() : L"", L"Credits " + std::to_wstring(++index), quota });
    }
    // An empty package list is a valid zero balance. Unknown units are not credits.
    if (!packages->array.empty() && usage.rows.empty()) return false;
    usage.total_usage_percentage = total > 0 ? (std::max)(0.0, (total - remaining) / total * 100) : 0;
    if (edition == Edition::China)
    {
        const auto checkin = root.Get(L"checkin");
        const auto active = checkin ? checkin->Get(L"active") : nullptr;
        const auto done = checkin ? checkin->Get(L"today_checked_in") : nullptr;
        if (active && done && active->type == JsonValue::Type::Boolean && done->type == JsonValue::Type::Boolean)
        {
            snapshot.checkin_known = true; snapshot.checkin_active = active->boolean; snapshot.checkin_done = done->boolean;
            ReadCount(checkin, L"daily_credit", snapshot.checkin_daily);
            ReadCount(checkin, L"today_credit", snapshot.checkin_today);
            ReadCount(checkin, L"streak_days", snapshot.checkin_streak);
            if (const auto v = checkin->Get(L"activity_name")) snapshot.checkin_activity = v->String();
            if (const auto v = checkin->Get(L"start_time")) snapshot.checkin_start = v->String();
            if (const auto v = checkin->Get(L"end_time")) snapshot.checkin_end = v->String();
        }
    }
    snapshot.usage = std::move(usage);
    snapshot.history_identity = edition == Edition::International ? L"workbuddy-global" : L"workbuddy-cn";
    if (account && account->type == JsonValue::Type::String && account->string.size() == 16 &&
        std::all_of(account->string.begin(), account->string.end(), [](wchar_t c) { return iswxdigit(c) != 0; }))
        snapshot.history_identity += L"-" + account->string;
    snapshot.has_data = true;
    return true;
}

void WorkBuddyUsage::Refresh(QoderSnapshot& snapshot, bool chinese, Edition edition, const std::wstring& configured_path)
{
    const bool international = edition == Edition::International;
    snapshot.region = international ? L"global" : L"cn";
    snapshot.credential_label = international
        ? (chinese ? L"WorkBuddy桌面登录（本机只读）" : L"WorkBuddy desktop sign-in (read-only)")
        : (chinese ? L"WorkBuddyCN桌面登录（本机只读）" : L"WorkBuddyCN desktop sign-in (read-only)");
    const auto executable = ExecutablePath(edition, configured_path);
    if (executable.empty())
    {
        snapshot.status = international
            ? (chinese ? L"未找到 WorkBuddy桌面应用" : L"WorkBuddy desktop app not found")
            : (chinese ? L"未找到 WorkBuddyCN桌面应用" : L"WorkBuddyCN desktop app not found");
        return;
    }
    std::wstring result;
    if (!RunQuery(executable, result, edition))
    {
        snapshot.status = chinese ? L"WorkBuddy 查询失败或超时" : L"WorkBuddy query failed or timed out";
        return;
    }
    JsonValue root;
    if (ParseJson(result, root))
    {
        const auto error = root.Get(L"error");
        if (error)
        {
            const auto code = error->String();
            if (code == L"login" || code == L"expired")
                snapshot.status = international
                    ? (chinese ? L"请打开 WorkBuddy并重新登录" : L"Open WorkBuddy and sign in again")
                    : (chinese ? L"请打开 WorkBuddyCN并重新登录" : L"Open WorkBuddyCN and sign in again");
            else if (code == L"enterprise")
                snapshot.status = chinese ? L"企业账户请在 WorkBuddy 查看积分" : L"View enterprise credits in WorkBuddy";
            else if (code == L"credentials")
                snapshot.status = chinese ? L"WorkBuddy 凭据格式不支持，请更新插件" : L"Unsupported WorkBuddy credentials; update the plugin";
            else if (code == L"domain")
                snapshot.status = chinese ? L"WorkBuddy 登录区域与所选版本不符" : L"WorkBuddy sign-in region does not match this edition";
            else
                snapshot.status = chinese ? L"WorkBuddy 积分接口请求失败" : L"WorkBuddy billing request failed";
            return;
        }
    }
    snapshot.status = ParseSummary(result, snapshot, edition)
        ? (chinese ? L"已连接" : L"Connected")
        : (chinese ? L"WorkBuddy 积分响应格式不支持" : L"Unsupported WorkBuddy billing response");
}
