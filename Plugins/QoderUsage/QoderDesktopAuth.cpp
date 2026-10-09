#include "QoderDesktopAuth.h"

#include "JsonValue.h"

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{
    constexpr std::size_t kSafeStoragePrefix = 3;   // "v10"
    constexpr std::size_t kGcmNonceLen = 12;
    constexpr std::size_t kGcmTagLen = 16;
    constexpr std::size_t kDpapiPrefix = 5;        // "DPAPI"

    struct LocalStateHandle
    {
        BCRYPT_ALG_HANDLE alg{};
        BCRYPT_KEY_HANDLE key{};
        LocalStateHandle() = default;
        ~LocalStateHandle()
        {
            if (key) BCryptDestroyKey(key);
            if (alg) BCryptCloseAlgorithmProvider(alg, 0);
        }
        LocalStateHandle(const LocalStateHandle&) = delete;
        LocalStateHandle& operator=(const LocalStateHandle&) = delete;
    };

    std::wstring ReadUtf8File(const std::wstring& path)
    {
        std::ifstream file(std::wstring(path), std::ios::binary);
        if (!file) return {};
        std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.empty() || bytes.size() > 32 * 1024 * 1024) return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0);
        if (count <= 0) return {};
        std::wstring out(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), out.data(), count);
        return out;
    }

    std::vector<std::uint8_t> Base64Decode(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1) return {};
        std::string utf8(static_cast<std::size_t>(length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), length, nullptr, nullptr);
        DWORD out_len = 4 * ((static_cast<DWORD>(utf8.size()) + 3) / 4);
        std::vector<std::uint8_t> out(out_len, 0);
        CryptStringToBinaryA(utf8.c_str(), static_cast<DWORD>(utf8.size()),
            CRYPT_STRING_BASE64, out.data(), &out_len, nullptr, nullptr);
        out.resize(out_len);
        return out;
    }

    std::wstring AppDataDir()
    {
        wchar_t value[MAX_PATH]{};
        DWORD size = GetEnvironmentVariableW(L"APPDATA", value, MAX_PATH);
        if (size == 0 || size >= MAX_PATH) return {};
        return value;
    }

    std::wstring FindFirstAppDir(const std::wstring& appdata)
    {
        const wchar_t* const names[] = { L"com.qoder.app.stable", L"com.qoder.app" };
        for (const auto* name : names)
        {
            std::wstring dir = appdata;
            dir.push_back(L'\\');
            dir.append(name);
            dir.append(L"\\Local State");
            if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES)
                return dir;
        }
        return {};
    }

    std::vector<std::uint8_t> DpapiUnprotect(const std::vector<std::uint8_t>& blob)
    {
        if (blob.empty()) return {};
        DATA_BLOB in{};
        in.pbData = const_cast<BYTE*>(blob.data());
        in.cbData = static_cast<DWORD>(blob.size());
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return {};
        std::vector<std::uint8_t> result(out.pbData, out.pbData + out.cbData);
        if (out.pbData) LocalFree(out.pbData);
        return result;
    }

    bool AesGcmDecrypt(const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& blob,
        std::vector<std::uint8_t>& plain)
    {
        if (blob.size() < kSafeStoragePrefix + kGcmNonceLen + kGcmTagLen) return false;
        if (blob[0] != 'v' || blob[1] != '1' || blob[2] != '0') return false;

        LocalStateHandle ctx;
        NTSTATUS status = BCryptOpenAlgorithmProvider(&ctx.alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status)) return false;
        status = BCryptSetProperty(ctx.alg, BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (!BCRYPT_SUCCESS(status)) return false;
        status = BCryptGenerateSymmetricKey(ctx.alg, &ctx.key, nullptr, 0,
            const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
        if (!BCRYPT_SUCCESS(status)) return false;

        const std::uint8_t* nonce = blob.data() + kSafeStoragePrefix;
        const std::uint8_t* ciphertext = nonce + kGcmNonceLen;
        const std::size_t cipher_len = blob.size() - kSafeStoragePrefix - kGcmNonceLen - kGcmTagLen;
        const std::uint8_t* tag = ciphertext + cipher_len;

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
        info.pbNonce = const_cast<PUCHAR>(nonce);
        info.cbNonce = static_cast<ULONG>(kGcmNonceLen);
        info.pbTag = const_cast<PUCHAR>(tag);
        info.cbTag = static_cast<ULONG>(kGcmTagLen);
        plain.assign(cipher_len, 0);

        // The Microsoft GCM implementation requires tag size in pbAuthInfo.
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO* pInfo = &info;
        return BCRYPT_SUCCESS(BCryptDecrypt(ctx.key, const_cast<PUCHAR>(ciphertext),
            static_cast<ULONG>(cipher_len), pInfo, nullptr, 0, plain.data(),
            static_cast<ULONG>(plain.size()), nullptr, 0));
    }

    long long ParseEpoch(const JsonValue* value)
    {
        if (value == nullptr || value->type != JsonValue::Type::Number || !std::isfinite(value->number))
            return 0;
        if (value->number <= 0.0) return 0;
        const double seconds = value->number < 1e12 ? value->number : value->number / 1000.0;
        return static_cast<long long>(seconds);
    }
}

