#ifndef UIAPPLICATION_H
#define UIAPPLICATION_H

#include "uidefines.h"
#include "uiobject.h"
#include "uicommon.h"
#include <vector>
#ifdef _WIN32
# include <Windows.h>
#else
# include <gtk/gtk.h>
#endif


class UIStyle;
class UIWidget;
class DECL_VISUALUI UIApplication : public UIObject
{
public:
#ifdef _WIN32
    explicit UIApplication(HINSTANCE hInstance, PWSTR cmdline, int cmdshow);
#else
    explicit UIApplication(int argc, char *argv[]);
#endif
    UIApplication(const UIApplication&) = delete;
    ~UIApplication();

    enum LayoutDirection : unsigned char {
        LeftToRight = 0,
        RightToLeft
    };

    UIApplication& operator=(const UIApplication&) = delete;
    static UIApplication *instance() noexcept;
#ifdef _WIN32
    HINSTANCE moduleHandle() noexcept;
#else
    static void postEvent(GtkWidget*, uint event_type, void *param);
    static bool sendEvent(GtkWidget*, uint event_type, void *param);
#endif
    void setLayoutDirection(LayoutDirection);
    void setFont(const FontInfo &fontInfo);
    FontInfo font() const noexcept;
    LayoutDirection layoutDirection() const noexcept;
    UIStyle* style() noexcept;

    std::vector<UIWidget*> windows() const;
    UIWidget* activeWindow() const noexcept;

    int exec();
    void exit(int);

private:
    friend class UIWidget;
    UIApplication();

    void registerWidget(UIWidget*, ObjectType, const Rect &rc);
    void unregisterWidget(UIWidget*, ObjectType);

    static UIApplication *inst;
    class UIApplicationPrivate;
    UIApplicationPrivate *d_ptr;
};

#endif // UIAPPLICATION_H
