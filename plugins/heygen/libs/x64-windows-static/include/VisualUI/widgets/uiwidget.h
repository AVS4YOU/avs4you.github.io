#ifndef UIWIDGET_H
#define UIWIDGET_H

#include "uiobject.h"
#include "uiplatformtypes.h"
#include "uicommon.h"
#include "uidrawingsurface.h"
#ifndef CW_USEDEFAULT
# define CW_USEDEFAULT 100
#endif


class UILayout;
class UIDragHandler;
class UIGeometryAnimation;
class DECL_VISUALUI UIWidget : public UIObject, public UIDrawningSurface
{
public:
    explicit UIWidget(UIWidget *parent = nullptr);
    virtual ~UIWidget();

    virtual void setObjectGroupId(const tstring &id) override;
    virtual void setGeometry(int, int, int, int);
    virtual void move(int, int);
    virtual void resize(int, int);
    void setDisabled(bool);
    virtual void close();
    UIWidget* parentWidget() const noexcept;
    virtual Size size() const noexcept;
    virtual void size(int*, int*) const noexcept;
    virtual Point pos() const noexcept;
    Point mapToGlobal(Point localPos) const;
    void updateGeometry();
    UIGeometryAnimation* geometryAnimation() noexcept;
    void setGeometryAnimation(UIGeometryAnimation*) noexcept;
    UIDragHandler* dragHandler() noexcept;
    void setDragHandler(UIDragHandler*) noexcept;
    void applyStyle();
    void setSizePolicy(SizePolicy::Properties, int) noexcept;
    void setFont(const FontInfo &fontInfo);
    void setBaseSize(int w, int h);
    void setCorners(unsigned char corner) noexcept;
    void setAcceptDrops(bool);
    void show();
    void hide();
    void repaint();
    void update();
    void bringAboveSiblings();
    void setLayout(UILayout *lut);
    bool isCreated() const noexcept;
    bool isActive() const noexcept;
    bool isVisible() const noexcept;
    bool isWindow() const noexcept;
    bool underMouse();
    void grabMouse();
    void ungrabMouse();
    FontInfo font() const;
    int  sizePolicy(SizePolicy::Properties) const noexcept;
    double dpiRatio() const noexcept;
    UILayout* layout() const noexcept;
    PlatformWindow platformWindow() const noexcept;
    UIWidget* topLevelWidget() const noexcept;
    static UIWidget* widgetFromPlatformWindow(UIWidget *parent, PlatformWindow);
#ifdef _WIN32
    void enableClipSiblings() const;
#endif

    /* Signals */
    Signal<> createdSignal;
    Signal<> aboutToDestroySignal;
    Signal<bool> activationChangedSignal;
    Signal<bool*> closeSignal;
    Signal<int,int> resizeSignal;
    Signal<int,int> moveSignal;
    Signal<int,int> contextMenuSignal;
    Signal<const std::vector<tstring>&> dropFilesSignal;

    virtual void onInvokeMethod(long long wParam);

protected:
    friend class UIApplication;
    friend class UIFontMetrics;
    friend class UIToolTipHandler;
    UIWidget(UIWidget *parent, ObjectType type, PlatformWindow hWindow = nullptr, const Rect &rc = Rect(CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT));
#ifdef _WIN32
    virtual bool event(UINT, WPARAM, LPARAM, LRESULT*);
    LRESULT checkInputRegion(LPARAM lParam, const RECT &rc);
#else
    virtual bool event(uint ev_type, void *param);
    void updateInputRegion(const RECT &rc);
#endif
    virtual void onPaint(const RECT &rc);
    virtual void onAfterCreated();

    PlatformFont   m_hFont;
    PlatformWindow m_hWindow;
#ifdef _WIN32
    HWND m_root_hWnd;
    bool m_root_is_layered;
#endif
    UILayout    *m_layout;
    double       m_dpi_ratio;
    unsigned char m_corners;
    bool         m_disabled,
                 m_rtl;

private:
    friend class UICheckBox;
    friend class UIRadioButton;
    friend class UIToggleButton;

#ifdef __linux
    virtual GtkWidget* gtkLayout();
    Size m_size;
    Point m_pos;
#endif
    void setPlatformWindow(PlatformWindow);

    int m_size_behaviors[SizePolicy::PROPERTIES_LAST];

    Size    m_base_size;
    FontInfo m_fontInfo;
    UIDragHandler *m_drag_handler;
    UIGeometryAnimation *m_geometry_animation;
    bool   m_is_created,
           m_is_active,
           m_is_destroyed,
           m_is_class_destroyed,
           m_mouse_entered;
};

#endif // UIWIDGET_H
