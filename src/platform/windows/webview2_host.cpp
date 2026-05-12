#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>

#if __has_include(<WebView2.h>) && __has_include(<wrl.h>)
#include <WebView2.h>
#include <wrl.h>
#define ZERO_NATIVE_HAS_WEBVIEW2 1
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#else
#define ZERO_NATIVE_HAS_WEBVIEW2 0
#endif

namespace {

enum EventKind {
    kStart = 0,
    kFrame = 1,
    kShutdown = 2,
    kResize = 3,
    kWindowFrame = 4,
};

struct WindowsEvent {
    int kind;
    uint64_t window_id;
    double width;
    double height;
    double scale;
    double x;
    double y;
    int open;
    int focused;
    const char *label;
    size_t label_len;
    const char *title;
    size_t title_len;
};

using EventCallback = void (*)(void *, const WindowsEvent *);
using BridgeCallback = void (*)(void *, uint64_t, const char *, size_t, const char *, size_t);

struct Window {
    uint64_t id = 1;
    HWND hwnd = nullptr;
    std::string label;
    std::string title;
    double x = 0;
    double y = 0;
    double width = 720;
    double height = 480;
};

struct Overlay {
    uint64_t window_id = 1;
    HWND hwnd = nullptr;
    std::string label;
    std::string url;
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
#if ZERO_NATIVE_HAS_WEBVIEW2
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
#endif
};

struct Host {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::string app_name;
    std::string window_title;
    std::string bundle_id;
    std::string icon_path;
    EventCallback callback = nullptr;
    void *callback_context = nullptr;
    BridgeCallback bridge_callback = nullptr;
    void *bridge_context = nullptr;
    bool running = false;
    std::map<uint64_t, Window> windows;
    std::map<std::string, Overlay> overlays;
};

static std::string slice(const char *bytes, size_t len) {
    return bytes && len > 0 ? std::string(bytes, len) : std::string();
}

static std::wstring widen(const std::string &value) {
    if (value.empty()) return std::wstring();
    int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
    std::wstring out((size_t)count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), out.data(), count);
    return out;
}

static size_t boundedLen(const char *text, size_t limit) {
    size_t len = 0;
    while (len < limit && text[len] != '\0') ++len;
    return len;
}

static void emit(Host *host, const Window &window, EventKind kind) {
    if (!host || !host->callback) return;
    RECT rect = {};
    if (window.hwnd) GetClientRect(window.hwnd, &rect);
    WindowsEvent event = {};
    event.kind = kind;
    event.window_id = window.id;
    event.width = rect.right > rect.left ? (double)(rect.right - rect.left) : window.width;
    event.height = rect.bottom > rect.top ? (double)(rect.bottom - rect.top) : window.height;
    event.scale = 1.0;
    event.x = window.x;
    event.y = window.y;
    event.open = window.hwnd != nullptr;
    event.focused = window.hwnd && GetFocus() == window.hwnd;
    event.label = window.label.c_str();
    event.label_len = window.label.size();
    event.title = window.title.c_str();
    event.title_len = window.title.size();
    host->callback(host->callback_context, &event);
}

static std::string overlayKey(uint64_t window_id, const std::string &label) {
    return std::to_string(window_id) + ":" + label;
}

static int overlayCoord(double value) {
    return value > 0 ? (int)(value + 0.5) : 0;
}

static int overlayExtent(double value) {
    return value > 1 ? (int)(value + 0.5) : 1;
}

static bool validOverlayFrame(double x, double y, double width, double height) {
    return x >= 0 && y >= 0 && width > 0 && height > 0;
}

static void destroyOverlaysForWindow(Host *host, uint64_t window_id) {
    if (!host) return;
    for (auto it = host->overlays.begin(); it != host->overlays.end();) {
        if (it->second.window_id == window_id) {
#if ZERO_NATIVE_HAS_WEBVIEW2
            if (it->second.controller) it->second.controller->Close();
#endif
            if (it->second.hwnd) DestroyWindow(it->second.hwnd);
            it = host->overlays.erase(it);
        } else {
            ++it;
        }
    }
}

