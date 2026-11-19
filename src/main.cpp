#include "platform.h"
#include "registry.h"
#include "resource.h"
#include "ui_text.h"

#include <algorithm>
#include <commctrl.h>
#include <gdiplus.h>
#include <process.h>
#include <shellapi.h>
#include <string>
#include <string_view>

#define STRINGIFY_IMPL(value) #value
#define STRINGIFY(value) STRINGIFY_IMPL(value)
#define WIDEN_IMPL(value) L##value
#define WIDEN(value) WIDEN_IMPL(value)

namespace {

constexpr wchar_t appName[] = L"Context Menu Toggler";
constexpr wchar_t version[] = L"v" WIDEN(STRINGIFY(APP_MAJOR)) L"." WIDEN(STRINGIFY(APP_MINOR))
                              L"." WIDEN(STRINGIFY(APP_PATCH));
constexpr wchar_t windowClass[] = L"ContextMenuToggler.MainWindow.SingleInstance";
constexpr wchar_t authorUrl[] = L"https://github.com/Kimtroll84";
constexpr wchar_t releasesUrl[] = L"https://github.com/Kimtroll84/ContextMenuToggler/releases";
constexpr DWORD windowStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
constexpr DWORD extendedStyle = WS_EX_CONTROLPARENT;
constexpr UINT restartFinished = WM_APP + 1;
constexpr int clientWidth = 440;
constexpr int clientHeight = 246;
constexpr int panelMargin = 16;
constexpr int panelWidth = clientWidth - 2 * panelMargin;

enum ControlId { Portrait = 200, Speech, Apply, Restore, Creator, Releases };
enum PortraitIndex { CheckImage, ClassicImage, DefaultImage, AuthorImage, IncompleteImage, PortraitCount };

struct App {
    HINSTANCE instance;
    const Text& text;
    menu::Recipe recipe = menu::DetectRecipe();
    menu::State state = menu::State::Check;
    HWND window = nullptr;
    HWND portrait = nullptr;
    HWND speech = nullptr;
    HWND apply = nullptr;
    HWND restore = nullptr;
    HWND creator = nullptr;
    HWND releases = nullptr;
    HFONT font = nullptr;
    std::array<HBITMAP, PortraitCount> images{};
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool keyboardFocus = false;
    bool showingAuthor = false;
    bool busy = false;

    ~App() {
        if (font) DeleteObject(font);
        for (auto image : images) if (image) DeleteObject(image);
    }

    int Scale(int value) const { return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI); }

    HWND Control(const wchar_t* type, const wchar_t* label, DWORD style, ControlId id) const {
        return CreateWindowExW(0, type, label, WS_CHILD | style, 0, 0, 0, 0, window,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    }

    void Position(HWND control, int x, int y, int width, int height) const {
        MoveWindow(control, Scale(x), Scale(y), Scale(x + width) - Scale(x), Scale(y + height) - Scale(y), TRUE);
    }

    bool CreateControls() {
        portrait = Control(WC_BUTTONW, text.author, WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, Portrait);
        speech = Control(WC_STATICW, L"", WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_NOPREFIX, Speech);
        const std::wstring name = std::wstring(text.author) + L" <a>Troll_Kim</a>";
        const std::wstring link = std::wstring(version) + L" · <a>" + text.releases + L"</a>";
        creator = Control(WC_LINK, name.c_str(), WS_TABSTOP, Creator);
        releases = Control(WC_LINK, link.c_str(), WS_TABSTOP, Releases);
        apply = Control(WC_BUTTONW, text.apply, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, Apply);
        restore = Control(WC_BUTTONW, text.restore, WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, Restore);
        if (!portrait || !speech || !creator || !releases || !apply || !restore) return false;

        if (!ReloadPortraits()) return false;
        state = menu::Inspect(HKEY_CURRENT_USER, menu::userPaths, recipe);
        Layout();
        Refresh();
        return true;
    }

    bool ReloadPortraits() {
        constexpr int ids[] = {PORTRAIT_CHECK, PORTRAIT_CLASSIC, PORTRAIT_DEFAULT, PORTRAIT_AUTHOR,
                               PORTRAIT_INCOMPLETE};
        static_assert(ARRAYSIZE(ids) == PortraitCount);
        std::array<HBITMAP, PortraitCount> resized{};
        for (size_t i = 0; i < resized.size(); ++i) {
            resized[i] = LoadPortrait(instance, ids[i], Scale(116));
            if (!resized[i]) {
                for (const auto image : resized) if (image) DeleteObject(image);
                return false;
            }
        }
        images.swap(resized);
        for (const auto image : resized) if (image) DeleteObject(image);
        return true;
    }

    void Layout() {
        const HFONT oldFont = font;
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
            if (-metrics.lfMessageFont.lfHeight < Scale(16)) metrics.lfMessageFont.lfHeight = -Scale(16);
            font = CreateFontIndirectW(&metrics.lfMessageFont);
        }
        if (!font) font = oldFont;
        for (HWND item : {portrait, speech, creator, releases, apply, restore}) {
            SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        }
        if (oldFont && oldFont != font) DeleteObject(oldFont);
        Position(portrait, panelMargin + 3, 19, 116, 116);
        Position(creator, 157, 55, 247, 20);
        Position(releases, 157, 77, 247, 20);
        Position(apply, panelMargin, 150, panelWidth, 36);
        Position(restore, panelMargin, 194, panelWidth, 36);
    }

