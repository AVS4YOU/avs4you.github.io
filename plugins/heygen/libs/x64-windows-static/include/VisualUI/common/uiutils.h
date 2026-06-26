#ifndef UIUTILS_H
#define UIUTILS_H

#ifdef _WIN32
# include <Windows.h>
# include <gdiplus.h>
#else
# include <gio/gio.h>
#endif
#include "uidefines.h"


namespace UIUtils
{
#ifdef _WIN32
    enum WinVer : BYTE {
        Undef, WinXP, WinVista, Win7, Win8, Win8_1, Win10, Win11, WinFuture
    };
    DECL_VISUALUI WinVer winVersion() noexcept;
    DECL_VISUALUI std::wstring currentUserSID();
    DECL_VISUALUI DWORD regQueryDwordValue(HKEY rootKey, LPCWSTR subkey, LPCWSTR value, bool *success = nullptr);
    DECL_VISUALUI void loadImageResource(Gdiplus::Bitmap* &hBmp, int id, LPCWSTR type);
    DECL_VISUALUI void loadEmfResource(Gdiplus::Metafile* &hBmp, int id, LPCWSTR type);
    DECL_VISUALUI void loadStringResource(tstring &str, int id);
#else
    enum class DesktopEnv : unsigned char {
        UNDEF, UNITY, GNOME, KDE, XFCE, CINNAMON, OTHER
    };
    enum class WindowServer : unsigned char {
        UNDEF, X11, WAYLAND, OTHER
    };
    DECL_VISUALUI DesktopEnv desktopEnv();
    DECL_VISUALUI WindowServer windowServer();
    DECL_VISUALUI void loadStringResource(tstring &str, GResource *res, const char *resourcePath);
#endif

};

namespace UIMemory
{
    // Checks whether `addr` points to a block on the process stack.
    // Used before freeing memory to avoid accessing invalid or foreign memory regions.
    bool isOnStack(void *addr);
};

struct _GtkWindow;
namespace UIScreen
{
#ifdef _WIN32
    DECL_VISUALUI double dpiAtPoint(const POINT &pt);
    DECL_VISUALUI double dpiAtRect(const RECT &rc);
    DECL_VISUALUI RECT workAreaAtPoint(const POINT &pt);
    DECL_VISUALUI RECT workAreaFromWindow(HWND hwnd);
#else
    DECL_VISUALUI GdkRectangle workAreaAtPoint(const GdkPoint &pt);
    DECL_VISUALUI GdkRectangle workAreaFromWindow(_GtkWindow *window);
#endif
};

namespace UILocalization
{
#ifdef _WIN32
DECL_VISUALUI bool isRtlLanguage(unsigned long lcid);
#else
DECL_VISUALUI bool isRtlLanguage(const char *locale);
#endif
};

namespace UIUnicode
{
#ifdef _WIN32
DECL_VISUALUI std::wstring utf8ToWStr(const std::string &str);
DECL_VISUALUI std::string wstrToUtf8(const std::wstring &str);
#endif
// NOTE:
//  On Windows: 'pos' is an index in wchar_t units (UTF-16 code units).
//               Surrogate pairs take 2 wchar_t.
//  On Linux:   'pos' is a byte offset in UTF-8.
//               Multi-byte sequences take 2–4 bytes.

// Returns the length of the character starting at position 'pos'.
// NOTE: See above for meaning of 'pos' on Windows vs Linux.
DECL_VISUALUI size_t charLenAt(const tstring &str, size_t pos) noexcept;

// Returns the length of the character immediately before position 'pos'.
// NOTE: See above for meaning of 'pos' on Windows vs Linux.
DECL_VISUALUI size_t charLenBefore(const tstring &str, size_t pos) noexcept;

// Returns the position of the previous character relative to 'pos'.
// NOTE: See above for meaning of 'pos' on Windows vs Linux.
DECL_VISUALUI size_t charPrevPos(const tstring &str, size_t pos) noexcept;

// Returns the position of the next character relative to 'pos'.
// NOTE: See above for meaning of 'pos' on Windows vs Linux.
DECL_VISUALUI size_t charNextPos(const tstring &str, size_t pos) noexcept;
};

#endif // UIUTILS_H