#if ZERO_NATIVE_HAS_WEBVIEW2
using CreateEnvironmentFn = HRESULT (STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

static RECT overlayRect(const Overlay &overlay) {
    RECT rect = {};
    rect.left = 0;
    rect.top = 0;
    rect.right = overlayExtent(overlay.width);
    rect.bottom = overlayExtent(overlay.height);
    return rect;
}

static CreateEnvironmentFn webView2Factory() {
    static HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    if (!loader) return nullptr;
    return reinterpret_cast<CreateEnvironmentFn>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"));
}

static void createOverlayWebView(Host *host, const std::string &key) {
    auto factory = webView2Factory();
    if (!factory) return;
    auto found = host->overlays.find(key);
    if (found == host->overlays.end() || !found->second.hwnd) return;
    HWND parent = found->second.hwnd;
    std::wstring initial_url = widen(found->second.url);
    factory(nullptr, nullptr, nullptr, Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [host, key, parent, initial_url](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT {
            if (FAILED(result) || !environment) return result;
            return environment->CreateCoreWebView2Controller(parent, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [host, key, initial_url](HRESULT controller_result, ICoreWebView2Controller *controller) -> HRESULT {
                    if (FAILED(controller_result) || !controller) return controller_result;
                    auto found = host->overlays.find(key);
                    if (found == host->overlays.end()) {
                        controller->Close();
                        return S_OK;
                    }
                    found->second.controller = controller;
                    controller->get_CoreWebView2(&found->second.webview);
                    RECT bounds = overlayRect(found->second);
                    controller->put_Bounds(bounds);
                    controller->put_IsVisible(TRUE);
                    if (found->second.webview && !initial_url.empty()) {
                        found->second.webview->Navigate(initial_url.c_str());
                    }
                    return S_OK;
                }).Get());
        }).Get());
}
#endif