    void SetSpeech(std::wstring_view line) const {
        // Insert line breaks only between words. The static control must not split Korean syllables.
        const HDC dc = GetDC(window);
        if (!dc) return;
        const HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
        const int width = Scale(247);
        std::wstring wrapped;
        size_t lineStart = 0;
        for (size_t wordStart = 0; wordStart < line.size();) {
            size_t wordEnd = line.find_first_of(L" \n", wordStart);
            if (wordEnd == std::wstring_view::npos) wordEnd = line.size();
            SIZE size{};
            GetTextExtentPoint32W(dc, line.data() + lineStart,
                                   static_cast<int>(wordEnd - lineStart), &size);
            if (wordStart != lineStart && size.cx > width) {
                wrapped.replace(wrapped.size() - 1, 1, L"\r\n");
                lineStart = wordStart;
            }
            wrapped.append(line.substr(wordStart, wordEnd - wordStart));
            if (wordEnd < line.size()) {
                if (line[wordEnd] == L'\n') {
                    wrapped += L"\r\n";
                    lineStart = wordEnd + 1;
                } else {
                    wrapped.push_back(L' ');
                }
            }
            wordStart = wordEnd + 1;
        }
        TEXTMETRICW metrics{};
        GetTextMetricsW(dc, &metrics);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(window, dc);

        int rows = 1;
        for (wchar_t ch : wrapped) if (ch == L'\n') ++rows;
        const int height = (metrics.tmHeight + metrics.tmExternalLeading) * rows;
        MoveWindow(speech, Scale(157), Scale(75) - height / 2, width, height + 2, TRUE);
        SetWindowTextW(speech, wrapped.c_str());
    }

    void Refresh() const {
        const bool canApply = state == menu::State::Default || state == menu::State::Incomplete;
        const bool canRestore = state == menu::State::Classic || state == menu::State::Incomplete;
        EnableWindow(apply, !busy && canApply);
        EnableWindow(restore, !busy && canRestore);
        EnableWindow(portrait, !busy);
        ShowWindow(speech, showingAuthor ? SW_HIDE : SW_SHOW);
        ShowWindow(creator, showingAuthor ? SW_SHOW : SW_HIDE);
        ShowWindow(releases, showingAuthor ? SW_SHOW : SW_HIDE);
        SetWindowTextW(portrait, showingAuthor ? text.showStatus : text.author);
        SetSpeech(text.status[static_cast<size_t>(state)]);
        InvalidateRect(portrait, nullptr, FALSE);
        InvalidateRect(window, nullptr, FALSE);
    }

    HBITMAP PortraitBitmap() const {
        if (showingAuthor) return images[AuthorImage];
        if (state == menu::State::Classic) return images[ClassicImage];
        if (state == menu::State::Default) return images[DefaultImage];
        if (state == menu::State::Incomplete) return images[IncompleteImage];
        return images[CheckImage];
    }
};


Gdiplus::Color SystemColor(int index) {
    Gdiplus::Color color;
    color.SetFromCOLORREF(GetSysColor(index));
    return color;
}

