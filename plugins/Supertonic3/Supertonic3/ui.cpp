#include "pch.h"
#include "ui.h"
#include "export_utils.h"
#include "../../../sdk/ui/winapi/ui.h"
#include "../../../sdk/translate/translate.h"

#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace
{
constexpr int ID_TEXT = 101;
constexpr int ID_LANGUAGE = 102;
constexpr int ID_STYLE = 103;
constexpr int ID_STEPS = 104;
constexpr int ID_SPEED = 105;
constexpr int ID_DOWNLOAD_MODEL = 106;
constexpr int ID_GENERATE = 107;
constexpr int CLIENT_WIDTH = 680;
constexpr int CLIENT_HEIGHT = 430;

struct Controls
{
    HWND text = nullptr;
    HWND language = nullptr;
    HWND style = nullptr;
    HWND steps = nullptr;
    HWND speed = nullptr;
    HWND progress = nullptr;
    HWND download = nullptr;
    HWND generate = nullptr;
    HWND status = nullptr;
};

std::wstring Tr(const wchar_t* text)
{
    auto* manager = CTranslate::GetInstance().GetManager();
    return manager ? manager->Translate(text) : std::wstring(text);
}
void ReleaseUiActivationContext(SupertonicPlugin* plugin)
{
    if (!plugin)
        return;

    if (plugin->activationCookie)
        DeactivateActCtx(0, plugin->activationCookie);
    if (plugin->activationContext != INVALID_HANDLE_VALUE)
        ReleaseActCtx(plugin->activationContext);

    plugin->activationContext = INVALID_HANDLE_VALUE;
    plugin->activationCookie = 0;
    plugin->activationThreadId = 0;
}

void CenterWindow(HWND window, HWND preferredParent)
{
    RECT windowRect{};
    GetWindowRect(window, &windowRect);
    RECT target{};
    HWND parent = preferredParent && IsWindow(preferredParent)
        ? preferredParent : GetWindow(window, GW_OWNER);
    if (parent && IsWindow(parent))
        GetWindowRect(parent, &target);
    else
    {
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
        target = info.rcWork;
    }
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    SetWindowPos(window, nullptr,
        target.left + (target.right - target.left - width) / 2,
        target.top + (target.bottom - target.top - height) / 2,
        0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
}

HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int width, int height,
                 AVS::LabelType type = AVS::LabelType::Disabled,
                 DWORD alignment = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
    return AVS::CreateLabel(parent, g_module, text, x, y, width, height,
        AVS::LabelSettings::Create(type, alignment));
}

HWND CreateEdit(HWND parent, int x, int y, int width, int height,
                const wchar_t* text, bool multiline = false)
{
    auto settings = AVS::TextEditSettings::Create();
    settings.IsMultiline = multiline;
    settings.IsVscroll = multiline;
    HWND edit = AVS::CreateTextEditMultiline(parent, g_module, x, y, width, height, settings);
    SetWindowTextW(edit, text);
    return edit;
}

std::wstring VoiceStyleDisplayName(const std::filesystem::path& style)
{
    const std::wstring id = style.stem().wstring();
    if (id.size() >= 2 && id[0] == L'F')
        return id + L" " + Tr(L"Female") + L" " + id.substr(1);
    if (id.size() >= 2 && id[0] == L'M')
        return id + L" " + Tr(L"Male") + L" " + id.substr(1);
    return id;
}

std::wstring GetText(HWND control)
{
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length), L'\0');
    if (length > 0)
        GetWindowTextW(control, value.data(), length + 1);
    return value;
}

int GetInteger(HWND control, int fallback)
{
    try { return std::stoi(GetText(control)); }
    catch (...) { return fallback; }
}

float GetReal(HWND control, float fallback)
{
    try { return std::stof(GetText(control)); }
    catch (...) { return fallback; }
}

