/**
 * @file tray.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

#include "ui/tray.hpp"

#include <mutex>
#include <utility>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "log/log.hpp"
#include "resource.h"

namespace {
constexpr UINT kTrayCallback = WM_APP + 1; ///< Shell_NotifyIcon callback message.
constexpr UINT kUpdateTip = WM_APP + 2; ///< setTooltip → apply on the UI thread.
constexpr UINT kTrayIconId = 1; ///< Our single icon's id within the window.
constexpr UINT kMenuIdBase = 1000; ///< First context-menu command id.
constexpr size_t kTipCap = 127; ///< szTip holds 128 wchars including the null.

/// Widen a UTF-8 std::string to UTF-16.
std::wstring widen(const std::string &s) {
    if (s.empty()) {
        return {};
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

/// Compose "Title — Artist" for the tooltip, truncated to the tray cap. Empty title → app name.
std::wstring formatTip(const EnrichedTrack &track) {
    const auto &id = track.track.identity;
    if (id.title.empty()) {
        return L"MusicPP";
    }
    std::wstring tip = widen(id.title);
    if (!id.artist.empty()) {
        tip += L" — " + widen(id.artist);
    }
    if (tip.size() > kTipCap) {
        tip.resize(kTipCap);
    }
    return tip;
}
}

struct Tray::Impl {
    std::wstring className;
    HWND hwnd = nullptr;
    NOTIFYICONDATAW nid{};
    std::vector<std::pair<std::wstring, std::function<void()> > > items;
    std::mutex tipMutex;
    std::wstring pendingTip;
    std::shared_ptr<spdlog::logger> log = logging::get("tray");

    explicit Impl(std::string appName);

    ~Impl();

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT handleMessage(HWND _hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void showMenu() const;

    void applyPendingTooltip();

    void addMenuItem(const std::string &label, std::function<void()> onClick);

    void setTooltip(const EnrichedTrack &track);

    static void runMessageLoop();

    void requestQuit() const;
};

Tray::Impl::Impl(std::string appName) : className(widen(appName)) {
    const HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Impl::wndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = className.c_str();
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(0, className.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hinst,
                           this);
    if (!hwnd) {
        log->error("Failed to create tray window (error {})", GetLastError());
        return;
    }

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayCallback;
    // Pull the icon out out of the embedded app-icon.ico.
    nid.hIcon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                              GetSystemMetrics(SM_CXSMICON),
                                              GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!nid.hIcon) {
        log->warn("Failed to load app icon (error {}); falling back to default", GetLastError());
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(nid.szTip, L"MusicPP");
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        log->debug("Tray icon created");
    } else {
        log->error("Failed to add tray icon");
    }
}

Tray::Impl::~Impl() {
    if (hwnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid); // Harmless if WM_DESTROY already removed it.
        DestroyWindow(hwnd);
    }
    UnregisterClassW(className.c_str(), GetModuleHandleW(nullptr));
}

LRESULT CALLBACK Tray::Impl::wndProc(const HWND hwnd, const UINT msg, const WPARAM wParam,
                                     const LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    if (auto *self = reinterpret_cast<Impl *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        return self->handleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Tray::Impl::handleMessage(const HWND _hwnd, const UINT msg, const WPARAM wParam,
                                  const LPARAM lParam) {
    switch (msg) {
    case kTrayCallback:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            showMenu();
        }
        return 0;
    case kUpdateTip:
        applyPendingTooltip();
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(_hwnd, msg, wParam, lParam);
    }
}

void Tray::Impl::showMenu() const {
    const HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < items.size(); ++i) {
        AppendMenuW(menu, MF_STRING, kMenuIdBase + i, items[i].first.c_str());
    }

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    const int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd,
                                   nullptr);
    DestroyMenu(menu);

    if (cmd >= static_cast<int>(kMenuIdBase)) {
        if (const size_t idx = static_cast<size_t>(cmd) - kMenuIdBase;
            idx < items.size() && items[idx].second) {
            items[idx].second();
        }
    }
}

void Tray::Impl::addMenuItem(const std::string &label, std::function<void()> onClick) {
    items.emplace_back(widen(label), std::move(onClick));
}

void Tray::Impl::setTooltip(const EnrichedTrack &track) { {
        std::wstring tip = formatTip(track);
        std::lock_guard lock(tipMutex);
        if (tip == pendingTip) {
            return; // Unchanged: skip the cross-thread hop.
        }
        pendingTip = std::move(tip);
    }
    PostMessageW(hwnd, kUpdateTip, 0, 0);
}

void Tray::Impl::applyPendingTooltip() {
    std::wstring tip; {
        std::lock_guard lock(tipMutex);
        tip = pendingTip;
    }
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Tray::Impl::runMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Tray::Impl::requestQuit() const {
    log->debug("Quit requested");
    PostMessageW(hwnd, WM_CLOSE, 0, 0); // DefWindowProc → DestroyWindow → WM_DESTROY.
}

Tray::Tray(std::string appName) : _impl(std::make_unique<Impl>(std::move(appName))) {
}

Tray::~Tray() = default;

void Tray::addMenuItem(std::string label, std::function<void()> onClick) const {
    _impl->addMenuItem(std::move(label), std::move(onClick));
}

void Tray::setTooltip(const EnrichedTrack &track) const { _impl->setTooltip(track); }

void Tray::runMessageLoop() const { _impl->runMessageLoop(); }

void Tray::requestQuit() const { _impl->requestQuit(); }