void PaintWindow(const App& app) {
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(app.window, &paint);
    RECT client{};
    GetClientRect(app.window, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    {
        Gdiplus::Graphics canvas(dc);
        canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush paper(SystemColor(COLOR_WINDOW));
        Gdiplus::Pen stroke(SystemColor(COLOR_3DSHADOW), 1.0f);
        const auto px = [&app](int value) { return static_cast<float>(app.Scale(value)); };

        const float x = px(panelMargin) + 0.5f;
        const float y = px(16) + 0.5f;
        const float endX = px(panelMargin + panelWidth) - 0.5f;
        const float endY = px(138) - 0.5f;
        const float diameter = px(12);
        Gdiplus::GraphicsPath panel;
        panel.AddArc(x, y, diameter, diameter, 180, 90);
        panel.AddArc(endX - diameter, y, diameter, diameter, 270, 90);
        panel.AddArc(endX - diameter, endY - diameter, diameter, diameter, 0, 90);
        panel.AddArc(x, endY - diameter, diameter, diameter, 90, 90);
        panel.CloseFigure();
        canvas.FillPath(&paper, &panel);
        canvas.DrawPath(&stroke, &panel);

        const float left = px(145);
        const float right = px(415);
        const float top = px(26);
        const float bottom = px(124);
        const float curve = px(12);
        Gdiplus::GraphicsPath bubble;
        bubble.AddArc(left, top, curve, curve, 180.0f, 90.0f);
        bubble.AddArc(right - curve, top, curve, curve, 270.0f, 90.0f);
        bubble.AddArc(right - curve, bottom - curve, curve, curve, 0.0f, 90.0f);
        bubble.AddArc(left, bottom - curve, curve, curve, 90.0f, 90.0f);
        bubble.AddLine(left, px(83), px(137), px(75));
        bubble.AddLine(px(137), px(75), left, px(67));
        bubble.CloseFigure();
        canvas.FillPath(&paper, &bubble);
        canvas.DrawPath(&stroke, &bubble);
    }
    EndPaint(app.window, &paint);
}


void PaintPortrait(const App& app, const DRAWITEMSTRUCT& draw) {
    RECT rect = draw.rcItem;
    FillRect(draw.hDC, &rect, GetSysColorBrush(COLOR_WINDOW));
    const HBITMAP image = app.PortraitBitmap();
    BITMAP bitmap{};
    const HDC source = CreateCompatibleDC(draw.hDC);
    if (source && GetObjectW(image, sizeof(bitmap), &bitmap)) {
        const HGDIOBJ oldImage = SelectObject(source, image);
        constexpr BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        AlphaBlend(draw.hDC, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                   source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, blend);
        SelectObject(source, oldImage);
    }
    if (source) DeleteDC(source);
    if (app.keyboardFocus && (draw.itemState & ODS_FOCUS)) {
        InflateRect(&rect, -2, -2);
        DrawFocusRect(draw.hDC, &rect);
    }
}

void ShowError(const App& app, const wchar_t* message, DWORD error) {
    std::wstring content = message;
    if (error != ERROR_SUCCESS) {
        content += L"\r\n\r\n";
        content += std::to_wstring(error);
    }
    MessageBoxW(app.window, content.c_str(), appName, MB_OK | MB_ICONERROR);
}

INT_PTR CALLBACK RestartDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        const App& app = *reinterpret_cast<const App*>(lParam);
        SetWindowTextW(dialog, app.text.restartTitle);
        SetDlgItemTextW(dialog, RESTART_PROMPT, app.text.restartQuestion);
        SetDlgItemTextW(dialog, IDYES, app.text.restartNow);
        SetDlgItemTextW(dialog, IDCANCEL, app.text.later);

        RECT units{0, 0, 4, 8};
        MapDialogRect(dialog, &units);
        const int margin = MulDiv(7, units.right, 4);
        const HWND prompt = GetDlgItem(dialog, RESTART_PROMPT);
        const HDC dc = GetDC(prompt);
        const auto font = reinterpret_cast<HFONT>(SendMessageW(prompt, WM_GETFONT, 0, 0));
        const auto previous = SelectObject(dc, font);
        RECT text{};
        DrawTextW(dc, app.text.restartQuestion, -1, &text, DT_CALCRECT | DT_NOPREFIX);
        RECT yes{}, later{};
        DrawTextW(dc, app.text.restartNow, -1, &yes, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        DrawTextW(dc, app.text.later, -1, &later, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        ReleaseDC(prompt, dc);

        const int buttonWidth = (std::max)(LONG(MulDiv(42, units.right, 4)), (std::max)(yes.right, later.right) + 2 * margin);
        const int gap = units.right;
        const int contentWidth = (std::max)(text.right + 2, LONG(2 * buttonWidth + gap));
        const int buttonY = margin + text.bottom + margin;
        const int buttonHeight = (std::max)(LONG(MulDiv(14, units.bottom, 8)), yes.bottom + gap);
        MoveWindow(prompt, margin, margin, contentWidth, text.bottom, FALSE);
        MoveWindow(GetDlgItem(dialog, IDYES), margin + contentWidth - 2 * buttonWidth - gap,
                   buttonY, buttonWidth, buttonHeight, FALSE);
        MoveWindow(GetDlgItem(dialog, IDCANCEL), margin + contentWidth - buttonWidth,
                   buttonY, buttonWidth, buttonHeight, FALSE);
        RECT bounds{0, 0, contentWidth + 2 * margin, buttonY + buttonHeight + margin};
        AdjustWindowRectExForDpi(&bounds, GetWindowLongW(dialog, GWL_STYLE), FALSE,
                                 GetWindowLongW(dialog, GWL_EXSTYLE), GetDpiForWindow(dialog));
        RECT owner{};
        GetWindowRect(app.window, &owner);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST), &monitor);
        const int x = (std::max)(monitor.rcWork.left, (std::min)((owner.left + owner.right - width) / 2,
                                                  monitor.rcWork.right - width));
        const int y = (std::max)(monitor.rcWork.top, (std::min)((owner.top + owner.bottom - height) / 2,
                                                 monitor.rcWork.bottom - height));
        SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER);
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDYES || LOWORD(wParam) == IDCANCEL)) {
        EndDialog(dialog, LOWORD(wParam));
        return TRUE;
    }
    return FALSE;
}

