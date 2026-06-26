#ifndef UIABSTRACTSCROLLAREA_H
#define UIABSTRACTSCROLLAREA_H

#include "uiwidget.h"

class UIScrollBar;
class DECL_VISUALUI UIAbstractScrollArea : public UIWidget
{
public:
    enum ScrollBarPolicy {
        ScrollBarAsNeeded,
        ScrollBarAlwaysOff,
        ScrollBarAlwaysOn
    };

    enum ContentResizeMode {
        ResizeToVisible,
        ResizeToContent
    };

    explicit UIAbstractScrollArea(UIWidget *parent = nullptr);
    virtual ~UIAbstractScrollArea();

    void setContentWidget(UIWidget *widget) noexcept;
    void setContentWidth(int width);
    void setContentHeight(int height);
    void setContentSize(int width, int height);
    void setWheelScrollStep(int step) noexcept;
    void setVerticalScrollBarPolicy(ScrollBarPolicy policy);
    void setHorizontalScrollBarPolicy(ScrollBarPolicy policy);
    void setVerticalContentResizeMode(ContentResizeMode mode);
    void setHorizontalContentResizeMode(ContentResizeMode mode);
    void scrollToTop();
    void scrollToBottom();
    void scrollToLeft();
    void scrollToRight();

    UIWidget* contentWidget() noexcept;
    int wheelScrollStep() const noexcept;
    int scrollBarThickness() const noexcept;
    ScrollBarPolicy verticalScrollBarPolicy() const noexcept;
    ScrollBarPolicy horizontalScrollBarPolicy() const noexcept;
    ContentResizeMode verticalContentResizeMode() const noexcept;
    ContentResizeMode horizontalContentResizeMode() const noexcept;

    /* Signals */
    Signal<int> scrollPositionChanged;

protected:
#ifdef _WIN32
    virtual bool event(UINT, WPARAM, LPARAM, LRESULT*) override;
#else
    virtual bool event(uint ev_type, void *param) override;
#endif
    virtual void onScrollOffsetChanged() = 0;
    int maxScrollOffsetX() const noexcept;
    int maxScrollOffsetY() const noexcept;

    UIWidget *m_contentWidget;
    UIScrollBar *m_verticalScrollBar;
    UIScrollBar *m_horizontalScrollBar;
    int m_scrollOffsetX;
    int m_scrollOffsetY;

private:
    struct ScrollBarState {
        bool needVertical;
        bool needHorizontal;
        int availableWidth;
        int availableHeight;
    };
    
    ScrollBarState calculateScrollBarState() const noexcept;
    void onMouseWheel(int delta);
    void createScrollBars();
    void updateScrollBarGeometry();
    void updateScrollBarPosition();

    int m_wheelScrollStep;
    int m_contentHeight;
    int m_contentWidth;
    ScrollBarPolicy m_verticalScrollBarPolicy;
    ScrollBarPolicy m_horizontalScrollBarPolicy;
    ContentResizeMode m_verticalContentResizeMode;
    ContentResizeMode m_horizontalContentResizeMode;
};

#endif // UIABSTRACTSCROLLAREA_H
