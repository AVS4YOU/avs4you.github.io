#ifndef UICOMMON_H
#define UICOMMON_H

#include "uidefines.h"
#ifdef _WIN32
# include <Windows.h>
#else
# include "gtk/gtk.h"
#endif

struct DECL_VISUALUI Margins {
    Margins() noexcept;
    Margins(int, int, int, int) noexcept;

    int left, top, right, bottom;
};

struct DECL_VISUALUI Point {
    Point() noexcept;
    Point(int, int) noexcept;

    int x, y;
};

struct DECL_VISUALUI Rect {
    Rect() noexcept;
    Rect(int, int, int, int) noexcept;

    bool contains(const Point &pt);

    int x, y, width, height;
};

struct DECL_VISUALUI Size {
    Size() noexcept;
    Size(int, int) noexcept;

    int width, height;
};

#ifdef _WIN32
struct NotifyParams {
    HWND senderHwnd = nullptr;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
};
#else
typedef Point POINT;
typedef Rect RECT;

struct DECL_VISUALUI DropFilesInfo {
    GdkDragContext *context;
    gint x, y;
    GtkSelectionData *sel_data;
    guint info, time;
};

struct DECL_VISUALUI FontDescription {
    PangoFontDescription *desc = nullptr;
    bool underline = false;
    bool strikeOut = false;
};
#endif

struct DECL_VISUALUI FontInfo {
    FontInfo(std::string _name = DEFAULT_FONT_NAME, float _pointSize = 10.0f, int _weight = 400,
                       bool _italic = false, bool _underline = false, bool _strikeOut = false) :
        name(_name),
        pointSize(_pointSize),
        weight(_weight),
        italic(_italic),
        underline(_underline),
        strikeOut(_strikeOut)
    {}

    std::string name;
    float pointSize;
    int weight;
    bool italic;
    bool underline;
    bool strikeOut;
};

namespace SizePolicy
{
    enum Properties : unsigned char {
        HSizeBehavior,
        VSizeBehavior,
        PROPERTIES_LAST
    };

    enum SizeBehavior : unsigned char {
        Fixed,
        Expanding,
        SIZE_BEHAVIOR_LAST
    };
}

#endif // COMMON_H