bool ConfirmRestart(const App& app) {
    return DialogBoxParamW(app.instance, MAKEINTRESOURCEW(DIALOG_RESTART), app.window,
                           RestartDialogProc, reinterpret_cast<LPARAM>(&app)) == IDYES;
}
unsigned __stdcall RestartWorker(void* parameter) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const DWORD error = RestartExplorer();
    if (SUCCEEDED(com)) CoUninitialize();
    PostMessageW(static_cast<HWND>(parameter), restartFinished, error, 0);
    return 0;
}

void ChangeSetting(App& app, bool classic) {
    const HWND button = classic ? app.apply : app.restore;
    if (app.busy || !IsWindowEnabled(button)) return;
    app.showingAuthor = false;
    app.busy = true;
    app.Refresh();

    const auto result = menu::Change(HKEY_CURRENT_USER, menu::userPaths, app.recipe, classic);
    app.state = menu::Inspect(HKEY_CURRENT_USER, menu::userPaths, app.recipe);
    const auto target = classic ? menu::State::Classic : menu::State::Default;
    app.Refresh();
    if (result.error != ERROR_SUCCESS || app.state != target) {
        ShowError(app, app.text.changeError, result.error ? result.error : ERROR_RETRY);
    } else if (result.changed && ConfirmRestart(app)) {
        const auto worker = _beginthreadex(nullptr, 0, RestartWorker, app.window, 0, nullptr);
        if (worker) {
            CloseHandle(reinterpret_cast<HANDLE>(worker));
            return;
        }
        ShowError(app, app.text.restartError, ERROR_NOT_ENOUGH_MEMORY);
    }
    app.busy = false;
    app.Refresh();
    SetFocus(classic && IsWindowEnabled(app.restore) ? app.restore
             : !classic && IsWindowEnabled(app.apply) ? app.apply : app.portrait);
}