bool QoderDesktopAuth::LoadDesktopCredential(Credential& credential)
{
    credential = {};
    const std::wstring appdata = AppDataDir();
    if (appdata.empty()) return false;
    const std::wstring local_state = FindFirstAppDir(appdata);
    if (local_state.empty()) return false;

    const std::wstring local_state_text = ReadUtf8File(local_state);
    if (local_state_text.empty()) return false;
    JsonValue local_root;
    if (!ParseJson(local_state_text, local_root)) return false;
    const JsonValue* os_crypt = local_root.Get(L"os_crypt");
    const JsonValue* encrypted_key = os_crypt ? os_crypt->Get(L"encrypted_key") : nullptr;
    if (encrypted_key == nullptr || encrypted_key->type != JsonValue::Type::String) return false;
    const std::vector<std::uint8_t> wrapped = Base64Decode(encrypted_key->string);
    if (wrapped.size() <= kDpapiPrefix) return false;
    if (wrapped[0] != 'D' || wrapped[1] != 'P' || wrapped[2] != 'A' || wrapped[3] != 'P' || wrapped[4] != 'I')
        return false;
    std::vector<std::uint8_t> aes_key(wrapped.begin() + kDpapiPrefix, wrapped.end());
    aes_key = DpapiUnprotect(aes_key);
    if (aes_key.empty()) return false;

    const std::wstring auth_path = local_state.substr(0, local_state.find_last_of(L'\\')) + L"\\auth.v1.dat";
    const std::wstring auth_text = ReadUtf8File(auth_path);
    if (auth_text.empty()) return false;

    // The auth file is stored as raw bytes; read it as bytes to preserve the GCM tag layout.
    std::ifstream auth_file(auth_path, std::ios::binary);
    if (!auth_file) return false;
    std::vector<std::uint8_t> blob((std::istreambuf_iterator<char>(auth_file)),
        std::istreambuf_iterator<char>());
    if (blob.size() < kSafeStoragePrefix + kGcmNonceLen + kGcmTagLen) return false;

    std::vector<std::uint8_t> plain;
    if (!AesGcmDecrypt(aes_key, blob, plain)) return false;

    // Zero the AES key as soon as we are done with it.
    SecureZeroMemory(aes_key.data(), aes_key.size());

    const std::string plain_utf8(plain.begin(), plain.end());
    SecureZeroMemory(plain.data(), plain.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, plain_utf8.data(),
        static_cast<int>(plain_utf8.size()), nullptr, 0);
    if (count <= 0) return false;
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, plain_utf8.data(),
        static_cast<int>(plain_utf8.size()), wide.data(), count);
    JsonValue auth_root;
    if (!ParseJson(wide, auth_root)) return false;
    const JsonValue* token_value = auth_root.Get(L"token");
    if (token_value == nullptr || token_value->type != JsonValue::Type::String) return false;
    const std::wstring token = token_value->string;
    if (token.empty()) return false;
    if (token.find_first_of(L"\r\n") != std::wstring::npos) return false;

    credential.token = token;
    credential.expires_at = ParseEpoch(auth_root.Get(L"expiresAt"));
    credential.origin = L"desktop";
    return true;
}