#pragma once

#include <string>

namespace QoderDesktopAuth
{
    enum class Edition { International, China };
    struct Credential
    {
        std::wstring token;
        long long expires_at{};  // 0 if unknown
        std::wstring origin;     // human-readable origin label, never contains the token
    };

    // Decrypt the Qoder desktop app's auth.v1.dat using the user's DPAPI key.
    // Returns false when credentials cannot be located or decrypted.
    bool LoadDesktopCredential(Credential& credential, Edition edition = Edition::International);
    std::wstring ExecutablePath(Edition edition = Edition::International,
        const std::wstring& configured_path = {});
}
