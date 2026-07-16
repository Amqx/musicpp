/**
 * @file credentials.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#include "security/credentials.hpp"
#include <winrt/Windows.Security.Credentials.h>
#include "system/winrt.hpp"

using namespace winrt::Windows::Security::Credentials;

namespace {
// Vault name
const winrt::hstring kVaultUserName{L"amqx/musicppv2"};
}

std::string readCredential(const std::string &target) {
    WinRtInit::initialize();
    try {
        const PasswordVault vault;
        const auto cred = vault.Retrieve(winrt::to_hstring(target), kVaultUserName);
        cred.RetrievePassword();
        return winrt::to_string(cred.Password());
    } catch (const winrt::hresult_error &) {
        // No credential stored under this target (or the vault is inaccessible).
        return {};
    }
}

void writeCredential(const std::string &target, const std::string &secret) {
    WinRtInit::initialize();
    const PasswordVault vault;
    // Add replaces any existing credential with the same resource and username.
    vault.Add(
        PasswordCredential(winrt::to_hstring(target), kVaultUserName, winrt::to_hstring(secret)));
}

void deleteCredential(const std::string &target) {
    WinRtInit::initialize();
    try {
        const PasswordVault vault;
        const auto cred = vault.Retrieve(winrt::to_hstring(target), kVaultUserName);
        vault.Remove(cred);
    } catch (const winrt::hresult_error &) {
        // Nothing stored under this target; deleting a missing credential is a no-op.
    }
}