void SetBusy(Controls* controls, bool busy)
{
    EnableWindow(controls->text, !busy);
    EnableWindow(controls->language, !busy);
    EnableWindow(controls->style, !busy);
    EnableWindow(controls->steps, !busy);
    EnableWindow(controls->speed, !busy);
    EnableWindow(controls->download, !busy);
    EnableWindow(controls->generate, !busy);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* plugin = reinterpret_cast<SupertonicPlugin*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    auto* controls = reinterpret_cast<Controls*>(GetPropW(window, L"Supertonic3.Controls"));

    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    switch (message)
    {
    case WM_CREATE:
    {
        plugin = reinterpret_cast<SupertonicPlugin*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        controls = new Controls();
        SetPropW(window, L"Supertonic3.Controls", controls);

        CreateLabel(window, Tr(L"Text to synthesize").c_str(), 15, 12, 250, 22);
        controls->text = CreateEdit(window, 15, 36, 650, 170,
            Tr(L"Enter the text you want to synthesize.").c_str(), true);

        CreateLabel(window, Tr(L"Language").c_str(), 15, 222, 120, 20);
        CreateLabel(window, Tr(L"Voice style").c_str(), 155, 222, 220, 20);
        CreateLabel(window, Tr(L"Steps").c_str(), 400, 222, 80, 20);
        CreateLabel(window, Tr(L"Speed").c_str(), 500, 222, 80, 20);

        const std::vector<std::wstring> languages{
            L"EN", L"RU", L"UK", L"DE", L"FR", L"ES", L"IT",
            L"PT", L"PL", L"TR", L"VI", L"JA", L"KO"
        };
        controls->language = AVS::CreateComboBox(window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LANGUAGE)), g_module,
            15, 246, 120, 28, AVS::ComboBoxSettings::Create(), languages);

        std::vector<std::wstring> styles;
        styles.reserve(plugin->voiceStyles.size());
        for (const auto& style : plugin->voiceStyles)
            styles.push_back(VoiceStyleDisplayName(style));
        controls->style = AVS::CreateComboBox(window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STYLE)), g_module,
            155, 246, 220, 28, AVS::ComboBoxSettings::Create(), styles);
        controls->steps = CreateEdit(window, 400, 246, 80, 28, L"8");
        controls->speed = CreateEdit(window, 500, 246, 80, 28, L"1.05");

        controls->progress = AVS::CreateProgressBar(window, g_module, 15, 300, 650, 20,
            AVS::ProgressBarSettings::Create());
        AVS::ProgressBar_SetRange(controls->progress, 0, 100);
        AVS::ProgressBar_SetPos(controls->progress, 0);
        ShowWindow(controls->progress, SW_HIDE);

        const std::wstring initialStatus = plugin->modelDirectory.empty()
            ? Tr(L"Press Download Model.")
            : (plugin->voiceStyles.empty() ? Tr(L"No voice styles were found.") : Tr(L"Ready"));
        controls->status = CreateLabel(window, initialStatus.c_str(), 15, 338, 320, 60,
            AVS::LabelType::Enabled, DT_LEFT | DT_TOP | DT_WORDBREAK);
        controls->download = AVS::CreateButton(window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_DOWNLOAD_MODEL)), g_module,
            Tr(L"Download Model").c_str(), 350, 342, 170, 32,
            AVS::ButtonSettings::Create(AVS::Buttons::Default));
        controls->generate = AVS::CreateButton(window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_GENERATE)), g_module,
            Tr(L"Generate").c_str(), 535, 342, 130, 32,
            AVS::ButtonSettings::Create(AVS::Buttons::Primary));

        SetFocus(controls->text);
        plugin->window = window;
        return 0;
    }

    case WM_COMMAND:
        if (!plugin || !controls)
            break;
        if (LOWORD(wParam) == ID_DOWNLOAD_MODEL)
        {
            if (plugin->busy)
                return 0;
            SetBusy(controls, true);
            ShowWindow(controls->progress, SW_SHOW);
            AVS::ProgressBar_SetPos(controls->progress, 0);
            AVS::Label_SetText(controls->status, Tr(L"Preparing model download...").c_str());
            plugin->StartDownload();
            return 0;
        }
        if (LOWORD(wParam) != ID_GENERATE)
            break;
        if (plugin->busy)
            return 0;
        if (plugin->modelDirectory.empty())
        {
            AVS::Label_SetText(controls->status, Tr(L"Press Download Model.").c_str());
            return 0;
        }
        if (plugin->voiceStyles.empty())
        {
            AVS::Label_SetText(controls->status, Tr(L"No voice styles were found.").c_str());
            return 0;
        }
        {
            GenerationOptions options;
            options.text = GetText(controls->text);
            if (options.text.empty())
            {
                AVS::Label_SetText(controls->status, Tr(L"Enter text before generation.").c_str());
                return 0;
            }
            options.language = AVS::ComboBox_GetCurrentText(controls->language);
            std::transform(options.language.begin(), options.language.end(),
                options.language.begin(), [](wchar_t value) {
                    return static_cast<wchar_t>(std::towlower(value));
                });
            const int styleIndex = AVS::ComboBox_GetCurrent(controls->style);
            if (styleIndex < 0 || styleIndex >= static_cast<int>(plugin->voiceStyles.size()))
            {
                AVS::Label_SetText(controls->status, Tr(L"Select a voice style.").c_str());
                return 0;
            }
            options.voiceStyle = plugin->voiceStyles[static_cast<size_t>(styleIndex)];
            options.steps = (std::max)(1, GetInteger(controls->steps, 8));
            options.speed = GetReal(controls->speed, 1.05f);
            if (!(options.speed > 0.0f))
            {
                AVS::Label_SetText(controls->status, Tr(L"Speed must be greater than zero.").c_str());
                return 0;
            }
            SetBusy(controls, true);
            ShowWindow(controls->progress, SW_SHOW);
            AVS::ProgressBar_SetPos(controls->progress, 15);
            AVS::Label_SetText(controls->status, Tr(L"Generating speech locally...").c_str());
            plugin->Start(std::move(options));
            return 0;
        }

    case WM_SUPERTONIC_STATUS:
        if (plugin && controls)
        {
            std::unique_ptr<UiStatus> status(reinterpret_cast<UiStatus*>(lParam));
            AVS::Label_SetText(controls->status, status->text);
            if (status->progress >= 0)
                AVS::ProgressBar_SetPos(controls->progress, status->progress);
            if (status->finished)
            {
                ShowWindow(controls->progress, SW_HIDE);
                SetBusy(controls, false);
                if (status->success && !status->outputReady)
                {
                    std::vector<std::wstring> styles;
                    styles.reserve(plugin->voiceStyles.size());
                    for (const auto& style : plugin->voiceStyles)
                        styles.push_back(VoiceStyleDisplayName(style));
                    AVS::ComboBox_SetItems(controls->style, styles,
                        styles.empty() ? -1 : 0);
                }
                RedrawWindow(controls->language, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
                RedrawWindow(controls->style, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
                RedrawWindow(window, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

                if (status->success && status->outputReady && plugin->callback)
                {
                    ReleaseUiActivationContext(plugin);
                    plugin->callback(ExportString(L"Supertonic3.plugin"),
                        ExportString(plugin->lastOutput.c_str()), 0, plugin->callbackContext);
                    DestroyWindow(window);
                }
            }
        }
        return 0;

    case WM_CLOSE:
        if (plugin && plugin->busy)
        {
            if (controls)
                AVS::Label_SetText(controls->status, Tr(L"Wait for the current operation to finish.").c_str());
            return 0;
        }
        // Release the UI-thread activation context before the parent receives
        // any synchronous notification caused by window destruction.
        ReleaseUiActivationContext(plugin);
        DestroyWindow(window);
        return 0;

    case WM_NCDESTROY:
        if (plugin)
        {
            plugin->window = nullptr;
            plugin->Join();
            ReleaseUiActivationContext(plugin);

        }
        delete controls;
        RemovePropW(window, L"Supertonic3.Controls");
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

void ShowSupertonicWindow(SupertonicPlugin* plugin)
{
    if (plugin->window && IsWindow(plugin->window))
    {
        ShowWindow(plugin->window, SW_RESTORE);
        SetForegroundWindow(plugin->window);
        return;
    }

    ACTCTXW activation{sizeof(activation)};
    activation.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
    activation.lpResourceName = MAKEINTRESOURCEW(1);
    activation.hModule = g_module;
    plugin->activationContext = CreateActCtxW(&activation);
    plugin->activationCookie = 0;
    plugin->activationThreadId = 0;
    if (plugin->activationContext != INVALID_HANDLE_VALUE)
    {
        if (ActivateActCtx(plugin->activationContext, &plugin->activationCookie))
            plugin->activationThreadId = GetCurrentThreadId();
        else
            ReleaseUiActivationContext(plugin);
    }

    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&commonControls);

    static const wchar_t* className = L"Supertonic3MainWindowClass";
    WNDCLASSEXW existing{sizeof(existing)};
    if (!GetClassInfoExW(g_module, className, &existing))
    {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = g_module;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        const auto iconPath = plugin->workDirectory / L"icon_internal.ico";
        windowClass.hIcon = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(),
            IMAGE_ICON, 32, 32, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(nullptr, iconPath.c_str(),
            IMAGE_ICON, 16, 16, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
        const AVS::Color background = AVS::Color::GetDefaultWindowBackground();
        windowClass.hbrBackground = CreateSolidBrush(RGB(background.R, background.G, background.B));
        windowClass.lpszClassName = className;
        RegisterClassExW(&windowClass);
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rectangle{0, 0, CLIENT_WIDTH, CLIENT_HEIGHT};
    AdjustWindowRectEx(&rectangle, style, FALSE, 0);
    const std::wstring windowTitle = Tr(L"Supertonic 3");
    HWND window = CreateWindowExW(0, className, windowTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        plugin->parentWindow, nullptr, g_module, plugin);
    if (!window)
    {
        if (plugin->activationCookie)
        {
            DeactivateActCtx(0, plugin->activationCookie);
            plugin->activationCookie = 0;
        }
        if (plugin->activationContext != INVALID_HANDLE_VALUE)
        {
            ReleaseActCtx(plugin->activationContext);
            plugin->activationContext = INVALID_HANDLE_VALUE;
        }
        return;
    }
    CenterWindow(window, plugin->parentWindow);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}











