/**
 * @file notifications.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

#include "system/notifications.hpp"

#include <string>

#include <windows.h>
#include <shobjidl.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include "log/log.hpp"
#include "system/winrt.hpp"

namespace {
/// The app's AppUserModelID
constexpr wchar_t kAumid[] = L"Amqx.MusicPPV2";

/// Name Windows shows as the toast's source when the app is unpackaged.
constexpr wchar_t kDisplayName[] = L"MusicPP";

/**
 * Registers @c kAumid under HKCU so an unpackaged app's toasts are attributed and displayed.
 */
void registerAumid() {
    const std::wstring subkey = std::wstring(L"Software\\Classes\\AppUserModelId\\") + kAumid;

    HKEY key = nullptr;
    if (const LSTATUS rc = RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
                                           KEY_WRITE, nullptr, &key, nullptr);
        rc != ERROR_SUCCESS) {
        logging::get("notifications")->warn("Couldn't open AUMID registry key (error {})", rc);
        return;
    }
    RegSetValueExW(key, L"DisplayName", 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(kDisplayName), sizeof(kDisplayName));
    RegCloseKey(key);
}

/// Runs the AUMID registration and process-id association exactly once per process.
void ensureRegistered() {
    static const bool once = [] {
        registerAumid();
        (void)SetCurrentProcessExplicitAppUserModelID(kAumid);
        return true;
    }();
    (void)once;
}
}

void showToastNotification(const std::string &title, const std::string &body) {
    WinRtInit::initialize();
    ensureRegistered();

    using namespace winrt::Windows::Data::Xml::Dom;
    using namespace winrt::Windows::UI::Notifications;

    try {
        // ToastText02: a bold heading (node 0) above a wrapped body line (node 1).
        const XmlDocument xml =
            ToastNotificationManager::GetTemplateContent(ToastTemplateType::ToastText02);
        const auto textNodes = xml.GetElementsByTagName(L"text");
        textNodes.Item(0).AppendChild(xml.CreateTextNode(winrt::to_hstring(title)));
        textNodes.Item(1).AppendChild(xml.CreateTextNode(winrt::to_hstring(body)));

        const ToastNotification toast{xml};
        ToastNotificationManager::CreateToastNotifier(winrt::hstring{kAumid}).Show(toast);
    } catch (const winrt::hresult_error &e) {
        logging::get("notifications")
            ->warn("Failed to show toast: {}", winrt::to_string(e.message()));
    }
}