static Host *hostFromWindow(HWND hwnd) {
    return reinterpret_cast<Host *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
        auto *host = reinterpret_cast<Host *>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    Host *host = hostFromWindow(hwnd);
    switch (message) {
        case WM_SIZE:
            if (host) {
                for (auto &entry : host->windows) {
                    if (entry.second.hwnd == hwnd) emit(host, entry.second, kResize);
                }
            }
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_MOVE:
            if (host) {
                for (auto &entry : host->windows) {
                    if (entry.second.hwnd == hwnd) emit(host, entry.second, kWindowFrame);
                }
            }
            return 0;
        case WM_TIMER:
            if (host) {
                for (auto &entry : host->windows) emit(host, entry.second, kFrame);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (host) {
                for (auto &entry : host->windows) {
                    if (entry.second.hwnd == hwnd) {
                        destroyOverlaysForWindow(host, entry.first);
                        entry.second.hwnd = nullptr;
                        emit(host, entry.second, kWindowFrame);
                    }
                }
                bool any_open = false;
                for (auto &entry : host->windows) any_open = any_open || entry.second.hwnd;
                if (!any_open) PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static ATOM registerClass(Host *host) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = host->instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ZeroNativeWindowsHost";
    return RegisterClassExW(&wc);
}

static bool createNativeWindow(Host *host, Window &window) {
    registerClass(host);
    std::wstring title = widen(window.title.empty() ? host->window_title : window.title);
    HWND hwnd = CreateWindowExW(
        0,
        L"ZeroNativeWindowsHost",
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        (int)window.width,
        (int)window.height,
        nullptr,
        nullptr,
        host->instance,
        host);
    if (!hwnd) return false;
    window.hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 16, nullptr);
    return true;
}

} // namespace

extern "C" {

void zero_native_windows_load_window_webview(Host *host, uint64_t window_id, const char *source, size_t source_len, int source_kind, const char *asset_root, size_t asset_root_len, const char *asset_entry, size_t asset_entry_len, const char *asset_origin, size_t asset_origin_len, int spa_fallback);
void zero_native_windows_bridge_respond_window(Host *host, uint64_t window_id, const char *response, size_t response_len);

Host *zero_native_windows_create(const char *app_name, size_t app_name_len, const char *window_title, size_t window_title_len, const char *bundle_id, size_t bundle_id_len, const char *icon_path, size_t icon_path_len, const char *window_label, size_t window_label_len, double x, double y, double width, double height, int restore_frame) {
    (void)restore_frame;
    Host *host = new Host();
    host->app_name = slice(app_name, app_name_len);
    host->window_title = slice(window_title, window_title_len);
    host->bundle_id = slice(bundle_id, bundle_id_len);
    host->icon_path = slice(icon_path, icon_path_len);
    Window window;
    window.id = 1;
    window.label = slice(window_label, window_label_len);
    window.title = host->window_title.empty() ? host->app_name : host->window_title;
    window.x = x;
    window.y = y;
    window.width = width;
    window.height = height;
    host->windows[window.id] = window;
    return host;
}

void zero_native_windows_destroy(Host *host) {
    if (!host) return;
    delete host;
}

void zero_native_windows_run(Host *host, EventCallback callback, void *context) {
    if (!host) return;
    host->callback = callback;
    host->callback_context = context;
    host->running = true;
    if (!host->windows.empty()) createNativeWindow(host, host->windows.begin()->second);
    WindowsEvent start = {};
    start.kind = kStart;
    start.window_id = 1;
    callback(context, &start);
    for (auto &entry : host->windows) {
        emit(host, entry.second, kResize);
        emit(host, entry.second, kWindowFrame);
    }
    MSG message = {};
    while (host->running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    WindowsEvent shutdown = {};
    shutdown.kind = kShutdown;
    shutdown.window_id = 1;
    callback(context, &shutdown);
}

void zero_native_windows_stop(Host *host) {
    if (!host) return;
    host->running = false;
    PostQuitMessage(0);
}

void zero_native_windows_load_webview(Host *host, const char *source, size_t source_len, int source_kind, const char *asset_root, size_t asset_root_len, const char *asset_entry, size_t asset_entry_len, const char *asset_origin, size_t asset_origin_len, int spa_fallback) {
    zero_native_windows_load_window_webview(host, 1, source, source_len, source_kind, asset_root, asset_root_len, asset_entry, asset_entry_len, asset_origin, asset_origin_len, spa_fallback);
}

void zero_native_windows_load_window_webview(Host *host, uint64_t window_id, const char *source, size_t source_len, int source_kind, const char *asset_root, size_t asset_root_len, const char *asset_entry, size_t asset_entry_len, const char *asset_origin, size_t asset_origin_len, int spa_fallback) {
    (void)source;
    (void)source_len;
    (void)source_kind;
    (void)asset_root;
    (void)asset_root_len;
    (void)asset_entry;
    (void)asset_entry_len;
    (void)asset_origin;
    (void)asset_origin_len;
    (void)spa_fallback;
    if (!host) return;
    auto found = host->windows.find(window_id);
    if (found != host->windows.end()) emit(host, found->second, kWindowFrame);
}

void zero_native_windows_set_bridge_callback(Host *host, BridgeCallback callback, void *context) {
    if (!host) return;
    host->bridge_callback = callback;
    host->bridge_context = context;
}

void zero_native_windows_bridge_respond(Host *host, const char *response, size_t response_len) {
    zero_native_windows_bridge_respond_window(host, 1, response, response_len);
}

void zero_native_windows_bridge_respond_window(Host *host, uint64_t window_id, const char *response, size_t response_len) {
    (void)host;
    (void)window_id;
    (void)response;
    (void)response_len;
}

void zero_native_windows_emit_window_event(Host *host, uint64_t window_id, const char *name, size_t name_len, const char *detail_json, size_t detail_json_len) {
    (void)host;
    (void)window_id;
    (void)name;
    (void)name_len;
    (void)detail_json;
    (void)detail_json_len;
}

void zero_native_windows_set_security_policy(Host *host, const char *allowed_origins, size_t allowed_origins_len, const char *external_urls, size_t external_urls_len, int external_action) {
    (void)host;
    (void)allowed_origins;
    (void)allowed_origins_len;
    (void)external_urls;
    (void)external_urls_len;
    (void)external_action;
}

int zero_native_windows_create_window(Host *host, uint64_t window_id, const char *window_title, size_t window_title_len, const char *window_label, size_t window_label_len, double x, double y, double width, double height, int restore_frame) {
    (void)restore_frame;
    if (!host || host->windows.find(window_id) != host->windows.end()) return 0;
    Window window;
    window.id = window_id;
    window.title = slice(window_title, window_title_len);
    window.label = slice(window_label, window_label_len);
    window.x = x;
    window.y = y;
    window.width = width;
    window.height = height;
    bool ok = createNativeWindow(host, window);
    host->windows[window_id] = window;
    return ok ? 1 : 0;
}

int zero_native_windows_focus_window(Host *host, uint64_t window_id) {
    if (!host) return 0;
    auto found = host->windows.find(window_id);
    if (found == host->windows.end() || !found->second.hwnd) return 0;
    SetForegroundWindow(found->second.hwnd);
    SetFocus(found->second.hwnd);
    return 1;
}

int zero_native_windows_close_window(Host *host, uint64_t window_id) {
    if (!host) return 0;
    auto found = host->windows.find(window_id);
    if (found == host->windows.end() || !found->second.hwnd) return 0;
    destroyOverlaysForWindow(host, window_id);
    DestroyWindow(found->second.hwnd);
    return 1;
}

int zero_native_windows_create_overlay(Host *host, uint64_t window_id, const char *label, size_t label_len, const char *url, size_t url_len, double x, double y, double width, double height) {
    if (!host || label_len == 0 || url_len == 0 || !validOverlayFrame(x, y, width, height)) return 0;
    auto window = host->windows.find(window_id);
    if (window == host->windows.end() || !window->second.hwnd) return 0;
    std::string label_string = slice(label, label_len);
    std::string key = overlayKey(window_id, label_string);
    if (host->overlays.find(key) != host->overlays.end()) return 0;

    std::string url_string = slice(url, url_len);
    HWND hwnd = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        overlayCoord(x),
        overlayCoord(y),
        overlayExtent(width),
        overlayExtent(height),
        window->second.hwnd,
        nullptr,
        host->instance,
        nullptr);
    if (!hwnd) return 0;

    Overlay overlay;
    overlay.window_id = window_id;
    overlay.hwnd = hwnd;
    overlay.label = label_string;
    overlay.url = url_string;
    overlay.x = x;
    overlay.y = y;
    overlay.width = width;
    overlay.height = height;
    host->overlays[key] = overlay;
#if ZERO_NATIVE_HAS_WEBVIEW2
    createOverlayWebView(host, key);
#else
    std::wstring title = widen(url_string);
    SetWindowTextW(hwnd, title.c_str());
#endif
    return 1;
}

int zero_native_windows_set_overlay_frame(Host *host, uint64_t window_id, const char *label, size_t label_len, double x, double y, double width, double height) {
    if (!host || label_len == 0 || !validOverlayFrame(x, y, width, height)) return 0;
    auto found = host->overlays.find(overlayKey(window_id, slice(label, label_len)));
    if (found == host->overlays.end() || !found->second.hwnd) return 0;
    found->second.x = x;
    found->second.y = y;
    found->second.width = width;
    found->second.height = height;
    MoveWindow(found->second.hwnd, overlayCoord(x), overlayCoord(y), overlayExtent(width), overlayExtent(height), TRUE);
#if ZERO_NATIVE_HAS_WEBVIEW2
    if (found->second.controller) {
        RECT bounds = overlayRect(found->second);
        found->second.controller->put_Bounds(bounds);
    }
#endif
    return 1;
}

int zero_native_windows_navigate_overlay(Host *host, uint64_t window_id, const char *label, size_t label_len, const char *url, size_t url_len) {
    if (!host || label_len == 0 || url_len == 0) return 0;
    auto found = host->overlays.find(overlayKey(window_id, slice(label, label_len)));
    if (found == host->overlays.end() || !found->second.hwnd) return 0;
    found->second.url = slice(url, url_len);
#if ZERO_NATIVE_HAS_WEBVIEW2
    if (found->second.webview) {
        std::wstring target = widen(found->second.url);
        found->second.webview->Navigate(target.c_str());
        return 1;
    }
#endif
    std::wstring title = widen(found->second.url);
    SetWindowTextW(found->second.hwnd, title.c_str());
    return 1;
}

int zero_native_windows_close_overlay(Host *host, uint64_t window_id, const char *label, size_t label_len) {
    if (!host || label_len == 0) return 0;
    auto found = host->overlays.find(overlayKey(window_id, slice(label, label_len)));
    if (found == host->overlays.end()) return 0;
#if ZERO_NATIVE_HAS_WEBVIEW2
    if (found->second.controller) found->second.controller->Close();
#endif
    if (found->second.hwnd) DestroyWindow(found->second.hwnd);
    host->overlays.erase(found);
    return 1;
}

size_t zero_native_windows_clipboard_read(Host *host, char *buffer, size_t buffer_len) {
    (void)host;
    if (!buffer || buffer_len == 0 || !OpenClipboard(nullptr)) return 0;
    HANDLE handle = GetClipboardData(CF_TEXT);
    if (!handle) {
        CloseClipboard();
        return 0;
    }
    const char *text = static_cast<const char *>(GlobalLock(handle));
    if (!text) {
        CloseClipboard();
        return 0;
    }
    size_t len = boundedLen(text, buffer_len);
    memcpy(buffer, text, len);
    GlobalUnlock(handle);
    CloseClipboard();
    return len;
}

void zero_native_windows_clipboard_write(Host *host, const char *text, size_t text_len) {
    (void)host;
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, text_len + 1);
    if (handle) {
        char *dest = static_cast<char *>(GlobalLock(handle));
        memcpy(dest, text, text_len);
        dest[text_len] = '\0';
        GlobalUnlock(handle);
        SetClipboardData(CF_TEXT, handle);
    }
    CloseClipboard();
}

}