void OpenLink(const App& app, const wchar_t* url) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.hwnd = app.window;
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpFile = url;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) ShowError(app, app.text.linkError, GetLastError());
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        app->dpi = GetDpiForWindow(window);
        return app->CreateControls() ? 0 : -1;
    case WM_DPICHANGED: {
        app->dpi = HIWORD(wParam);
        const auto& suggested = *reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                     suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
        app->ReloadPortraits();
        app->Layout();
        app->Refresh();
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && !app->busy && app->apply) {
            app->state = menu::Inspect(HKEY_CURRENT_USER, menu::userPaths, app->recipe);
            app->Refresh();
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintWindow(*app);
        return 0;
    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_DRAWITEM:
        if (wParam == Portrait) {
            PaintPortrait(*app, *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        }
        break;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wParam) == app->portrait && !app->busy) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (!app->busy && HIWORD(wParam) == BN_CLICKED) {
            switch (LOWORD(wParam)) {
            case Portrait:
                app->showingAuthor = !app->showingAuthor;
                app->Refresh();
                return 0;
            case Apply:
                ChangeSetting(*app, true);
                return 0;
            case Restore:
                ChangeSetting(*app, false);
                return 0;
            }
        }
        break;
    case WM_NOTIFY: {
        const auto& event = *reinterpret_cast<const NMHDR*>(lParam);
        if (app->showingAuthor && (event.code == NM_CLICK || event.code == NM_RETURN)) {
            if (event.idFrom == Creator) OpenLink(*app, authorUrl);
            if (event.idFrom == Releases) OpenLink(*app, releasesUrl);
        }
        break;
    }
    case restartFinished:
        app->busy = false;
        app->Refresh();
        if (wParam != ERROR_SUCCESS) ShowError(*app, app->text.restartError, static_cast<DWORD>(wParam));
        return 0;
    case WM_CLOSE:
        if (app->busy) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    struct InstanceGuard {
        HANDLE handle;
        ~InstanceGuard() { if (handle) CloseHandle(handle); }
    };
    const bool isKorean = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
    InstanceGuard guard{CreateMutexW(nullptr, FALSE, L"Local\\Kimtroll84.ContextMenuToggler")};
    const DWORD mutexStatus = GetLastError();
    if (!guard.handle) {
        MessageBoxW(nullptr, isKorean ? L"중복 실행 여부를 확인하지 못했습니다."
                                      : L"Could not check for another instance.",
                    appName, MB_OK | MB_ICONERROR);
        return 1;
    }
    if (mutexStatus == ERROR_ALREADY_EXISTS) {
        for (int attempt = 0; attempt < 80; ++attempt) {
            if (HWND existing = FindWindowW(windowClass, nullptr)) {
                if (IsIconic(existing)) ShowWindowAsync(existing, SW_RESTORE);
                SetForegroundWindow(GetLastActivePopup(existing));
                break;
            }
            Sleep(25);
        }
        return 0;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;

    ULONG_PTR drawing = 0;
    const Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&drawing, &startup, nullptr) != Gdiplus::Ok) {
        CoUninitialize();
        return 1;
    }

    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LINK_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    App app{instance, isKorean ? korean : english};

    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance;
    type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(APP_ICON));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(APP_ICON), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    type.lpszClassName = windowClass;

    const UINT dpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(clientWidth, dpi, USER_DEFAULT_SCREEN_DPI),
                    MulDiv(clientHeight, dpi, USER_DEFAULT_SCREEN_DPI)};
    AdjustWindowRectExForDpi(&bounds, windowStyle, FALSE, extendedStyle, dpi);
    const std::wstring title = std::wstring(appName) + L" " + version;
    HWND window = nullptr;
    if (RegisterClassExW(&type)) {
        window = CreateWindowExW(extendedStyle, windowClass, title.c_str(), windowStyle,
                   CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                   nullptr, nullptr, instance, &app);
    }
    if (!window) {
        MessageBoxW(nullptr, isKorean ? L"앱을 시작하지 못했습니다." : L"The app could not start.",
                    appName, MB_OK | MB_ICONERROR);
        Gdiplus::GdiplusShutdown(drawing);
        CoUninitialize();
        return 1;
    }

    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    BOOL result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message == WM_LBUTTONDOWN || message.message == WM_KEYDOWN) {
            const bool keyboard = message.message == WM_KEYDOWN;
            if (keyboard != app.keyboardFocus) {
                app.keyboardFocus = keyboard;
                InvalidateRect(app.portrait, nullptr, FALSE);
            }
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    Gdiplus::GdiplusShutdown(drawing);
    CoUninitialize();
    return result < 0 ? 1 : static_cast<int>(message.wParam);
}
